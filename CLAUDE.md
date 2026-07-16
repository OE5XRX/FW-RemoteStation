# FW-RemoteStation — Agent Reference

Zephyr-RTOS firmware for the OE5XRX Remote Station's STM32 part — SA818 FM-transceiver module, STM32U575 as USB bridge (UAC2 + CDC-ACM + DFU).

> **CONTRIBUTING.md is the canonical standard; this is a summary and never overrides it.**

---

## 1. Language & Coding Standard

### Two-tier language split

The tier is determined by the **layer**, not the file extension.

**Driver layer (`drivers/`)** — C-first Zephyr idiom:
- Zephyr device model: `struct device`, API structs, `device_is_ready()`.
- Devicetree: `DT_NODELABEL`, `DT_PROP`, `DT_ENUM_IDX`, etc.
- Public headers use `extern "C"` guards and expose a plain-C ABI.
- Result types are C enums (`enum sa818_result`).
- All functions returning a result code are marked `[[nodiscard]]`.
- No vtables, inheritance, or namespaces forced into the driver ABI.
- Modern C++ *inside* a driver TU is permitted — the constraint is on the *interface*, not the implementation. The SA818 driver TUs are `.cpp` yet expose a pure C ABI; that extension is an implementation detail. A new driver written in plain C is equally valid.

**Non-driver code (app / module layer / USB / sim)** — modern C++20 + ETL:
- OOP, RAII, `constexpr`, namespaces, value-types.
- Inheritance only where it carries a real abstraction interface (`mod::Setting` / `mod::Action` / `mod::Telemetry` in `iface.h` are the canonical exemplar).
- `std::string_view` for non-owning string references.
- `std::optional` and `std::variant` where they clarify intent (see ETL section below).

### Hard rules (apply everywhere)

1. **No dynamic memory allocation.** Forbidden: `new`, `delete`, `malloc`, `free`, `std::vector`, `std::string`, `std::list`, `std::map`, `std::function`, and any type that may allocate internally at runtime. Allowed: `std::array`, `std::span`, `std::string_view`, `std::optional`, `constexpr`, fixed-size buffers, value-types, ETL containers.

2. **No exceptions.** Built with exceptions disabled. Error handling via return/status types only.

3. **No RTTI.** No `dynamic_cast`, no `typeid`. Use virtual dispatch through an explicit interface if runtime polymorphism is needed.

### ETL usage

ETL is pinned at `20.48.0` (`west.yml`, path `modules/lib/etl`). Enabled via `CONFIG_ETL=y` and `CONFIG_ETL_LOG_ERRORS=y` in `app/prj.conf`.

| Instead of (heap-backed) | Use (ETL fixed-capacity)  |
|--------------------------|---------------------------|
| `std::string`            | `etl::string<N>`          |
| `std::vector<T>`         | `etl::vector<T, N>`       |
| `std::map<K, V>`         | `etl::map<K, V, N>`       |

`std::string_view`, `std::optional`, and `std::variant` are permitted because they do not heap-allocate. Important precision: **only the wrapper itself is stack-allocated — the contained type must also be heap-free.** `std::optional<std::string>` is forbidden; `std::optional<etl::string<N>>` is permitted. Verify this for every contained type.

**ETL overflow panics, not truncates.** When a runtime check fails (e.g. a container capacity overflow), the handler in `app/src/etl_error_handler.cpp` calls `printk` to emit the error location and then calls `k_panic()`. The handler is armed at boot via `SYS_INIT(etl_register_error_handler, PRE_KERNEL_1, 0)`. An ETL capacity overflow is a hard fault — size capacities correctly at design time.

### Formatting

clang-format-18, `.clang-format` in repo root (LLVM base, 2-space indent, 160-column limit). Scope: all `.c/.h/.cc/.hh/.cpp/.hpp` under `app/`, `boards/`, `tests/`. Format before every commit; the `clang_format` CI job fails if any diff is produced.

---

## 2. Architecture Orientation

### Module layer (`subsys/module/`, `include/oe5xrx/module/`)

`include/oe5xrx/module/iface.h` defines a generic, device-agnostic Capability framework. The header has **no** device or RTOS dependencies.

Key types:
- **`mod::Capability`** — abstract base; owns its typed descriptor (`FieldSpec`) and its set/get/do behavior.
- **Kind mixins** — `mod::Setting` (set + get), `mod::Action` (do + get), `mod::Telemetry` (get-only). Each enforces the op-to-kind contract before delegating to concrete hooks, so a wrong op can never reach a hook.
- **`mod::FieldSpec`** — static, typed descriptor; single source of truth for both the advertised field metadata and runtime input validation; rendered generically into the `describe` JSON.
- **`mod::JsonWriter`** — bounded, truncation-safe JSON string builder.
- **`mod::Result`** — outcome of a command: success carrying a typed value, or an error code.
- **`mod::Module`** — identity + a fixed registry of `Capability*`; renders `describe` JSON and dispatches `execute`.

Adding a capability = one new subclass + one registry entry. A whole new module type reuses the framework unchanged. Concrete capabilities live in `subsys/module/devices/` (e.g. `sa818_module.cpp`).

The `Sa818Context` struct in `sa818_module.cpp` is the RAM shadow of the current working state (used to rebuild full `sa818_at_set_group()` calls). It is **working state only, not persistence** — the firmware does not persist capability state across resets; that is the agent's responsibility.

### SA818 driver (`drivers/radio/sa818/`)

Organized as core / AT / audio / audio-stream / shell. Public headers live under `drivers/radio/sa818/sa818/` (e.g. `sa818/sa818.h`). The public interface exposes:
- Zephyr device model (`struct device`, API structs), Devicetree, `extern "C"` headers.
- Result enum `enum sa818_result` with `[[nodiscard]]` on all functions returning it.
- DT `band` property on the `sa818` node selects VHF (`SA818-V`, 134–174 MHz) or UHF (`SA818-U`, 400–480 MHz) at build time; this also sets the default frequency and frequency ranges for the capability descriptors.

### USB composite

Three-class composite on `USB_DEVICE_STACK_NEXT`:
- `USBD_AUDIO2_CLASS` — UAC2 audio streaming (SA818 audio bridge)
- `USBD_CDC_ACM_CLASS` — CDC-ACM serial (shell / console)
- `USBD_DFU` — DFU (firmware update)

### Firmware role: thin and self-describing

The firmware describes itself (identity + capabilities as machine-readable JSON via the `module describe` shell command) and executes commands — nothing more. Platform knowledge, capability persistence, and access/permission models belong outside the firmware (in the agent / server). New firmware code must preserve this boundary and not pull any of that logic in.

---

## 3. Build & Test

### Downstream Zephyr patches (run after `west update`)

This repo carries downstream fixes to west modules under `zephyr/patches/` (indexed by
`zephyr/patches.yml`), applied with:

```
west patch apply
```

Currently one patch: the STM32 UDC isochronous-OUT-incomplete recovery
(`HAL_PCD_ISOOUTIncompleteCallback` in `drivers/usb/udc/udc_stm32.c`) — **required for
UAC2 host→device playback (TX audio) on fm_board**; without it iso-OUT reception stalls after
a few frames. `west update` resets the module, so re-run `west patch apply` afterwards (CI does
this automatically after `action-zephyr-setup`). Upstream: zephyr#113622 (iso IN recovery is a
separate follow-up).

### Build commands

```
west build -b fm_board app
west build -b native_sim/native/64 app
```

### Test commands

```
west twister -T app --integration -v
west twister -T tests/sim_shell -p native_sim/native/64 -v
west twister -T tests/etl -p native_sim/native/64 -v
```

### pytest system tests

`tests/sim_shell/pytest/` drives the shell via stdin/stdout. The `SA818Simulator` class (`sa818_simulator.py`) speaks the AT protocol, enabling full protocol and capability system tests without any physical hardware.

### CI gates

Two jobs must be green for every PR and push to `main`:
- **`clang_format`** — clang-format-18 over `app/`, `boards/`, `tests/`; fails on any diff.
- **`build_and_tests`** — builds `native_sim`, then runs Twister on `app --integration`, `tests/sim_shell`, and `tests/etl`.

CI uses `zephyrproject-rtos/action-zephyr-setup@v1`, not the devcontainer.

---

## 4. Board / DT

| Target                 | Description                                              |
|------------------------|----------------------------------------------------------|
| `fm_board`             | Custom STM32U575 FM-transceiver board (real hardware); MPU + HW stack protection enabled |
| `native_sim/native/64` | Host simulation on Linux x86-64 — primary dev, CI, and system-test target |

DT bindings are under `dts/bindings/`.

---

## 5. Test Philosophy

Simulation equals hardware. Implement and test new functionality on `native_sim` first. Hardware-specific code must be cleanly encapsulated so that the logic layer remains testable without hardware. Code that is only testable on real hardware is a design problem, not a non-goal to be worked around.

No regressions. New code must include or reference tests.

---

## 6. Pitfalls

- **clang-format-18 is mandatory.** The version matters; earlier versions produce different output. Install `clang-format-18` explicitly.
- **ETL overflow panics, not truncates.** Overflowing an `etl::string<N>` or `etl::vector<T,N>` calls `k_panic()`. Size capacities correctly at design time.
- **Driver result codes are `[[nodiscard]]`.** The compiler will warn if you discard the return value of any `sa818_*` function; treat the warning as an error.
- **DT `band` is a build-time constant.** It selects the SA818 model string, the default frequency, and the valid frequency ranges at compile time via `DT_ENUM_IDX`. Do not attempt to change the band at runtime.
- **Keep platform/access/persistence out of FW.** The firmware is thin by design. Do not add capability persistence, user/role models, or platform configuration to the firmware.
- **Real test dirs are `tests/etl`, `tests/sim_shell`, `tests/usb_audio`.** Do not invent or reference test directory names not listed here. CI currently gates only `app --integration`, `tests/sim_shell`, and `tests/etl`; `tests/usb_audio` exists but is not wired into a CI Twister step yet — check `.github/workflows/ci.yml` for the exact set that runs.
