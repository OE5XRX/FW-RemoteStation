# USB 3-Class Composite Integration (fm_board) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the `fm_board` firmware enumerate the full 3-class USB composite (CDC-ACM + UAC2 audio + DFU) instead of CDC-ACM only, while preserving the current USB host identity.

**Architecture:** Adopt the upstream Zephyr sample composite path (`sample_usbd_init_device()` → `usbd_register_all_classes()`), which the `tests/usb_audio` suite already builds. Wire `app/src/main_usb_audio.cpp` + `app/src/usb_audio_bridge.cpp` into the fm_board app build via `common.cmake`, delete the obsolete CDC-only `app/src/usb_config.cpp`, and pin the USB identity through Kconfig. native_sim is unaffected (it has no USB).

**Tech Stack:** Zephyr RTOS (USB device stack `USB_DEVICE_STACK_NEXT`), CMake, Kconfig, Twister, Zephyr SDK 1.0.1 (arm-zephyr-eabi for the STM32U575 cross-build).

## Global Constraints

- **Board targets (exact):** cross-build `fm_board/stm32u575xx` (SoC per `board.yml`); simulation `native_sim/native/64`.
- **Preserve USB identity verbatim:** VID `0x2FE3`, PID `0x0012`, product string `"FM Transceiver Board"`, manufacturer `"OE5XRX"`, bus-powered, `bMaxPower` 500 mA.
- **Toolchain:** ARM cross-compiler at `/home/pbuchegger/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/`. If `west build` cannot find it, prefix commands with `ZEPHYR_SDK_INSTALL_DIR=/home/pbuchegger/zephyr-sdk-1.0.1`.
- **No new C/C++ logic:** this change is build + Kconfig + test-config only, plus one file deletion. clang-format-18 has nothing to reformat (no `.c/.h/.cpp` added or modified). Do not touch `main_usb_audio.cpp` / `usb_audio_bridge.cpp`.
- **Firmware stays thin:** no persistence / access model / platform config added.
- **west runs from the workspace:** all `west` commands run from the worktree root `/home/pbuchegger/oe5xrx/FW-RemoteStation/.claude/worktrees/debug+usb-sound-interface`. The west binary is `/home/pbuchegger/zephyr-oe5xrx/bin/west` (on PATH as `west`).

---

### Task 1: Point `tests/usb_audio` at the real SoC

The composite build test is pinned to `fm_board/stm32f302xc`, but the board's SoC is `stm32u575xx` (`boards/oe5xrx/fm_board/board.yml`). The test therefore never builds for the real target. Fixing this first proves the composite sources (`main_usb_audio.cpp` + `usb_audio_bridge.cpp` + `sample_usbd`) actually compile for STM32U575 before we touch the app build.

**Files:**
- Modify: `tests/usb_audio/testcase.yaml` (three occurrences of `fm_board/stm32f302xc`)

**Interfaces:**
- Consumes: nothing.
- Produces: a green `fm.usb_audio.build` build_only case for `fm_board/stm32u575xx` (evidence the composite compiles for the real SoC).

- [ ] **Step 1: Show the test is currently filtered off the real SoC**

Run:
```bash
west twister -T tests/usb_audio -p fm_board/stm32u575xx -v --build-only
```
Expected: 0 test configurations built/passed — the case is filtered because its `platform_allow` is `fm_board/stm32f302xc`, not the platform we requested.

- [ ] **Step 2: Fix the platform strings**

Edit `tests/usb_audio/testcase.yaml` — replace every `fm_board/stm32f302xc` with `fm_board/stm32u575xx`. There are three: two under `platform_allow:` (one per test case) and one under `integration_platforms:`. Resulting file:

```yaml
# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

# USB Audio Bridge Tests
# Tests for application-level USB Audio Class 2 integration

tests:
  fm.usb_audio.build:
    # Build test - verifies USB Audio compiles for fm_board
    build_only: true
    platform_allow:
      - fm_board/stm32u575xx
    tags:
      - usb
      - audio

  fm.usb_audio.stream:
    # Runtime test on fm_board with USB Audio enabled
    platform_allow:
      - fm_board/stm32u575xx
    harness: pytest
    harness_config:
      pytest_root:
        - pytest
    tags:
      - usb
      - audio
      - hardware
    integration_platforms:
      - fm_board/stm32u575xx
```

- [ ] **Step 3: Build the composite test for the real SoC**

Run:
```bash
west twister -T tests/usb_audio -p fm_board/stm32u575xx -v --build-only
```
Expected: `fm.usb_audio.build` builds and PASSES. `fm.usb_audio.stream` is filtered (it needs a connected DUT / pytest harness) — that is expected without hardware. If the ARM toolchain is not found, re-run with `ZEPHYR_SDK_INSTALL_DIR=/home/pbuchegger/zephyr-sdk-1.0.1` prefixed.

- [ ] **Step 4: Commit**

```bash
git add tests/usb_audio/testcase.yaml
git commit -m "test(usb_audio): target real SoC fm_board/stm32u575xx (was stm32f302xc)"
```

---

### Task 2: Wire the composite into the app build and delete `usb_config.cpp`

Replace the CDC-only app USB path with the composite path for fm_board, keep native_sim on its USB-free stub. Both changes must land together: deleting `usb_config.cpp` without the CMake rewire leaves the app with no USB; the rewire without the delete produces two `main()` definitions and two USBD contexts on `zephyr_udc0`.

**Files:**
- Modify: `app/CMakeLists.txt`
- Delete: `app/src/usb_config.cpp`

**Interfaces:**
- Consumes: `main_usb_audio.cpp` (`main()`, calls `sample_usbd_init_device()`, `usb_audio_bridge_register_ops()`/`usb_audio_bridge_start()`), `usb_audio_bridge.cpp`, and `${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake` (adds the include dir and compiles `sample_usbd_init.c`).
- Produces: an fm_board app image whose USB configuration is assembled by `usbd_register_all_classes()` (CDC-ACM + UAC2 + DFU). Identity at this point is still the defconfig default (PID `0x0100`) — corrected in Task 3.

- [ ] **Step 1: Establish the baseline (both targets build today)**

Run:
```bash
west build -p always -b native_sim/native/64 app
```
Expected: build PASSES (baseline for the regression check in Step 5).

- [ ] **Step 2: Rewrite `app/CMakeLists.txt`**

Replace the whole file with:

```cmake
cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C CXX ASM)

target_sources(app PRIVATE src/version_shell.cpp)

if (CONFIG_ETL)
    target_sources(app PRIVATE src/etl_error_handler.cpp)
endif()

if (CONFIG_BOARD_NATIVE_SIM)
    # native_sim has no USB; audio is exercised via emulated ADC/DAC.
    target_sources(app PRIVATE
        src/main.cpp
        src/sim_audio/sample_clock.cpp
        src/sim_audio/adc_sink.cpp
        src/sim_audio/sine_source.cpp
        src/sim_audio/wav_source.cpp
        src/sim_audio/audio_pipeline.cpp
        src/sim_audio/shell_wav.cpp
    )
elseif (CONFIG_USB_DEVICE_STACK_NEXT)
    # Real hardware: 3-class USB composite (CDC-ACM + UAC2 + DFU).
    include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
    target_sources(app PRIVATE
        src/main_usb_audio.cpp
        src/usb_audio_bridge.cpp
    )
else()
    target_sources(app PRIVATE src/main.cpp)
endif()
```

- [ ] **Step 3: Delete the obsolete CDC-only USB config**

Run:
```bash
git rm app/src/usb_config.cpp
```

- [ ] **Step 4: Build fm_board and confirm the composite links**

Run:
```bash
west build -p always -b fm_board/stm32u575xx app
```
Expected: build PASSES with no duplicate-`main` and no missing-`sample_usbd.h` errors. (If the toolchain is not auto-detected, prefix `ZEPHYR_SDK_INSTALL_DIR=/home/pbuchegger/zephyr-sdk-1.0.1`.)

- [ ] **Step 5: Rebuild native_sim (regression)**

Run:
```bash
west build -p always -b native_sim/native/64 app
```
Expected: build PASSES exactly as in Step 1 (uses the stub `main.cpp` + `sim_audio/*`, unaffected by the USB change).

- [ ] **Step 6: Commit**

```bash
git add app/CMakeLists.txt
git commit -m "app: build 3-class USB composite for fm_board, drop CDC-only usb_config

Wire main_usb_audio.cpp + usb_audio_bridge.cpp (via the Zephyr USB sample
common.cmake) into the fm_board app build so usbd_register_all_classes
registers CDC-ACM + UAC2 + DFU. native_sim keeps its USB-free stub path.
Deletes the obsolete usb_config.cpp, whose second USBD context on
zephyr_udc0 conflicts with the composite."
```

---

### Task 3: Pin the USB identity in `fm_board_defconfig`

The sample path reads identity from `CONFIG_SAMPLE_USBD_*`. The defconfig currently declares PID `0x0100` / product `"OE5XRX FM Remote Station"`; override these (and power attributes) so the host sees the exact identity it does today.

**Files:**
- Modify: `boards/oe5xrx/fm_board/fm_board_defconfig`

**Interfaces:**
- Consumes: the composite path from Task 2 (which uses these symbols).
- Produces: built config with PID `0x0012`, product `"FM Transceiver Board"`, bus-powered, `bMaxPower` 500 mA.

- [ ] **Step 1: Edit the USB identity keys**

In `boards/oe5xrx/fm_board/fm_board_defconfig`, in the `# USB Configuration` block:
- Change `CONFIG_SAMPLE_USBD_PID=0x0100` → `CONFIG_SAMPLE_USBD_PID=0x0012`
- Change `CONFIG_SAMPLE_USBD_PRODUCT="OE5XRX FM Remote Station"` → `CONFIG_SAMPLE_USBD_PRODUCT="FM Transceiver Board"`
- Add two lines (SELF_POWERED default is `y`; MAX_POWER default is `125`):
  ```
  CONFIG_SAMPLE_USBD_SELF_POWERED=n
  CONFIG_SAMPLE_USBD_MAX_POWER=250
  ```
`CONFIG_SAMPLE_USBD_VID=0x2FE3` and `CONFIG_SAMPLE_USBD_MANUFACTURER="OE5XRX"` are already correct — leave them.

- [ ] **Step 2: Rebuild fm_board**

Run:
```bash
west build -p always -b fm_board/stm32u575xx app
```
Expected: build PASSES.

- [ ] **Step 3: Verify the override reached the generated config**

Run:
```bash
grep SAMPLE_USBD build/zephyr/.config
```
Expected output includes:
```
CONFIG_SAMPLE_USBD_VID=0x2FE3
CONFIG_SAMPLE_USBD_PID=0x12
CONFIG_SAMPLE_USBD_MANUFACTURER="OE5XRX"
CONFIG_SAMPLE_USBD_PRODUCT="FM Transceiver Board"
CONFIG_SAMPLE_USBD_MAX_POWER=250
```
and `CONFIG_SAMPLE_USBD_SELF_POWERED` is absent or `=n`. (Kconfig normalises `0x0012` to `0x12` — same value.)

- [ ] **Step 4: Commit**

```bash
git add boards/oe5xrx/fm_board/fm_board_defconfig
git commit -m "fm_board: pin USB composite identity to PID 0x0012 / bus-powered 500mA

Preserve the existing host identity (PID 0x0012, product \"FM Transceiver
Board\", bus-powered, 500 mA) now that identity comes from CONFIG_SAMPLE_USBD_*
rather than the deleted usb_config.cpp."
```

---

### Task 4: Regression suite + hardware verification handoff

Confirm no simulation/CI regressions, then hand the definitive end-to-end check to the user (requires physical hardware — cannot be automated in this environment).

**Files:** none (verification only).

**Interfaces:**
- Consumes: the fully wired composite from Tasks 1–3.
- Produces: green CI-equivalent suites and a hardware verification checklist.

- [ ] **Step 1: Run the CI-equivalent native_sim suites**

Run:
```bash
west twister -T app --integration -v
west twister -T tests/sim_shell -p native_sim/native/64 -v
west twister -T tests/etl -p native_sim/native/64 -v
```
Expected: all PASS (these three are the CI `build_and_tests` gate; they must be unaffected by a fm_board-only change).

- [ ] **Step 2: Re-run the composite build test**

Run:
```bash
west twister -T tests/usb_audio -p fm_board/stm32u575xx -v --build-only
```
Expected: `fm.usb_audio.build` PASSES (composite still compiles for the real SoC after all changes).

- [ ] **Step 3: Hardware verification — USER-run (needs the physical fm_board)**

Flash the Task 3 build to the board, replug USB, then on the Linux host:
```bash
lsusb -v -d 2fe3:0012
aplay -l
arecord -l
```
Expected:
- `lsusb -v` now shows **three** functions — a CDC-ACM Interface Association, a UAC2 audio function (AudioControl + AudioStreaming interfaces), and a DFU interface — with PID still `0x0012`, "Bus Powered", `MaxPower 500mA`.
- `aplay -l` / `arecord -l` list a card named **OE5XRX** (playback → SA818 TX, capture → SA818 RX).

This is the definitive proof the sound interface is back. Report the `lsusb -v` output; if audio or DFU is still missing, stop and return to systematic-debugging with the new descriptor as evidence.

- [ ] **Step 4: Finish the branch**

Once Step 3 is confirmed on hardware, use the `superpowers:finishing-a-development-branch` skill to open the PR / integrate.

---

## Notes / follow-ups (out of scope for this plan)

- **CI does not build fm_board** (only native_sim + the three Twister suites). A broken composite build would not be caught by CI. Adding a `fm_board/stm32u575xx` build (and `tests/usb_audio --build-only`) to `.github/workflows/ci.yml` is a worthwhile separate change.
- **Stale docs:** `app/USB_AUDIO_BRIDGE.md` (references an old `audio_work_handler` model) and `boards/oe5xrx/fm_board/USB_CONFIGURATION.md` may no longer match the code; review in a follow-up.
