# ETL Retrofit of Non-Driver Code — Design Spec

**Ticket:** FW-RemoteStation #45 "Non-Driver-Code auf C++20 + ETL umstellen (Treiber bleiben C)"
**Branch:** `feature/etl-retrofit-nondriver` (off `origin/main`, in a git worktree)
**Date:** 2026-07-03

## Goal

Raise the existing **non-driver** code (app / module layer / sim) to the canonical FW
standard: modern C++20 with **ETL fixed-capacity types** replacing hand-rolled C-style
buffers, tagged unions, and low-level pointer plumbing. Drivers stay C. This is a
**type/idiom migration with identical observable behavior** — no feature, API, or wire
change.

Prerequisites already satisfied on `origin/main`: ETL 20.48.0 is build-integrated (#43,
`CONFIG_ETL=y`, `CONFIG_ETL_LOG_ERRORS=y`, error handler in `app/src/etl_error_handler.cpp`)
and the coding policy is canonical (#46, CLAUDE.md + CONTRIBUTING.md).

## Constraints (binding — CONTRIBUTING.md §3)

- **No dynamic allocation.** `new`/`delete`, `malloc`/`free`, `std::string`, `std::vector`,
  `std::map`, `std::function`, or any internally-allocating type are forbidden.
- **No exceptions, no RTTI.** Error handling via return/status types; virtual dispatch, not
  `dynamic_cast`.
- **Two-tier split by layer, not extension.** `drivers/` stay C-first Zephyr idiom;
  non-driver code is modern C++20 + ETL.
- **Behavior identical.** `tests/sim_shell` (black-box pytest over the shell) must stay green
  **unchanged** — that is the proof of identical behavior.
- CI `build_and_tests` **and** `clang_format` (clang-format-18) must stay green.

## Critical ETL behavior facts (verified in vendored `modules/lib/etl` 20.48.0)

These govern every decision below:

1. **`etl::string<N>` truncates gracefully, does NOT panic.** The overflow-as-error path
   (`ETL_ASSERT_FAIL(string_truncation)`) is gated by `ETL_HAS_ERROR_ON_STRING_TRUNCATION`,
   which requires `ETL_STRING_TRUNCATION_IS_ERROR` — **not defined** in this build. On
   overflow the string clips to `N` and sets `is_truncated()`; the ETL error handler is
   **not** invoked. → `etl::string<N>` preserves the current silent-truncation semantics.
2. **`etl::vector<T,N>` overflow DOES panic.** No truncation flag; an over-capacity insert
   invokes the error handler → `k_panic()`. → any `etl::vector` write path must keep an
   explicit capacity clamp so overflow is unreachable.
3. **`std::variant` is allocation-free** as long as every alternative is; `etl::string<N>`
   qualifies. `std::get_if`/`index()` never throw (only `std::get` on the wrong type would);
   we use the non-throwing accessors, so `-fno-exceptions` is safe.

## Scope — the changes (surgical)

### C1 — `mod::Result` → real discriminated union (`include/oe5xrx/module/iface.h`)

Today `Result` is a hand-rolled tagged union: a `VK` enum tag plus parallel scalar members
(`int i_`, `double f_`, `bool b_`), a `const char* s_` that points either at an external
literal or at an internal `char sbuf_[16]`, and **hand-written copy-ctor + copy-assignment
whose only job is to re-point `s_` at the internal buffer after a copy**. This is the
prime "low-level C++" cleanup target.

Replace the value representation with:

```cpp
std::variant<std::monostate, int, double, bool, etl::string<15>> value_;
```

- `std::monostate` = `okNull`.
- `okInt`/`okFloat`/`okBool` store the scalar alternative.
- `okStr(literal)` and `okStrCopy(buf)` **unify**: both store into the `etl::string<15>`
  alternative (every current caller — `"high"`, `"low"`, `"25"`, `"12.5"`, `"vhf"`/`"uhf"`,
  tone strings — is ≤ 7 chars; capacity 15 equals today's 16-byte buffer minus NUL).
- The error arm stays a `const char* err_` (error codes are static literals) plus the
  `bool ok_` discriminant — value vs. error is orthogonal to the value's type.
- **Delete both hand-written copy operators.** `std::variant` + `etl::string` are trivially
  copyable-by-value; the compiler-generated copy is correct. The `VK` enum, the parallel
  scalar members, `s_`, and `sbuf_` all disappear.
- `renderValue(JsonWriter&)` switches on `value_.index()` (or `std::get_if`), formatting each
  arm exactly as today: `snprintf("%d")` for int, the existing `%.4f` + `isfinite`→`null`
  path for float, `"true"/"false"` for bool, `w.quoted(str.c_str())` for string, `"null"`
  for monostate. **Byte-identical JSON output.**

Behavior identity: numeric formatting is unchanged (still `snprintf`). String overflow
(pathological only; impossible with current inputs) truncates, matching today — never panics.

### C2 — `sa818_module.cpp` tone handling

The four tone sites (`TxToneCap`/`RxToneCap` × set/get) call the **C driver**
`sa818_at_tone_to_str(code, char* buf, size_t)` which writes into a caller `char[16]`. That
buffer stays `char[16]` — it is the driver's C ABI. The result is then handed to the new
`Result` string API (`okStrCopy`), which now stores into the `etl::string<15>` arm. No driver
file is touched. Net effect: the tone strings flow through the ETL-backed `Result` unchanged.

### C3 — `wav_source` buffer → `etl::vector` (`app/src/sim_audio/wav_source.{h,cpp}`)

Replace `std::array<int16_t, wav_max_samples> buf_` **and** the separate
`std::size_t count_samples_` with a single:

```cpp
etl::vector<int16_t, sim_audio::wav_max_samples> buf_;
```

- `buf_.size()` replaces `count_samples_` everywhere (`count_samples()`, `loaded()`,
  `next_sample_norm()` wrap check, `samples()`).
- The parse loop `push_back`s each decoded sample instead of indexed assignment.
- **Keep** the existing clamp `samples_to_read = min(available_samples, buf_.capacity())` so
  the vector never exceeds capacity → the panic path (fact #2) is unreachable, matching the
  current "fill to capacity, drop the rest" behavior.
- `idx_samples_` (playback cursor) stays a separate `std::size_t` — different concern.
- `samples()` still returns `std::span<const int16_t>` (`{buf_.data(), buf_.size()}`).
- Reset-on-error path clears `buf_` (`buf_.clear()`) in place of zeroing the count.

### C4 — `sim_audio` callbacks → `etl::delegate` (`sample_clock.{h,cpp}`, `audio_pipeline.{h,cpp}`)

Replace the raw `tick_fn_t = void (*)(void* user)` + `void* user_` pair in `SampleClock`
with a typed no-alloc delegate:

```cpp
etl::delegate<void()> tick_;
```

- `SampleClock::start(rate_hz, etl::delegate<void()> fn)` stores the delegate; the `k_timer`
  trampoline calls `tick_.call_if()` (invokes only when bound — replaces today's
  `if (!fn_) return` guard; `call_if` never panics on an unbound delegate, whereas
  `operator()` would).
- `AudioPipeline` binds its member: `etl::delegate<void()>::create<AudioPipeline,
  &AudioPipeline::on_tick>(*this)`. `on_tick` becomes a non-static member `void on_tick()`
  (drops the `static` + `void*` self-cast). `AudioPipeline` is a static-lifetime global
  (`g_pipe`), so the delegate's pointer-to-instance is valid for the program's life.
- Behavior identity: the timer still fires at the same rate and calls the same logic. This is
  the one change that is not a pure type-swap, so it gets an explicit test (C4-test below).

### C5 — `parse_int` / `parse_float` → `etl::to_arithmetic` (`sa818_module.cpp`)

Replace the `strtol`/`strtof` + end-pointer + `isfinite` dance with ETL's checked, no-alloc,
exception-free parse:

```cpp
etl::to_arithmetic<long>(etl::string_view{s});
etl::to_arithmetic<float>(etl::string_view{s});
```

returning `std::optional` at the call boundary as today. `to_arithmetic` requires the whole
view to be a valid number (matching the current `*end != '\0'` rejection of trailing
garbage). `parse_bool` (custom `on/off/1/0/true/false`) stays as-is.

**Edge-semantics gate:** `to_arithmetic` and `strtol/strtof` may differ on leading `+`,
surrounding whitespace, or `inf`/`nan` spellings. Before this change lands, add characterizing
tests that pin the **current** accept/reject behavior for those edges; the ETL version must
reproduce them. If any edge genuinely differs, keep a thin adapter so the observable
accept/reject set is identical (identical behavior beats idiom purity).

**C5 note:** one intentional behavior change — an integer that overflows `long` now returns
`bad_value` (parse-time reject) instead of the old `out_of_range` (strtol clamp + range check).
Accepted as an improvement; pinned by `test_module_set_parse_edges`.

## Out of scope (deliberate)

- **Driver code** (`drivers/radio/sa818/…`) — untouched, stays C.
- **`JsonWriter` internals** — its raw `char*`/`cap`/`len` builder is a *documented*
  truncation-safe type (CLAUDE.md lists `mod::JsonWriter` as such). ETL buys nothing here but
  panic-risk; left as-is.
- **`numStr` / `renderValue` number formatting** — `etl::to_string` formats floats
  differently from the existing `%.4f` / `%g`+`.0` logic → not byte-identical. Kept as
  `snprintf`.
- **Registry lookups** (`Module::find`/`ModuleRegistry::find` linear `strcmp` over
  `std::span`) — tiny static arrays; `etl::flat_map` adds no value.
- **`std::span` / `std::array` / `std::optional` / `std::string_view` / `std::variant`** that
  are already non-allocating and canonically permitted — **not** converted to ETL
  equivalents. "ETL-before-STL" as a blanket house rule is explicitly rejected for this
  ticket; ETL replaces only *allocating* / *hand-rolled* constructs.
- **`argv`-derived `const char*`** at `strcmp`/parse boundaries — stay `const char*`
  (`std::string_view` is not NUL-terminated; the C driver + parsers need NUL).
- **`usb_audio_bridge.cpp`, `main_usb_audio.cpp`, `usb_config.cpp`** — Zephyr/USB C-ABI
  boundary (ring_buf, k_mutex, DMA-aligned pools, `USBD_*` macros, UAC2/SA818 callback
  structs). This is framework-boundary C by design, not un-modernized code; converting it
  (e.g. ring_buf → `etl::circular_buffer` in ISR/USB-timing context) is a real risk, not a
  cleanup.

## Testing strategy (TDD, `native_sim`)

The DoD proof is **identical behavior**, so the existing black-box suite is the primary gate:

- **`tests/sim_shell` pytest passes unchanged.** Capture a baseline run before any code
  change; it must stay green after each change with no test edits (edits would mean behavior
  drifted).
- **Characterization tests added first** where a change has subtle edges:
  - **C1/C2:** `module <id> set tx_tone <code>` then `get tx_tone` round-trip, and a
    `set bandwidth`/`get bandwidth` + `get band` case — exercises every `Result` value arm
    (string, and via other caps int/float/bool/null) through the new variant renderer; assert
    the exact JSON.
  - **C3:** `wav load <file>` + `wav info` sample-count for a file larger than a trivial size
    (exercises `etl::vector` fill + `.size()`); plus a file at/over `wav_max_samples` if
    feasible to assert the clamp still drops the tail (no panic).
  - **C4:** `wav sine …` / `wav start` then `wav info running=1` and `wav stop` — proves the
    delegate-bound timer still fires and drives the pipeline.
  - **C5:** characterizing accept/reject cases for `frequency`/`volume`/`squelch` set with
    malformed inputs (trailing garbage, leading `+`, whitespace, empty) pinned to current
    behavior, then held across the `to_arithmetic` swap.
- **`tests/etl`** must remain green (unaffected).

## Build & CI

- **Builds:** `west build -b fm_board app` and `west build -b native_sim/native/64 app`.
- **Local build environment:** west at `/home/pbuchegger/OE5XRX/.zephyr-venv/bin/west`, SDK
  `~/zephyr-sdk-1.0.1`.
- **T2-workspace prerequisite (execution-time):** this is a single west workspace whose
  manifest-repo checkout is the *main* tree (currently on another session's branch). When
  building from this worktree, ETL/HAL modules must resolve. Resolve at execution start by
  the least-invasive means that works — preferring not to mutate shared workspace state
  (e.g. `-DZEPHYR_EXTRA_MODULES=/home/pbuchegger/OE5XRX/modules/lib/etl` if the active
  manifest doesn't already list etl). Confirm `west list` / a probe build **before** starting
  C1; do not disturb the main checkout.
- **clang-format-18** over changed files before every commit; `clang_format` CI job must be
  clean.

## Definition of Done (from #45)

- Non-driver code uses ETL instead of heap/hand-rolled c-strings **where it genuinely
  improves the code** (C1–C5); drivers unchanged C.
- Behavior identical: `tests/sim_shell` green **without test-behavior edits**;
  `fm_board` + `native_sim` build green; `tests/etl` green.
- CI green (`build_and_tests` + `clang_format`).
- One PR against `main`, `Closes #45`.

## Risks & mitigations

| Risk | Mitigation |
|------|------------|
| `etl::vector` overflow → `k_panic()` | Keep the `min(available, capacity)` clamp (fact #2); test the at-capacity case. |
| `to_arithmetic` edge-semantics differ from `strtof/strtol` | Characterization tests first; thin adapter if needed (C5). |
| `std::variant` renderer changes JSON output | Keep exact `snprintf` formatting per arm; assert byte-exact JSON in tests. |
| Delegate bound to a non-static instance | Only `sim_audio` globals are bound (`g_pipe`); static lifetime — safe. |
| T2 workspace module resolution from worktree | Probe/resolve before C1; prefer `ZEPHYR_EXTRA_MODULES` over mutating shared manifest state. |
| Disturbing the parallel FW session (main tree) | Work only in the worktree; never switch the main checkout's branch. |
