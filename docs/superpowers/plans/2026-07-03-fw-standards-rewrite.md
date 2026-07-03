# FW Coding-Standard Docs Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the canonical FW coding standard across CONTRIBUTING.md (English, full), CLAUDE.md (English, agent-focused), and README.md (German, rewritten), with a two-tier language policy and expanded FW-implementation knowledge for agents.

**Architecture:** Three documents, each a distinct role. CONTRIBUTING.md is the single source of truth; CLAUDE.md is a self-contained agent summary + FW-impl orientation that never contradicts it; README.md is the public German entry point. All facts trace to repo files. No cross-repo references. Docs-only — no code/test/CI changes.

**Tech Stack:** Markdown. Content derived from: `boards/oe5xrx/fm_board/board.yml`, `app/prj.conf`, `app/CMakeLists.txt`, `west.yml`, `.github/workflows/ci.yml`, `.clang-format`, `include/oe5xrx/module/iface.h`, `subsys/module/devices/sa818/sa818_module.cpp`, `drivers/radio/sa818/sa818/sa818.h`, `app/src/etl_error_handler.cpp`, `app/src/usb_config.cpp`, `tests/`.

## Global Constraints

- **fm_board SoC** = `stm32u575xx` (STM32U575), verbatim from `board.yml`. Never STM32F302.
- **Two-tier language policy:** `drivers/` = C-first Zephyr idiom (device model, DT, `extern "C"` headers, result enums; no forced C++/vtables in the ABI); non-driver (app/module-layer/USB/sim) = modern C++20 + ETL.
- **Hard rules everywhere:** no dynamic allocation (`new`/`delete`, `malloc`/`free`, `std::vector`, `std::string`, `std::function`, `std::list`, `std::map`…); no exceptions; no RTTI.
- **ETL:** west-pinned `etl` @ `20.48.0`; `CONFIG_ETL=y`, `CONFIG_ETL_LOG_ERRORS=y`. `etl::string<N>` / `etl::vector<T,N>` / `etl::map<K,V,N>` replace heap `std::`; `std::string_view` for views; `std::optional`/`std::variant` deliberate — **only the wrapper is stack; contained type must also be heap-free** (PR #42). ETL overflow → registered handler → `printk` + `k_panic()` (fail-fast, armed `SYS_INIT(PRE_KERNEL_1, 0)`); NOT silent truncation.
- **CI gates:** `clang_format` (clang-format-18 over `app/`,`boards/`,`tests/`) AND `build_and_tests` must be green.
- **Real test dirs:** `tests/etl`, `tests/sim_shell`, `tests/usb_audio`. No `tests/unit_audio`.
- **Language:** CONTRIBUTING.md + CLAUDE.md English; README.md German.
- **No references to other OE5XRX repos** in any of the three files.

---

### Task 1: CONTRIBUTING.md — canonical English standard

**Files:**
- Modify (full rewrite): `CONTRIBUTING.md`

**Interfaces:**
- Produces: the authoritative wording for the two-tier policy, hard rules, ETL guide, build/test commands, CI gates, architecture overview. Tasks 2 and 3 must not contradict this file.

- [ ] **Step 1: Rewrite `CONTRIBUTING.md`** with these sections, in English:

  1. **Intro** — one paragraph: contributing to FW-RemoteStation, values openness/testability/maintainability.
  2. **License** — LGPL-3.0-or-later; by contributing you agree it ships under LGPL-3.0-or-later, you have the right to submit, no incompatibly-licensed parts. (Preserve current intent.)
  3. **Project philosophy** — open source first; commercial use allowed; proprietary forks of the code undesired; improvements benefit the community; **simulation and tests are equal to hardware**.
  4. **Coding standard** (the core — full detail):
     - **Two-tier language split.** Driver layer (`drivers/`) = C-first Zephyr idiom: device model (`struct device`, API structs), Devicetree, `extern "C"` public headers, result enums (`enum sa818_result`), `[[nodiscard]]` free functions. No C++ compulsion; no vtables/inheritance forced into the driver ABI; modern C++ *inside* a driver TU is fine but the *interface* stays C-idiomatic. Note: the SA818 driver TUs are `.cpp` yet expose a pure C ABI — extension is an implementation detail; a new driver may be plain C. Non-driver code (app / module layer / USB / sim) = modern C++20 + ETL: OOP, RAII, `constexpr`, namespaces, value-types; inheritance only where it carries a real abstraction interface (the `mod::Capability` mixins `Setting`/`Action`/`Telemetry` are the exemplar).
     - **Memory: no dynamic allocation.** Forbidden list + "any type that may allocate internally". Allowed: `std::array`, `std::span`, `std::string_view`, `std::optional`, `constexpr`, fixed buffers, value-types, ETL containers.
     - **No exceptions, no RTTI.** No `dynamic_cast`; error handling via return/status types.
     - **ETL usage guide** with the mapping table (`etl::string<N>`↔`std::string`, `etl::vector<T,N>`↔`std::vector`, `etl::map<K,V,N>`↔`std::map`), the `std::string_view`/`std::optional`/`std::variant` guidance incl. the wrapper-vs-contained precision, and the **error-handler behavior** paragraph (overflow → `printk`+`k_panic()`, armed `PRE_KERNEL_1`; "overflow is a hard fault, size capacities correctly"). Reference `app/src/etl_error_handler.cpp` and the `CONFIG_ETL_LOG_ERRORS` setting.
     - **clang-format** — mandatory, CI-enforced (clang-format-18) over `app/`,`boards/`,`tests/`; LLVM base, 2-space indent, 160 cols; format before commit.
     - **No magic numbers** (`constexpr`), speaking identifiers, simple traceable abstractions.
  5. **Architecture overview** — module layer (generic `describe` + typed set/get/do, `include/oe5xrx/module/iface.h`, concrete caps in `subsys/module/devices/…`); SA818 driver (`drivers/radio/sa818/`); USB composite UAC2+CDC-ACM+DFU on `USB_DEVICE_STACK_NEXT`; **thin & self-describing firmware boundary** — no platform knowledge / capability persistence / access model in FW.
  6. **Simulation vs. hardware** — implement/test on `native_sim` first; hardware code cleanly encapsulated.
  7. **Build & test workflow** — exact commands:
     ```
     west build -b fm_board app
     west build -b native_sim/native/64 app
     west twister -T app --integration -v
     west twister -T tests/sim_shell -p native_sim/native/64 -v
     west twister -T tests/etl -p native_sim/native/64 -v
     ```
     Real test dirs only. CI gates (`clang_format` AND `build_and_tests`). Devcontainer vs CI (CI uses official Zephyr GH Actions, not the devcontainer, to stay free/open).
  8. **Commits & pull requests** — small logically-separate commits; PRs describe what/why and name/add tests.
  9. **Non-goals** — proprietary/closed deps; unnecessary complexity; changes without a test strategy; code only testable on real hardware.
  10. **Questions & discussion** — open an issue / discuss before large rebuilds.

- [ ] **Step 2: Self-verify** — read `CONTRIBUTING.md` and confirm: English throughout; every Global Constraint value present and correct; no cross-repo references; no `tests/unit_audio`; no "STM32F302"; no multi-line `{# #}`-style or broken code fences.

  Run: `grep -nE "STM32F302|unit_audio|Watchtower" CONTRIBUTING.md` → Expected: no output.

- [ ] **Step 3: Commit**

  ```bash
  git add CONTRIBUTING.md
  git commit -m "docs(CONTRIBUTING): canonical English FW coding standard"
  ```

---

### Task 2: CLAUDE.md — self-contained English agent doc

**Files:**
- Modify (full rewrite): `CLAUDE.md`

**Interfaces:**
- Consumes: the policy wording locked in Task 1 (must match, condensed).
- Produces: agent-facing summary + FW-impl orientation. Must not contradict `CONTRIBUTING.md`.

- [ ] **Step 1: Rewrite `CLAUDE.md`** with these sections, in English, self-contained (no links to other repos):

  1. **Intro** — one line: Zephyr-RTOS firmware for the OE5XRX Remote Station's STM32 part — SA818 FM-transceiver module, STM32U575 as USB bridge (UAC2 + CDC-ACM + DFU).
  2. **Language & coding standard** — condensed two-tier split (drivers = C-first Zephyr idiom; non-driver = modern C++20 + ETL); hard rules (no alloc / no exceptions / no RTTI); ETL how-to bullets incl. the wrapper-vs-contained precision and the "overflow panics, not truncates" consequence. One line: "CONTRIBUTING.md is the canonical standard; this is a summary and never overrides it."
  3. **Architecture orientation** — module layer (device-agnostic `Capability` framework: `Setting`/`Action`/`Telemetry`, `FieldSpec` descriptor, `JsonWriter`, `Result`; renders `describe` JSON + dispatches `execute`; add a capability = one subclass + one registry entry; header has no device/RTOS deps); SA818 driver (`drivers/radio/sa818/` core/at/audio/shell, headers under `sa818/sa818/`, C ABI + `[[nodiscard]]`, DT `band` selects VHF/UHF); USB composite; **thin & self-describing firmware role** (describes itself + executes commands; no platform/persistence/access logic; the `Sa818Context` shadow is working state, not persistence).
  4. **Build & test** — `west build -b fm_board app` / `-b native_sim/native/64 app`; Twister targets (`app --integration`, `tests/sim_shell`, `tests/etl` on `native_sim/native/64`); pytest under `tests/sim_shell/pytest/` drives the shell via stdin/stdout with a Python `SA818Simulator` for the AT protocol; CI gates `clang_format` AND `build_and_tests`.
  5. **Board / DT** — fm_board = STM32U575 (custom board, MPU + HW stack protection); native_sim/native/64 = host sim (primary dev/CI/systemtest); DT bindings under `dts/bindings/`.
  6. **Test philosophy** — Simulation == Hardware; new code implemented/tested on `native_sim` first; no regressions; hardware-only-testable code is a non-goal.
  7. **Pitfalls (Stolpersteine)** — clang-format-18 mandatory; ETL overflow panics (fail-fast); driver result codes are `[[nodiscard]]`; DT `band` sets model/default freq/ranges at build time; keep platform/access/persistence out of FW; real test dirs are `tests/etl`/`tests/sim_shell`/`tests/usb_audio` (no `tests/unit_audio`).

- [ ] **Step 2: Self-verify** — read `CLAUDE.md` and confirm: English; agrees with `CONTRIBUTING.md` on SoC / policy / test dirs / ETL / CI gates; no contradictions; no cross-repo links; self-contained.

  Run: `grep -nE "STM32F302|unit_audio" CLAUDE.md` → Expected: no output.

- [ ] **Step 3: Cross-check against Task 1** — diff the two policy summaries mentally: same SoC, same two-tier wording, same ETL rules, same CI gates. Fix any drift in `CLAUDE.md` (CONTRIBUTING wins).

- [ ] **Step 4: Commit**

  ```bash
  git add CLAUDE.md
  git commit -m "docs(CLAUDE): self-contained English agent standard + FW-impl orientation"
  ```

---

### Task 3: README.md — German public entry point (full rewrite)

**Files:**
- Modify (full rewrite): `README.md`

**Interfaces:**
- Consumes: canonical facts (Tasks 1/2). Must not contradict them.
- Produces: public German landing doc pointing to CONTRIBUTING for the standard.

- [ ] **Step 1: Rewrite `README.md`** in German with:

  - **Badges** — CI, License LGPLv3, Zephyr RTOS. (Drop or keep the C++ badge only if accurate; prefer replacing with a neutral "Zephyr RTOS" framing to avoid the "pure C++" implication.)
  - **Was es ist** — Zephyr-RTOS-Firmware für das FM-Transceiver-Board (`fm_board`); `native_sim` für frühe automatisierte Tests von Logik/Audio/Protokoll.
  - **Ziele** — Trennung HW-abhängiger Code / Logik; High-Level-Tests via Simulation; reproduzierbare Builds/Tests mit Twister; C++20-Nicht-Treiber-Code ohne Heap + Zephyr-C-Treiber; Offenheit/Wartbarkeit/Langzeitbetrieb.
  - **Targets** — `fm_board` (**STM32U575**-basiertes FM-Transceiver-Board); `native_sim/native/64` (Host-Simulation, Linux).
  - **Struktur** — accurate tree: `app/` (C++ Firmware + `sim_audio`), `boards/oe5xrx/fm_board/`, `drivers/radio/sa818/`, `subsys/module/`, `include/oe5xrx/module/`, `tests/{sim_shell,etl,usb_audio}`, `dts/bindings/`, `.github/workflows/`, `west.yml`.
  - **Abhängigkeiten** — Zephyr (via west), Python ≥3.10 (Twister/pytest), Zephyr SDK / Arm toolchain, Linux host für native_sim.
  - **Setup** — `west init -m https://github.com/OE5XRX/FW-RemoteStation` / `west update` / `west zephyr-export`.
  - **Build** — fm_board + native_sim commands; `./build/zephyr/zephyr.exe`.
  - **Simulation-Features** — UART-Shell über stdin/stdout, ADC-Emulation, EEPROM-Sim, WAV-Playback, Sinusgenerator; beispiel `fm> wav sine 1000 1.0 8000`.
  - **Tests** — Twister integration + `tests/sim_shell` + `tests/etl` (echte Dirs, kein `unit_audio`).
  - **Design-Prinzipien** — **korrekte Sprach-Policy**: Zephyr-C-Treiber + C++20-ohne-Heap für Nicht-Treiber-Code; Simulation gleichwertig zur Hardware; Testbarkeit vor Optimierung; explizite Abhängigkeiten.
  - **Lizenz** — LGPL-3.0-or-later.
  - **Status** — aktiv in Entwicklung; Fokus Simulation/Tests/CI.
  - **Mitarbeit** — Verweis auf `CONTRIBUTING.md` als verbindlichen Standard. (Merge the old "Developer Notes"/devcontainer content here or drop the duplicated tail so there is one coherent doc.)

- [ ] **Step 2: Self-verify** — read `README.md`: German; `STM32U575` (never F302); real test dirs; no "pure C++ ohne Heap" as the blanket policy (must reflect the two-tier split); points to CONTRIBUTING; no broken fences (the old file had a literal `--- CODE BLOCK START ---` artifact — must be gone).

  Run: `grep -nE "STM32F302|unit_audio|CODE BLOCK START" README.md` → Expected: no output.

- [ ] **Step 3: Commit**

  ```bash
  git add README.md
  git commit -m "docs(README): rewrite — STM32U575, accurate two-tier policy, real test dirs"
  ```

---

### Task 4: Consistency & verification pass

**Files:**
- Possibly touch any of `CLAUDE.md`, `CONTRIBUTING.md`, `README.md` for fixes.

**Interfaces:**
- Consumes: all three finished files.
- Produces: verified, mutually-consistent doc set ready for PR.

- [ ] **Step 1: Cross-file consistency check.** Confirm all three agree on: SoC (`STM32U575`), two-tier language policy, ETL rules + error-handler behavior, real test dirs, CI gates. Confirm CLAUDE.md contains nothing contradicting CONTRIBUTING.md. Confirm no cross-repo references.

  Run:
  ```bash
  grep -rnE "STM32F302|unit_audio|CODE BLOCK START" CLAUDE.md CONTRIBUTING.md README.md
  grep -rniE "station-manager|linux-image|HW-Module|internal-web" CLAUDE.md CONTRIBUTING.md README.md
  ```
  Expected: no output from either.

- [ ] **Step 2: Fact-trace spot check.** For each of: SoC, ETL pin `20.48.0`, `CONFIG_ETL_LOG_ERRORS`, CI job names, `USB_DEVICE_STACK_NEXT` classes, DT `band` — confirm the claim matches its source file (`board.yml`, `west.yml`, `app/prj.conf`, `.github/workflows/ci.yml`, `boards/.../fm_board_defconfig`, `subsys/module/devices/sa818/sa818_module.cpp`). Fix any mismatch.

- [ ] **Step 3: Language check.** CONTRIBUTING.md + CLAUDE.md fully English; README.md German. Fix stragglers.

- [ ] **Step 4: Commit any fixes**

  ```bash
  git add -A
  git commit -m "docs: consistency pass across CLAUDE/CONTRIBUTING/README" || echo "no fixes needed"
  ```

---

## Post-plan (controller, not a task step)

- Open one PR against `main` from `docs/fw-standards-rewrite`.
- Run copilot-loop; address findings.
- After merge, close PR #42 (`docs/fw-claude-wrapper-precision`) — its nuance is folded in.
