# Contributing to FW-RemoteStation

Thank you for your interest in contributing to **FW-RemoteStation**. This is
the Zephyr-RTOS firmware for the OE5XRX Remote Station's STM32 part — an
SA818-based FM transceiver module with a STM32U575 acting as the USB bridge
(UAC2 + CDC-ACM + DFU). This project values openness, testability, and
long-term maintainability. Please read this document carefully before
submitting changes; it is the single source of truth for the coding policy.

---

## 1. License

This project is released under the **GNU Lesser General Public License v3.0
or later (LGPL-3.0-or-later)**.

By submitting a contribution you confirm that:

- your contribution will be published under LGPL-3.0-or-later,
- you have the right to submit the code, and
- no incompatibly-licensed parts are introduced.

Contributions without clear license compatibility cannot be accepted.

---

## 2. Project Philosophy

- **Open source first.** Commercial use is permitted; proprietary forks of
  this code are unwanted — improvements benefit the community.
- **Simulation and tests are equal to hardware.** The `native_sim` target is a
  first-class development and CI platform, not a convenience shortcut.
- **Testability before optimisation.** A change that cannot be tested without
  real hardware is a design problem.
- **Maintainability for the long run.** Amateur radio infrastructure must
  outlive any single contributor.

---

## 3. Coding Standard

### 3.1 Two-Tier Language Split

The codebase applies a deliberate two-tier language policy. The tier is
determined by which **layer** the code belongs to, not by file extension.

#### Driver layer (`drivers/`)

Use the **C-first Zephyr idiom**:

- Zephyr device model: `struct device`, API structs, `device_is_ready()`.
- Devicetree: `DT_NODELABEL`, `DT_PROP`, `DT_ENUM_IDX`, and friends.
- Public headers use `extern "C"` guards and expose a plain-C ABI.
- Result types are C enums (e.g. `enum sa818_result`).
- All functions that return a result code are marked `[[nodiscard]]`.
- No C++ compulsion: do not force vtables, inheritance, or namespaces into the
  driver ABI.

Modern C++ **inside** a driver translation unit is permitted — the constraint
is on the *interface*, not the implementation. The SA818 driver TUs are `.cpp`
yet expose a pure C ABI; that extension is an implementation detail. A new
driver written entirely in plain C is equally valid.

#### Non-driver code (app / module layer / USB / sim)

Use **modern C++20 with ETL**:

- OOP, RAII, `constexpr`, namespaces, value-types.
- Inheritance only where it carries a real abstraction interface — the
  `mod::Setting` / `mod::Action` / `mod::Telemetry` mixins in `iface.h` are
  the canonical exemplar.
- `std::string_view` for non-owning string references.
- `std::optional` and `std::variant` where they clarify intent (see §3.3).
- ETL fixed-capacity containers as replacements for heap-backed `std::`
  equivalents (see §3.3).

### 3.2 Hard Rules (Apply Everywhere)

These three rules are absolute and apply to every layer:

1. **No dynamic memory allocation.** The following are forbidden:
   `new`, `delete`, `malloc`, `free`, `std::vector`, `std::string`,
   `std::list`, `std::map`, `std::function`, and any type that may allocate
   internally at runtime.

   Allowed: `std::array`, `std::span`, `std::string_view`, `std::optional`,
   `constexpr`, fixed-size buffers, value-types, and ETL containers (see §3.3).

2. **No exceptions.** The project is built with exceptions disabled.
   Error handling is done via return/status types. Code must be correct without
   any exception-based control flow.

3. **No RTTI.** No `dynamic_cast`; no `typeid`. If you need runtime
   polymorphism, use virtual dispatch through an explicit interface.

### 3.3 ETL Usage Guide

[ETL (Embedded Template Library)](https://github.com/ETLCPP/etl) is pinned at
`20.48.0` (see `west.yml`, path `modules/lib/etl`). It is enabled via
`CONFIG_ETL=y` and `CONFIG_ETL_LOG_ERRORS=y` in `app/prj.conf`.

#### Mapping table

| Instead of this (heap-backed) | Use this (ETL fixed-capacity) |
|-------------------------------|-------------------------------|
| `std::string`                 | `etl::string<N>`              |
| `std::vector<T>`              | `etl::vector<T, N>`           |
| `std::map<K, V>`              | `etl::map<K, V, N>`           |

Where `N` is the maximum capacity declared at compile time.

#### `std::string_view`, `std::optional`, `std::variant`

These standard-library types are permitted and encouraged because they do not
allocate on the heap:

- **`std::string_view`** — a non-owning view over a string; no allocation,
  fully compatible with the no-alloc rule. Prefer it over raw `const char*`
  where a view abstraction adds clarity.
- **`std::optional<T>`** and **`std::variant<Ts...>`** — only the *wrapper
  itself* is stack-allocated. The *contained type* must itself be heap-free. An
  `std::optional<std::string>` is forbidden; `std::optional<etl::string<N>>` is
  permitted because `etl::string<N>` is stack-allocated. Verify this for every
  contained type.

#### ETL error handler

ETL is built with exceptions disabled. When a runtime check fails (e.g. a
container capacity overflow), ETL invokes the registered error handler instead
of throwing. The handler in `app/src/etl_error_handler.cpp` uses `printk` to
emit the error location and then calls `k_panic()` — fail fast.

The handler is armed automatically at boot via
`SYS_INIT(etl_register_error_handler, PRE_KERNEL_1, 0)`, which runs before
essentially all other initialisation. It uses `printk` rather than the logging
subsystem because `printk` is safe at any boot stage, including before the
logging subsystem is up.

**An ETL capacity overflow is a hard fault, not a silent truncation. Size
capacities correctly at design time.**

### 3.4 Formatting

Code formatting is mandatory and CI-enforced.

- Tool: **clang-format-18**, configuration in `.clang-format`.
- Style: LLVM base, 2-space indent, 160-column limit, C++ Latest standard.
- Scope: all `.c`, `.h`, `.cc`, `.hh`, `.cpp`, `.hpp` files under `app/`,
  `boards/`, and `tests/`.
- Format before every commit: `clang-format -i <files>` or configure your
  editor to format on save.

The `clang_format` CI job fails the build if any formatting difference is
detected. Do not skip it.

### 3.5 General Code Quality

- **No magic numbers.** Use `constexpr` named constants.
- **Speaking identifiers.** Names should reveal intent without requiring a
  comment.
- **Simple, traceable abstractions.** Prefer a flat, readable solution over a
  clever, layered one.

---

## 4. Architecture Overview

Understanding the architecture helps you place new code in the right layer.

### Module layer (`subsys/module/`, `include/oe5xrx/module/`)

`include/oe5xrx/module/iface.h` defines a generic, device-agnostic capability
framework with **no** device or RTOS dependencies in the header itself:

- **`mod::Capability`** — abstract base; owns both its typed descriptor
  (`FieldSpec`) and its set/get/do behaviour.
- **Kind mixins** — `mod::Setting` (set + get), `mod::Action` (do + get),
  `mod::Telemetry` (get-only): each enforces the op-to-kind contract before
  delegating to concrete hooks, so a wrong op can never reach a hook.
- **`mod::FieldSpec`** — static, typed descriptor rendered generically into the
  `describe` JSON; the single source of truth for both the advertised field
  metadata and the runtime input validation.
- **`mod::JsonWriter`** — bounded, truncation-safe JSON builder.
- **`mod::Result`** — outcome of a command: success carrying a typed value, or
  an error code.
- **`mod::Module`** — identity + a fixed registry of `Capability*`; renders
  `describe` JSON and dispatches `execute`.

Concrete capabilities live in `subsys/module/devices/` (e.g.
`sa818_module.cpp`). Adding a capability is one new subclass plus one registry
entry; a whole new module type reuses the framework unchanged.

The `Sa818Context` struct in `sa818_module.cpp` is the RAM shadow of the
current working state (used to rebuild full `sa818_at_set_group()` calls).
It is **working state only, not persistence** — the firmware does not persist
capability state across resets; that is the agent's responsibility.

### SA818 driver (`drivers/radio/sa818/`)

The SA818 driver (core / AT / audio / shell) exposes a pure C ABI via headers
under `drivers/radio/sa818/sa818/`. The public interface uses the Zephyr device
model (`struct device`, API structs), Devicetree, `extern "C"` headers, a
result enum (`enum sa818_result`), and `[[nodiscard]]` on functions that
return a result code. See `sa818/sa818.h` for the canonical example.

### USB composite

The board-level defconfig enables a three-class USB composite on
`USB_DEVICE_STACK_NEXT`:

- `USBD_AUDIO2_CLASS` — UAC2 audio streaming (SA818 audio bridge)
- `USBD_CDC_ACM_CLASS` — CDC-ACM serial (shell / console)
- `USBD_DFU` — DFU (firmware update)

### Firmware role: thin and self-describing

The firmware intentionally stays thin. It **describes itself** (identity +
capabilities as machine-readable JSON) and **executes commands** — nothing
more. Platform knowledge, capability persistence, and access/permission models
belong outside the firmware (in the agent / server). New firmware code must
preserve this boundary.

### Build targets

| Target                  | Purpose                                     |
|-------------------------|---------------------------------------------|
| `fm_board`              | Custom STM32U575 FM-transceiver board (real hardware) |
| `native_sim/native/64`  | Host simulation on Linux x86-64 (primary dev / CI) |

---

## 5. Simulation vs. Hardware

Implement and test new functionality on `native_sim` first. Hardware-specific
code must be cleanly encapsulated so that the logic layer remains testable
without hardware.

The `native_sim` target supports:

- UART shell over stdin/stdout
- ADC emulation
- EEPROM simulation
- WAV playback and sine-wave generation

A pytest harness under `tests/sim_shell/pytest/` drives the shell via
stdin/stdout and includes a Python `SA818Simulator` that speaks the AT
protocol, enabling full system tests without any physical hardware.

Code that is only testable on real hardware is a non-goal (see §8).

---

## 6. Build and Test Workflow

### Build

```
west build -b fm_board app
west build -b native_sim/native/64 app
```

### Run tests locally

```
west twister -T app --integration -v
west twister -T tests/sim_shell -p native_sim/native/64 -v
west twister -T tests/etl -p native_sim/native/64 -v
```

Real test directories: `tests/etl`, `tests/sim_shell`, `tests/usb_audio`.

### CI gates

Two jobs must be green for every pull request and push to `main`:

- **`clang_format`** — runs clang-format-18 over `app/`, `boards/`, `tests/`
  and fails if any diff is produced.
- **`build_and_tests`** — builds `native_sim`, then runs Twister on
  `app --integration`, `tests/sim_shell`, and `tests/etl`.

CI uses the official [Zephyr GitHub Actions](https://github.com/zephyrproject-rtos/action-zephyr-setup)
(`zephyrproject-rtos/action-zephyr-setup@v1`), not the devcontainer, to keep
the build environment free and open. The devcontainer is a local development
aid only.

### Setup (first time)

```
west init -m https://github.com/OE5XRX/FW-RemoteStation
west update
west zephyr-export
```

---

## 7. Commits and Pull Requests

- Keep commits small and logically separate.
- Write commit messages that explain what changed and, more importantly, why.
- Pull requests must:
  - describe what was changed,
  - explain why the change is worthwhile, and
  - name or add tests that cover the change.

---

## 8. Non-Goals

The following are generally not accepted:

- Proprietary or closed dependencies.
- Unnecessary complexity.
- Changes without a test strategy.
- Code that is only testable on real hardware (this is a design problem; fix
  the encapsulation first).

---

## 9. Questions and Discussion

For questions or before embarking on a large refactor, please open an issue to
discuss the approach first. This avoids duplicate work and keeps changes
aligned with the project direction.

---

Thank you for contributing and for keeping **FW-RemoteStation** open,
testable, and maintainable.
