# FW Coding-Standard Docs Rewrite — Design Spec

**Ticket:** Canonical FW coding standard — full CLAUDE.md rewrite + CONTRIBUTING.md update
**Branch:** `docs/fw-standards-rewrite` (worktree off `origin/main`)
**Date:** 2026-07-03
**Supersedes:** open PR #42 (`docs/fw-claude-wrapper-precision`) — its nuance is folded in here; #42 is closed after this merges.

## Goal

Establish the **canonical firmware coding standard** for FW-RemoteStation across three
documents, and give agents substantially more FW-implementation knowledge — all derived
from this repo, self-contained, with **no references to other repos**.

The rewrite pins down a policy that the current docs only gesture at: a **two-tier
language split** (Zephyr-C-idiomatic drivers vs. modern C++20 + ETL everywhere else),
plus a concrete ETL usage guide including error-handler behavior.

## Scope

Three files change:

| File | Language | Role | Authority |
|------|----------|------|-----------|
| **CONTRIBUTING.md** | English | Canonical, complete contributor standard — single source of truth for the coding policy | Wins on any conflict |
| **CLAUDE.md** | English | Self-contained agent doc: policy summary + FW-implementation orientation | Must never contradict CONTRIBUTING.md |
| **README.md** | German | Public entry point: what/why, targets, build/test quickstart | Factual; points to CONTRIBUTING for the standard |

Out of scope: no source-code changes, no test changes, no CI changes. Docs only.

## Constraints (binding, from repo reality)

- No dynamic allocation (`new`/`delete`, `malloc`/`free`, `std::vector`, `std::string`, …).
- No exceptions, no RTTI. Error handling via return/status types.
- Two-tier language policy (see below).
- `build_and_tests` **and** `clang_format` CI jobs must stay green (docs-only, so trivially true).

## The core policy (identical intent in CLAUDE.md ↔ CONTRIBUTING.md)

### Two-tier language split

- **Driver layer (`drivers/`) = C-first Zephyr idiom.** Device model (`struct device`, API
  structs), Devicetree, `extern "C"` public headers, result enums. No C++ compulsion; no
  vtables/inheritance forced into the driver ABI. Modern C++ *inside* a driver translation
  unit is fine; the *interface* stays C-idiomatic.
  - Reconciliation with reality: the SA818 driver TUs are `.cpp` but expose a **pure C ABI**
    (`extern "C"` headers, `enum sa818_result`, `[[nodiscard]]` free functions over
    `const struct device *`). The `.cpp` extension is an implementation detail; a new driver
    may be plain C. The rule is about the **interface idiom**, not the file extension.
- **Non-driver code (app / module layer / USB / sim) = modern C++20 + ETL.** OOP, RAII,
  `constexpr`, namespaces, value-types; inheritance **only where it carries a real
  abstraction interface**. The `mod::Capability` mixin hierarchy
  (`Setting`/`Action`/`Telemetry`) in `include/oe5xrx/module/iface.h` is the exemplar.

### Hard rules (everywhere, both tiers)

- **No dynamic allocation.** Forbidden: `new`/`delete`, `malloc`/`free`, `std::vector`,
  `std::string`, and any type that may allocate internally (`std::list`, `std::map`,
  `std::function`, …). Use static/stack: `std::array`, `std::span`, `std::string_view`,
  `std::optional`, `constexpr`, fixed buffers, value-types — and ETL fixed-capacity
  containers.
- **No exceptions, no RTTI.** No `dynamic_cast`. Code must be correct without these.

### ETL usage guide (the "how")

ETL is integrated (west-pinned `etl` @ `20.48.0`, `CONFIG_ETL=y`). It is the no-alloc,
fixed-capacity replacement for the forbidden heap `std::` containers:

- `etl::string<N>` instead of `std::string`
- `etl::vector<T, N>` instead of `std::vector`
- `etl::map<K, V, N>` (and friends) instead of `std::map`
- `std::string_view` for non-owning views
- `std::optional` / `std::variant` deliberately — **only the wrapper is stack-allocated;
  the contained type must itself be heap-free** (the no-alloc rule demands it anyway).
  This is PR #42's precision, preserved verbatim in intent.

**Error-handler behavior (must document):** exceptions are disabled and
`CONFIG_ETL_LOG_ERRORS=y`, so a failed ETL check (e.g. writing past an `etl::string<N>`
capacity) does **not** throw and does **not** silently truncate — it invokes the registered
`etl::error_handler` callback (`app/src/etl_error_handler.cpp`), which does `printk` then
`k_panic()`. The handler is armed at `SYS_INIT(PRE_KERNEL_1, 0)` so it is active before
essentially all other init. **Contributor consequence: an ETL capacity overflow is a
hard fault, not a silent bug — size capacities correctly.**

### clang-format

`clang-format` (clang-format-18) is mandatory and CI-enforced over `app/`, `boards/`,
`tests/`. Config: LLVM base, 2-space indent, 160 column limit, C++ latest. Format before
commit.

## FW-implementation knowledge to capture (CLAUDE.md, derived from repo)

### Architecture

- **Module layer** (`include/oe5xrx/module/iface.h`, `subsys/module/`): a device-agnostic
  capability framework — the firmware half of the Firmware↔Agent contract. A `Module` is
  identity + a fixed registry of `Capability` objects; each capability owns its typed
  `FieldSpec` descriptor **and** its set/get/do behavior. Kind mixins `Setting` / `Action`
  / `Telemetry` fix the op↔kind gating (`set`/`get`/`do`). The module renders `describe`
  (machine-readable JSON via the bounded `JsonWriter`) and dispatches `execute`. A `Result`
  carries a typed value or an error code. Adding a capability = one subclass + one registry
  entry; a new module type reuses the framework unchanged. Header has **no device/RTOS
  dependencies** — concrete capabilities live in the driver/subsys TU
  (`subsys/module/devices/sa818/sa818_module.cpp`).
- **SA818 driver** (`drivers/radio/sa818/`): core / at / audio / audio_stream / shell TUs;
  public headers under `drivers/radio/sa818/sa818/`. C-first ABI: `enum sa818_result`,
  `[[nodiscard]]` functions, `struct sa818_status`, DT-configured (`sa,sa818.yaml` binding,
  `band` property selects VHF/UHF).
- **USB composite** (`app/src/usb_config.cpp`, board `USB_CONFIGURATION.md`): built on
  `CONFIG_USB_DEVICE_STACK_NEXT` — UAC2 (audio streaming), CDC-ACM (shell/console), DFU
  (firmware update). VID `0x2FE3`.
- **Firmware role: thin & self-describing.** The FW **describes itself** (identity +
  capabilities, machine-readable) and **executes commands** — nothing more. No platform
  knowledge, no capability persistence, no access/rights model in the firmware; that logic
  lives outside (agent/server). New FW code must preserve this boundary.
  - The SA818 `Sa818Context` RAM shadow is **working state only** (lets `set frequency`
    rebuild the full `sa818_at_set_group()` call), explicitly **not** capability persistence.

### Board / Devicetree

- **fm_board = STM32U575** (`boards/oe5xrx/fm_board/`, `board.yml` soc `stm32u575xx`);
  custom Zephyr board, MPU + HW stack protection on.
- **native_sim/native/64** = host simulation target; primary dev + CI + systemtest target.
- DT bindings under `dts/bindings/` (`radio/sa,sa818.yaml`, `dac/wav-dac.yaml`).

### Build & test workflow

- Build: `west build -b fm_board app` / `west build -b native_sim/native/64 app`.
- Twister targets (all CI-gated except unit tests currently commented out):
  - `west twister -T app --integration`
  - `west twister -T tests/sim_shell -p native_sim/native/64`
  - `west twister -T tests/etl -p native_sim/native/64`
- pytest system tests under `tests/sim_shell/pytest/` drive the shell over stdin/stdout; a
  Python `SA818Simulator` emulates the AT protocol so the same tests validate module/driver
  end-to-end without hardware.
- **CI gates (`.github/workflows/ci.yml`): `clang_format` AND `build_and_tests` must be
  green.** CI uses official Zephyr GitHub Actions (not the devcontainer) to stay free/open.

### Test philosophy

- **Simulation == Hardware.** New functionality is implemented and tested on `native_sim`
  first; hardware-specific code stays cleanly encapsulated. Contributions must not regress
  existing tests, and code that is only testable on real hardware is a non-goal.

### Common pitfalls (CLAUDE.md "Stolpersteine")

- clang-format-18 is mandatory — a format mismatch fails CI.
- An ETL capacity overflow **panics** (fail-fast), it does not truncate.
- Driver result codes are `[[nodiscard]]` — don't ignore them.
- DT `band` property selects VHF vs UHF at build time (model/default frequency/ranges
  follow from it).
- Keep platform/access/persistence logic **out** of the firmware (thin & self-describing).
- Real test dirs are `tests/etl`, `tests/sim_shell`, `tests/usb_audio` — there is no
  `tests/unit_audio` (README previously referenced it in error).

## Corrections baked in (repo consistency)

- STM32F302 → **STM32U575** (README).
- Non-existent `tests/unit_audio` → real `tests/etl` / `tests/sim_shell` / `tests/usb_audio`.
- "reines C++ ohne Heap" framing → the accurate two-tier C / C++20+ETL policy.

## File outlines

### CONTRIBUTING.md (English, canonical/full)
1. Intro
2. License (LGPL-3.0-or-later; contributor terms) — preserved
3. Project philosophy — preserved, incl. "simulation is equal to hardware"
4. **Coding standard** — full two-tier policy; memory/exceptions/RTTI hard rules; ETL guide
   with examples + error-handler behavior; clang-format
5. Architecture overview — module layer, driver idiom, USB composite, thin-firmware boundary
6. Simulation vs. hardware
7. Build & test workflow — exact commands, real test dirs, CI gates, devcontainer vs CI
8. Commits & pull requests
9. Non-goals
10. Questions & discussion

### CLAUDE.md (English, self-contained, no cross-repo links)
1. One-line intro (Zephyr FW, SA818, STM32U575 USB bridge)
2. Language & coding standard — two-tier split + hard rules + ETL how-to (summary)
3. Architecture orientation — module layer, SA818 driver, USB composite, thin/self-describing role
4. Build & test — west build, Twister targets, pytest, CI gates
5. Board / DT — fm_board=U575, native_sim, bindings location
6. Test philosophy — Simulation == Hardware; AT simulator
7. Pitfalls (Stolpersteine)

### README.md (German, full rewrite)
Badges · Was es ist · Ziele · Targets (**fm_board = STM32U575**, native_sim) · Struktur ·
Abhängigkeiten · Setup/Build/Test-Quickstart · Simulation-Features · Design-Prinzipien
(korrekte Sprach-Policy) · Lizenz · Status · Verweis auf CONTRIBUTING für den Standard.

## Consistency requirements (verification gate)

Before "done":
- CLAUDE.md, CONTRIBUTING.md, README.md agree on: target SoC (U575), language policy
  (two-tier), test dirs, ETL rules, CI gates.
- CLAUDE.md contains nothing that contradicts CONTRIBUTING.md (CONTRIBUTING wins).
- No references to other OE5XRX repos in any of the three files.
- Every factual claim traceable to a repo file (board.yml, prj.conf, ci.yml, west.yml,
  iface.h, etl_error_handler.cpp, etc.).

## Process

Spec committed on `docs/fw-standards-rewrite` → subagent-driven section execution →
verification-before-completion (the consistency gate above) → single PR against `main` →
copilot-loop → close PR #42. No TDD (docs only).
