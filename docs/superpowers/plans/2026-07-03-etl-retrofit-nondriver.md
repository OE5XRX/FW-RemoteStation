# ETL Retrofit of Non-Driver Code — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate non-driver code (module layer / sim) from hand-rolled C-style buffers, a tagged-union `Result`, raw callback pointers, and `strtol/strtof` parsing to ETL fixed-capacity types + `std::variant`, with byte-identical observable behavior. Drivers stay C.

**Architecture:** Five surgical, independently-testable type/idiom swaps (C1–C5). The existing `tests/sim_shell` pytest suite is black-box over the shell and already characterizes every touched behavior; it must stay green **without test-behavior edits** — that is the identical-behavior proof. New tests are added only for genuinely-new edges (C5 parse edges, C1 null arm).

**Tech Stack:** Zephyr (C++20, `-fno-exceptions`, no-RTTI, no-alloc), ETL 20.48.0 (`etl::string`, `etl::vector`, `etl::delegate`, `etl::to_arithmetic`), `std::variant`, Twister + pytest on `native_sim/native/64`.

## Global Constraints

- No dynamic allocation; no exceptions; no RTTI. (CONTRIBUTING.md §3.2)
- Drivers (`drivers/**`) are OUT OF SCOPE — do not edit. Two-tier split by layer.
- `etl::string<N>` truncates gracefully (no panic in this build); `etl::vector<T,N>` **panics** on overflow → every vector write path keeps an explicit capacity clamp.
- Use non-throwing variant access (`std::get_if` / `index()`), never `std::get`.
- Number formatting stays `snprintf` (`%d`, `%.4f`, `isfinite`→`null`) — byte-identical JSON.
- clang-format-18 over changed files before every commit; `clang_format` + `build_and_tests` CI must stay green.
- Spec: `docs/superpowers/specs/2026-07-03-etl-retrofit-nondriver-design.md`.
- Branch `feature/etl-retrofit-nondriver` (worktree). Never switch the main checkout's branch.

---

### Task 0: Establish build/test baseline in the worktree

**Files:** none (environment + baseline capture only).

**Interfaces:**
- Produces: the exact working `west build` / `west twister` invocations from this worktree, recorded here as `$WEST_BUILD` / `$WEST_TWISTER` for later tasks. All later tasks assume the baseline is green.

- [ ] **Step 1: Locate west and resolve module topology**

This is a T2 west workspace; the manifest-repo checkout is the *main* tree (another session, different branch). Determine whether ETL resolves from this worktree without mutating shared state.

Run:
```bash
WEST=/home/pbuchegger/OE5XRX/.zephyr-venv/bin/west
cd /home/pbuchegger/OE5XRX/FW-RemoteStation/.claude/worktrees/feature+etl-retrofit-nondriver
"$WEST" list 2>/dev/null | grep -iE 'etl|hal_stm32|cmsis' || echo "MODULES NOT IN ACTIVE MANIFEST"
```
Expected: either etl/hal_stm32/cmsis are listed (good), or not. If NOT listed, later builds must pass `-DZEPHYR_EXTRA_MODULES=/home/pbuchegger/OE5XRX/modules/lib/etl` (ETL is header-only; HAL/cmsis for fm_board resolve from the workspace's on-disk modules). Record which case applies.

- [ ] **Step 2: Baseline build — native_sim**

Run:
```bash
"$WEST" build -b native_sim/native/64 -d build/ns app
```
Expected: build succeeds (`Memory region ... ` / no errors). If it fails on a missing ETL/HAL module, re-run adding `-- -DZEPHYR_EXTRA_MODULES=/home/pbuchegger/OE5XRX/modules/lib/etl` and record the working command as `$WEST_BUILD`.

- [ ] **Step 3: Baseline build — fm_board**

Run:
```bash
"$WEST" build -b fm_board -d build/fm app
```
Expected: build succeeds. Record the working invocation (with `ZEPHYR_EXTRA_MODULES` if needed).

- [ ] **Step 4: Baseline tests — sim_shell + etl**

Run:
```bash
"$WEST" twister -T tests/sim_shell -p native_sim/native/64 -v --clobber-output
"$WEST" twister -T tests/etl -p native_sim/native/64 -v --clobber-output
"$WEST" twister -T app --integration -p native_sim/native/64 -v --clobber-output
```
Expected: all PASS. This is the green baseline every later task must preserve.

- [ ] **Step 5: Record the invocations**

Append the confirmed `$WEST_BUILD` (both boards) and `$WEST_TWISTER` commands as a comment block at the top of this file (or note them in the task handoff). Do NOT commit build artifacts (`build/` is ignored by Zephyr convention; verify with `git status`).

---

### Task 1: `mod::Result` → `std::variant` (C1)

**Files:**
- Modify: `include/oe5xrx/module/iface.h` (the `Result` class, ~lines 194–321, plus includes)
- Test: `tests/sim_shell/pytest/test_module_iface.py` (existing = characterization; add one null-arm case)

**Interfaces:**
- Consumes: `JsonWriter` (unchanged).
- Produces: `mod::Result` with the SAME public API — `okInt(int)`, `okFloat(double)`, `okBool(bool)`, `okStr(const char*)`, `okStrCopy(const char*)`, `okNull()`, `err(const char*)`, `render(JsonWriter&, const char*, const char*, const char*)`. Callers in `sa818_module.cpp` are unchanged.

- [ ] **Step 1: Add a band string-arm regression anchor**

The existing suite already asserts the float/int/bool/string/error arms (see
`test_module_describe_*`, `test_module_set_frequency_*`, `test_module_tones`). The one
arm with no dedicated positive assertion is the `band` telemetry string returned via
`Result::okStr` from a static range-name literal — exactly the zero-copy `okStr` path
that C1 changes from pointer-storage to owned `etl::string`. Add a focused anchor to
`tests/sim_shell/pytest/test_module_iface.py`:

```python
def test_module_band_string_arm(sa818_sim, shell):
    """`band` returns the named-range string via Result::okStr (the okStr arm C1 changes)."""
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm get band")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True
    assert r["cap"] == "band"
    assert r["op"] == "get"
    # Default power-on freq (145.5 VHF / 435.0 UHF) is inside the named band range,
    # so okStr returns the band name; this pins the okStr -> etl::string arm exactly.
    assert r["value"] == "vhf"
```

Note: the model is `SA818-V` (VHF) on this board (see `test_module_describe_valid_json`),
so the band name is `"vhf"`. The `okNull` (`monostate` -> `"null"`) arm is structurally
trivial in the variant renderer and needs no forced out-of-band case.

- [ ] **Step 2: Run the full module suite against unmodified code (baseline green)**

Run: `"$WEST_TWISTER" -T tests/sim_shell -p native_sim/native/64 -v --clobber-output`
Expected: PASS (incl. the new test). This pins current behavior before the refactor.

- [ ] **Step 3: Refactor `Result` to `std::variant`**

In `include/oe5xrx/module/iface.h`, add includes near the top (after the existing includes):
```cpp
#include <etl/string.h>
#include <variant>
```

Replace the entire `Result` class body with:
```cpp
/** @brief Outcome of a command: success carrying a typed value, or an error code. */
class Result {
public:
  static Result okInt(int v) {
    Result r(true);
    r.value_ = v;
    return r;
  }
  static Result okFloat(double v) {
    Result r(true);
    r.value_ = v;
    return r;
  }
  static Result okBool(bool v) {
    Result r(true);
    r.value_ = v;
    return r;
  }
  static Result okStr(const char *v) {
    Result r(true);
    r.value_ = etl::string<kStrCap>(v != nullptr ? v : "");
    return r;
  }
  static Result okNull() {
    Result r(true);
    r.value_ = std::monostate{};
    return r;
  }
  // Kept for source compatibility; okStr already copies into the owned string.
  static Result okStrCopy(const char *v) { return okStr(v); }
  static Result err(const char *code) {
    Result r(false);
    r.err_ = code;
    return r;
  }

  /** Render `{"ok":..,"module":..,"cap":..,"op":..,"value"|"error":..}` into @p w. */
  void render(JsonWriter &w, const char *module, const char *cap, const char *op) const {
    w.ch('{');
    w.kvRaw("ok", ok_ ? "true" : "false");
    w.ch(',');
    w.kvStr("module", module);
    w.ch(',');
    w.kvStr("cap", cap);
    w.ch(',');
    w.kvStr("op", op);
    w.ch(',');
    if (ok_) {
      w.key("value");
      renderValue(w);
    } else {
      w.kvStr("error", err_ != nullptr ? err_ : "error");
    }
    w.ch('}');
  }

private:
  static constexpr size_t kStrCap = 15;

  explicit Result(bool ok) : ok_(ok) {}

  /** Render the value arm as JSON: integer for Int, always-decimal for Float. */
  void renderValue(JsonWriter &w) const {
    char b[32];
    if (const int *i = std::get_if<int>(&value_)) {
      snprintf(b, sizeof(b), "%d", *i);
      w.raw(b);
    } else if (const double *f = std::get_if<double>(&value_)) {
      if (!isfinite(*f)) {
        // NaN/Inf are not valid JSON numbers; keep the contract valid.
        w.raw("null");
        return;
      }
      snprintf(b, sizeof(b), "%.4f", *f);
      w.raw(b);
    } else if (const bool *bp = std::get_if<bool>(&value_)) {
      w.raw(*bp ? "true" : "false");
    } else if (const etl::string<kStrCap> *s = std::get_if<etl::string<kStrCap>>(&value_)) {
      w.quoted(s->c_str());
    } else {
      w.raw("null"); // std::monostate
    }
  }

  bool ok_;
  std::variant<std::monostate, int, double, bool, etl::string<kStrCap>> value_{};
  const char *err_ = nullptr;
};
```

Note: `std::get_if<bool>` is checked before `int`? No — order among distinct types is irrelevant; each alternative type is unique, so exactly one `get_if` matches. Keep the `int` check first for readability.

- [ ] **Step 4: Build + run the suite (must be green, no test edits)**

Run:
```bash
"$WEST_BUILD_NS"   # native_sim build from Task 0
"$WEST_TWISTER" -T tests/sim_shell -p native_sim/native/64 -v --clobber-output
```
Expected: build PASS, all pytest PASS unchanged. If any JSON assertion changed, the refactor altered output — fix the renderer to match, do not edit the test.

- [ ] **Step 5: clang-format + commit**

Run:
```bash
clang-format-18 -i include/oe5xrx/module/iface.h tests/sim_shell/pytest/test_module_iface.py 2>/dev/null || clang-format -i include/oe5xrx/module/iface.h
git add include/oe5xrx/module/iface.h tests/sim_shell/pytest/test_module_iface.py
git commit -m "refactor(module): Result as std::variant<...,etl::string> (#45)

Replace the hand-rolled tagged union (VK enum + parallel scalar members +
self-referential s_/sbuf_ + manual copy-ctor/assignment) with
std::variant<monostate,int,double,bool,etl::string<15>>. Compiler-generated
copy is now correct; JSON output byte-identical. (clang-format note: .py not
formatted by clang.)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `parse_int` / `parse_float` → `etl::to_arithmetic` (C5)

**Files:**
- Modify: `subsys/module/devices/sa818/sa818_module.cpp` (`parse_float`, `parse_int`, includes)
- Test: `tests/sim_shell/pytest/test_module_iface.py` (add parse-edge characterization cases)

**Interfaces:**
- Consumes: nothing new.
- Produces: `parse_float(const char*) -> std::optional<float>` and `parse_int(const char*) -> std::optional<long>` with identical accept/reject semantics. `parse_bool` unchanged.

- [ ] **Step 1: Add parse-edge characterization tests (pin CURRENT behavior)**

Add to `tests/sim_shell/pytest/test_module_iface.py`. These assert the *current* `strtof/strtol` accept/reject set; they run first against unmodified code.

```python
def test_module_set_parse_edges(sa818_sim, shell):
    """Pin the accept/reject behavior of numeric parsing across the to_arithmetic swap."""
    shell.exec_command("sa818 power on")

    def result(cmd):
        return _payload(shell.exec_command(cmd), "MODULE-RESULT")

    # Trailing garbage -> rejected as bad_value (whole-string parse).
    assert result("module fm set frequency 145.5x")["error"] == "bad_value"
    # Empty-ish / non-numeric -> bad_value.
    assert result("module fm set volume abc")["error"] == "bad_value"
    # Valid integer within range -> ok.
    r = result("module fm set volume 5")
    assert r["ok"] is True and r["value"] == 5
    # Valid float within range -> ok.
    r = result("module fm set frequency 145.500")
    assert r["ok"] is True and r["value"] == 145.5
    # Out-of-range numeric still parses then range-rejects (not bad_value).
    assert result("module fm set volume 99")["error"] == "out_of_range"
```

- [ ] **Step 2: Run against unmodified code (baseline green)**

Run: `"$WEST_TWISTER" -T tests/sim_shell -p native_sim/native/64 -v --clobber-output`
Expected: PASS. Documents current accept/reject behavior.

- [ ] **Step 3: Swap the parsers to `etl::to_arithmetic`**

In `subsys/module/devices/sa818/sa818_module.cpp`, add includes (with the other includes):
```cpp
#include <etl/string_view.h>
#include <etl/to_arithmetic.h>
```

Replace `parse_float` and `parse_int` (keep `parse_bool` as-is):
```cpp
std::optional<float> parse_float(const char *s) {
  if (s == nullptr || *s == '\0') {
    return std::nullopt;
  }
  etl::to_arithmetic_result<float> r = etl::to_arithmetic<float>(etl::string_view(s));
  if (!r.has_value() || !isfinite(r.value())) {
    return std::nullopt;
  }
  return r.value();
}

std::optional<long> parse_int(const char *s) {
  if (s == nullptr || *s == '\0') {
    return std::nullopt;
  }
  etl::to_arithmetic_result<long> r = etl::to_arithmetic<long>(etl::string_view(s));
  if (!r.has_value()) {
    return std::nullopt;
  }
  return r.value();
}
```

- [ ] **Step 4: Build + run; reconcile any edge diff**

Run:
```bash
"$WEST_BUILD_NS"
"$WEST_TWISTER" -T tests/sim_shell -p native_sim/native/64 -v --clobber-output
```
Expected: all PASS unchanged. If a parse-edge test now differs (e.g. `to_arithmetic` accepts a leading `+` or surrounding whitespace that `strtof` handled differently, or vice-versa), preserve identical behavior by pre-checking in the parser (e.g. reject leading/trailing whitespace explicitly) rather than editing the test. Re-run until green.

- [ ] **Step 5: clang-format + commit**

Run:
```bash
clang-format -i subsys/module/devices/sa818/sa818_module.cpp
git add subsys/module/devices/sa818/sa818_module.cpp tests/sim_shell/pytest/test_module_iface.py
git commit -m "refactor(module): parse via etl::to_arithmetic (#45)

Replace strtol/strtof + end-pointer + isfinite with etl::to_arithmetic over
a string_view. Accept/reject behavior pinned identical by characterization
tests. parse_bool unchanged.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: `wav_source` buffer → `etl::vector` (C3)

**Files:**
- Modify: `app/src/sim_audio/wav_source.h`, `app/src/sim_audio/wav_source.cpp`
- Test: `tests/sim_shell/pytest/test_sim_shell.py` (existing `wav load`/`info` = characterization)

**Interfaces:**
- Consumes: `sim_audio::wav_max_samples` (constants.h, unchanged).
- Produces: `WavSource` with identical public API (`load`, `loaded`, `sample_rate_hz`, `next_sample_norm`, `pos_samples`, `count_samples`, `samples`).

- [ ] **Step 1: Confirm existing wav coverage is green (baseline)**

Run: `"$WEST_TWISTER" -T tests/sim_shell -p native_sim/native/64 -v --clobber-output`
Expected: PASS (`test_sim_shell.py` drives `wav load <file>` + `wav info` with the sample count, and `wav start`/`stop`). This is the C3 characterization.

- [ ] **Step 2: Change the member type in `wav_source.h`**

Replace the include and member. In `app/src/sim_audio/wav_source.h`:
- Replace `#include <array>` with `#include <etl/vector.h>`.
- Replace the private members block:
```cpp
private:
  std::array<int16_t, sim_audio::wav_max_samples> buf_{};
  std::size_t count_samples_{0};
  std::size_t idx_samples_{0};
  uint32_t sample_rate_hz_{0};
```
with:
```cpp
private:
  etl::vector<int16_t, sim_audio::wav_max_samples> buf_{};
  std::size_t idx_samples_{0};
  uint32_t sample_rate_hz_{0};
```
- Update the inline accessors that referenced `count_samples_`:
```cpp
  [[nodiscard]] bool loaded() const noexcept { return (buf_.size() > 0u) && (sample_rate_hz_ > 0u); }
  [[nodiscard]] std::size_t count_samples() const noexcept { return buf_.size(); }
  [[nodiscard]] std::span<const int16_t> samples() const noexcept { return std::span{buf_.data(), buf_.size()}; }
```
(`pos_samples()` still returns `idx_samples_`.)

- [ ] **Step 3: Update `wav_source.cpp` fill + reset + wrap logic**

In `parse_wav_into_buffer`, replace the indexed fill:
```cpp
  const std::size_t available_samples = static_cast<std::size_t>(data_bytes / 2u);
  const std::size_t max_samples = buf_.max_size();
  const std::size_t samples_to_read = (available_samples > max_samples) ? max_samples : available_samples;

  buf_.clear();
  for (std::size_t i = 0; i < samples_to_read; i++) {
    uint8_t b[2]{};
    rc = read_exact(fd, b, sizeof(b));
    if (rc)
      return rc;
    buf_.push_back(static_cast<int16_t>(rd_u16_le(b)));
  }

  idx_samples_ = 0;
  sample_rate_hz_ = sample_rate_hz;
  return 0;
```
(The `min(available, max_size)` clamp stays — `etl::vector` overflow panics, so this guard is mandatory.)

In `load`, replace the error-reset:
```cpp
  if (rc) {
    buf_.clear();
    idx_samples_ = 0;
    sample_rate_hz_ = 0;
  }
```

In `next_sample_norm`, replace `count_samples_` with `buf_.size()`:
```cpp
float WavSource::next_sample_norm() {
  if (!loaded() || buf_.size() == 0u)
    return 0.0f;

  const int16_t s = buf_[idx_samples_++];
  if (idx_samples_ >= buf_.size())
    idx_samples_ = 0;

  // Convert to [-1, +1). Use 32768 to map -32768 -> -1.0 exactly.
  return static_cast<float>(s) / 32768.0f;
}
```

- [ ] **Step 4: Build + run (green, unchanged)**

Run:
```bash
"$WEST_BUILD_NS"
"$WEST_TWISTER" -T tests/sim_shell -p native_sim/native/64 -v --clobber-output
```
Expected: build PASS, `test_sim_shell.py` wav tests PASS unchanged (same reported sample count).

- [ ] **Step 5: clang-format + commit**

Run:
```bash
clang-format -i app/src/sim_audio/wav_source.h app/src/sim_audio/wav_source.cpp
git add app/src/sim_audio/wav_source.h app/src/sim_audio/wav_source.cpp
git commit -m "refactor(sim_audio): wav buffer as etl::vector (#45)

Replace std::array + separate count_samples_ with etl::vector<int16_t,N>;
.size() is the count. Capacity clamp retained (etl::vector overflow panics).
Behavior identical.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: `sim_audio` callbacks → `etl::delegate` (C4)

**Files:**
- Modify: `app/src/sim_audio/sample_clock.h`, `app/src/sim_audio/sample_clock.cpp`, `app/src/sim_audio/audio_pipeline.h`, `app/src/sim_audio/audio_pipeline.cpp`
- Test: `tests/sim_shell/pytest/test_sim_shell.py` (existing `wav sine`/`start`/`info running` = characterization)

**Interfaces:**
- Consumes: nothing new.
- Produces: `SampleClock::start(uint32_t rate_hz, etl::delegate<void()> fn)`, `SampleClock::stop()`, `running()`, `rate_hz()`. `AudioPipeline` unchanged public API (`start(SampleSource&)`, `stop()`, `running()`, `source()`).

- [ ] **Step 1: Confirm existing timer-driven coverage is green (baseline)**

Run: `"$WEST_TWISTER" -T tests/sim_shell -p native_sim/native/64 -v --clobber-output`
Expected: PASS. `test_sim_shell.py` runs `wav sine ...` then asserts `wav info` shows `running=1`, and `wav stop`. This proves the timer fires and drives the pipeline — the C4 characterization.

- [ ] **Step 2: Convert `SampleClock` to hold an `etl::delegate`**

In `app/src/sim_audio/sample_clock.h`:
- Replace `#include <cstdint>` block additions with `#include <etl/delegate.h>` (keep `<cstdint>`, `<zephyr/kernel.h>`).
- Replace the `tick_fn_t` typedef and members:
```cpp
class SampleClock {
public:
  SampleClock();

  void start(uint32_t rate_hz, etl::delegate<void()> fn);
  void stop();

  bool running() const { return running_; }
  uint32_t rate_hz() const { return rate_hz_; }

private:
  static void timer_trampoline(k_timer *t);

  k_timer timer_;
  bool running_{false};
  uint32_t rate_hz_{0};

  etl::delegate<void()> fn_{};
};
```

In `app/src/sim_audio/sample_clock.cpp`:
```cpp
void SampleClock::start(uint32_t rate_hz, etl::delegate<void()> fn) {
  if (rate_hz == 0u || !fn.is_valid())
    return;

  rate_hz_ = rate_hz;
  fn_ = fn;

  const uint64_t period_ns = ns_per_s / static_cast<uint64_t>(rate_hz_);
  k_timer_start(&timer_, K_NSEC(period_ns), K_NSEC(period_ns));
  running_ = true;
}
```
```cpp
void SampleClock::timer_trampoline(k_timer *t) {
  auto *self = static_cast<SampleClock *>(k_timer_user_data_get(t));
  if (self == nullptr)
    return;
  self->fn_.call_if(); // invokes only when bound (replaces the null-fn guard)
}
```
(`SampleClock()` ctor and `stop()` are unchanged. The `void* user_` member is gone.)

- [ ] **Step 3: Bind `AudioPipeline::on_tick` as a member delegate**

In `app/src/sim_audio/audio_pipeline.h`, change `on_tick` from a static `void*` trampoline to a member:
```cpp
private:
  void on_tick();

  AdcSink sink_;
  SampleClock clock_;
  SampleSource *src_{nullptr};
  bool running_{false};
```

In `app/src/sim_audio/audio_pipeline.cpp`:
```cpp
#include <etl/delegate.h>

AudioPipeline::AudioPipeline(AdcSink sink) : sink_(sink) {}

int AudioPipeline::start(SampleSource &src) {
  if (!sink_.ready())
    return sim_audio::err_nodev;

  src_ = &src;
  running_ = true;

  clock_.start(src.sample_rate_hz(), etl::delegate<void()>::create<AudioPipeline, &AudioPipeline::on_tick>(*this));
  return 0;
}

void AudioPipeline::stop() {
  clock_.stop();
  running_ = false;
  src_ = nullptr;
  sink_.write_norm(0.0f);
}

void AudioPipeline::on_tick() {
  if (!running_ || src_ == nullptr)
    return;

  const float s_norm = src_->next_sample_norm();
  sink_.write_norm(s_norm);
}
```

- [ ] **Step 4: Build + run (green, unchanged)**

Run:
```bash
"$WEST_BUILD_NS"
"$WEST_TWISTER" -T tests/sim_shell -p native_sim/native/64 -v --clobber-output
```
Expected: build PASS; `wav sine`/`start`/`info running=1`/`stop` PASS unchanged (timer still fires).

- [ ] **Step 5: clang-format + commit**

Run:
```bash
clang-format -i app/src/sim_audio/sample_clock.h app/src/sim_audio/sample_clock.cpp app/src/sim_audio/audio_pipeline.h app/src/sim_audio/audio_pipeline.cpp
git add app/src/sim_audio/sample_clock.h app/src/sim_audio/sample_clock.cpp app/src/sim_audio/audio_pipeline.h app/src/sim_audio/audio_pipeline.cpp
git commit -m "refactor(sim_audio): timer callback via etl::delegate (#45)

Replace raw void(*)(void*) + void* user_data with etl::delegate<void()>.
AudioPipeline::on_tick becomes a bound member (no void* trampoline);
SampleClock uses call_if as the bound-check. Behavior identical.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Full verification + PR

**Files:** none (verification + PR).

**Interfaces:** none.

- [ ] **Step 1: clang-format guard — no diff**

Run:
```bash
clang-format --dry-run --Werror $(git diff --name-only origin/main -- '*.cpp' '*.h' '*.hpp' | grep -vE '^tests/.*\.py$') || echo "FORMAT DIFF"
```
Expected: no output / no error. Fix any diff and amend the relevant commit.

- [ ] **Step 2: Both board builds green**

Run:
```bash
"$WEST_BUILD_NS"          # native_sim
"$WEST_BUILD_FM"          # fm_board (from Task 0)
```
Expected: both PASS. fm_board proves the changes compile for the real STM32U575 target (ETL on Cortex-M), not just native_sim.

- [ ] **Step 3: Full test matrix green**

Run:
```bash
"$WEST_TWISTER" -T app --integration -p native_sim/native/64 -v --clobber-output
"$WEST_TWISTER" -T tests/sim_shell -p native_sim/native/64 -v --clobber-output
"$WEST_TWISTER" -T tests/etl -p native_sim/native/64 -v --clobber-output
```
Expected: all PASS. Confirm the sim_shell diff vs origin/main added only new test functions (no edits to existing assertions): `git diff origin/main -- tests/sim_shell/pytest/`.

- [ ] **Step 4: Driver-untouched guard**

Run:
```bash
git diff --name-only origin/main -- drivers/ | grep . && echo "DRIVER TOUCHED - ABORT" || echo "drivers clean"
```
Expected: `drivers clean`. Drivers must be unmodified.

- [ ] **Step 5: Push + open PR**

Run:
```bash
git push -u origin feature/etl-retrofit-nondriver
```
Then open a PR against `main` titled `refactor: ETL retrofit of non-driver code` with body describing C1–C5, the identical-behavior proof (sim_shell green with no assertion edits), both-board builds, and ending with `Closes #45`. After the PR is open, run the copilot-loop and work through findings.

---

## Self-Review

**Spec coverage:**
- C1 Result→variant → Task 1 ✓
- C2 tone handling (flows through new Result; `char[16]` driver buffer stays) → covered by Task 1 (no separate code change; tone round-trip tests already green) ✓
- C3 wav→etl::vector → Task 3 ✓
- C4 sim_audio→etl::delegate → Task 4 ✓
- C5 parse→to_arithmetic → Task 2 ✓
- Out-of-scope (JsonWriter, numStr, registry, USB C-ABI, std::span/array) → not touched by any task ✓
- Build/test topology prerequisite → Task 0 ✓
- DoD (identical behavior, both builds, CI, one PR Closes #45) → Task 5 ✓

**Placeholder scan:** No TBD/TODO; every code step shows full code; commands have expected output. ✓

**Type consistency:** `Result` public API identical across Task 1 and its `sa818_module.cpp` callers (unchanged); `SampleClock::start` new signature (Task 4) is the only interface change and both its caller (`AudioPipeline::start`) and definition are updated in the same task; `buf_.size()`/`buf_.max_size()` used consistently in Task 3; `to_arithmetic_result::has_value()/value()` per verified ETL API in Task 2. ✓
