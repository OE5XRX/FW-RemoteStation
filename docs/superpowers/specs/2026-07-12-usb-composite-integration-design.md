# USB 3-Class Composite Integration (fm_board) — Design

Date: 2026-07-12
Branch: `worktree-debug+usb-sound-interface` (based on `origin/main`)
Status: Approved design, pending implementation plan

## Problem

On real hardware (`fm_board`, STM32U575), only the CDC-ACM serial interface enumerates
over USB. The UAC2 audio (sound) interface and the DFU interface never appear. This has
been true since first bring-up — it is a missing integration, not a regression.

### Confirmed root cause (verified by inspection + live `lsusb -v`)

The running firmware's USB descriptor contains exactly one function: CDC-ACM
(`bNumInterfaces 2`, VID `0x2FE3` / PID `0x0012`, bus-powered, 500 mA). This matches
`app/src/usb_config.cpp` byte-for-byte (hard-coded VID/PID at lines 18–19, device code
triple `MISC/0x02/0x01` at line 55).

`app/src/usb_config.cpp:49` registers a single class instance:
`usbd_register_class(&cdc_acm_serial, "cdc_acm_0", speed, 1)`. It never calls
`usbd_register_all_classes`, so UAC2 and DFU are never added to the configuration.

The intended 3-class composite lives in `app/src/main_usb_audio.cpp` +
`app/src/usb_audio_bridge.cpp`, but those files are **not** referenced by
`app/CMakeLists.txt` — the app compiles the stub `app/src/main.cpp`
(`int main(){ return 0; }`) plus `usb_config.cpp` instead. The composite path is only
built by `tests/usb_audio`.

The Kconfig class symbols are all already enabled in
`boards/oe5xrx/fm_board/fm_board_defconfig` (`USB_DEVICE_STACK_NEXT`,
`USBD_CDC_ACM_CLASS`, `USBD_AUDIO2_CLASS`, `USBD_DFU`). The gap is purely build wiring
plus a conflicting second USBD context.

### Key facts established during investigation

- `sample_usbd` is **not** missing. It exists upstream at
  `<west>/zephyr/samples/subsys/usb/common/`: header `sample_usbd.h`, implementation
  `sample_usbd_init.c` (the `.c` basename differs from the `#include`, which is why it
  looked absent). `common.cmake` there adds the include dir and compiles the impl.
- `sample_usbd_init.c` registers every enabled class via
  `usbd_register_all_classes(..., blocklist)` with `blocklist = {"dfu_dfu"}` — i.e.
  runtime DFU is registered; only the DFU-mode instance is withheld while the app runs.
- native_sim has **no** USB / no UAC2. Audio there is exercised through emulated ADC/DAC
  (`app/src/sim_audio/*`, `CONFIG_ADC_EMUL` / `CONFIG_DAC_WAV`). The USB composite is a
  fm_board-only concern by design.
- The UAC2 DT node `uac2_radio` exists in `boards/oe5xrx/fm_board/fm_board.dts` with the
  terminal ordering the bridge hard-codes (`USB_OUT_TERMINAL_ID=1`, `USB_IN_TERMINAL_ID=4`).

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Integration approach | Adopt the sample composite path | Already built by `tests/usb_audio`; board already provisioned; least new code; app and test share one code path. |
| Scope | Audio **and** DFU | Both are missing; the composite brings DFU in for free; defconfig already enables `USBD_DFU`. |
| Host identity | Keep the *old* identity | Avoid breaking existing udev rules / host setup: PID `0x0012`, product "FM Transceiver Board", bus-powered, 500 mA — set via Kconfig override. |
| `usb_config.cpp` | Delete the file | Dead once the composite path is active; its second USBD context on `zephyr_udc0` would conflict. User approved deletion. |

## Approach: adopt the sample composite path

### Build system — `app/CMakeLists.txt` (the actual fix)

Split sources by board:

- **native_sim:** keep `src/main.cpp` (stub) + `src/sim_audio/*` (unchanged; no USB).
- **Otherwise (fm_board):**
  - `include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)`
  - `target_sources(app PRIVATE src/main_usb_audio.cpp src/usb_audio_bridge.cpp)`
- Remove the unconditional `src/main.cpp` and the `usb_config.cpp` block from the
  fm_board path (otherwise two `main()` definitions / two USBD contexts on `zephyr_udc0`).
- `version_shell.cpp` and (under `CONFIG_ETL`) `etl_error_handler.cpp` stay compiled for
  both targets.

### Delete `app/src/usb_config.cpp`

The CDC-only self-initializing (`SYS_INIT`) context is superseded by the composite. Nothing
references it beyond its own `SYS_INIT`. Removing it eliminates the conflicting second USBD
context and the source of the confusion.

### Identity override — `boards/oe5xrx/fm_board/fm_board_defconfig`

The sample path reads identity from Kconfig, so pin it to the current values:

```
CONFIG_SAMPLE_USBD_PID=0x0012            # was 0x0100
CONFIG_SAMPLE_USBD_PRODUCT="FM Transceiver Board"   # was "OE5XRX FM Remote Station"
CONFIG_SAMPLE_USBD_MANUFACTURER="OE5XRX" # unchanged
CONFIG_SAMPLE_USBD_SELF_POWERED=n        # -> bus-powered (default is y)
CONFIG_SAMPLE_USBD_MAX_POWER=250         # -> 500 mA (2 mA units; default 125)
```

### No code changes to the audio sources

`main_usb_audio.cpp` and `usb_audio_bridge.cpp` are used as-is. `#include "sample_usbd.h"`
resolves once `common.cmake` adds the include dir. Terminal IDs already match `fm_board.dts`.

## Data flow (unchanged; now active)

`usb_audio_bridge_register_ops(uac2)` (must run before USB init) →
`sample_usbd_init_device()` → `usbd_register_all_classes()` → CDC-ACM + UAC2 + DFU in the
configuration → `usbd_enable()` → `usb_audio_bridge_start(sa818)` wires the UAC2
terminals to the SA818 generic audio stream via two ring buffers (USB-OUT → SA818-TX,
SA818-RX → USB-IN, 8 kHz / 16-bit / mono) → host sees an ALSA card ("OE5XRX") plus the
serial console and a DFU interface.

## Verification

1. **native_sim regression:** `west build -b native_sim/native/64 app`; Twister on
   `app --integration`, `tests/sim_shell`, `tests/etl` all green (unaffected — no USB).
2. **fm_board build:** `west build -b fm_board app` compiles and links; `tests/usb_audio`
   (build_only + boot-log pytest) stays green.
3. **Real hardware (user-driven, the true end-to-end proof):** flash, then
   - `lsusb -v -d 2fe3:0012` shows three functions (CDC-ACM IAD, UAC2 audio, DFU), PID
     still `0x0012`, bus-powered / 500 mA.
   - `aplay -l` / `arecord -l` list the "OE5XRX" audio card.

## Open risks / to verify during implementation

- **`tests/usb_audio/testcase.yaml` is pinned to `fm_board/stm32f302xc`**, while CLAUDE.md
  states fm_board is STM32U575. Confirm the correct board/SoC target string and that the
  test (and `west build -b fm_board app`) actually builds for the real SoC. Fix the target
  if stale.
- **CI does not build fm_board** (only native_sim + the three Twister suites). This fix is
  fm_board-only, so CI will not catch a broken composite build. Adding an fm_board build to
  CI is worthwhile but treated as separate scope, not part of this fix.
- **500 mA bus-powered** is at the USB limit; retained only to preserve current behavior.

## Out of scope

- Persisting capability state (belongs in the agent/server, per firmware boundary).
- Any refactor of the audio bridge internals or the SA818 driver.
- Wiring fm_board into CI (noted as a follow-up).
