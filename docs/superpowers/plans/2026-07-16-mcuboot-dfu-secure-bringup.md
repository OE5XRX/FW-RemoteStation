# Secure MCUboot + USB-DFU Bringup — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring up a brick-proof, authenticated firmware-update path on the fm_board (STM32U575): MCUboot bootloader + Zephyr `USBD_DFU` flash backend, with health-gated self-confirm and auto-revert.

**Architecture:** Sysbuild builds MCUboot + a signed app into bank-aligned A/B slots. A DFU download writes slot1; MCUboot swaps on reset (swap-using-offset). The new image boots in test mode and confirms itself only after proving USB + shell transport + SA818 are alive; otherwise the IWDG / a deadline reboot triggers MCUboot auto-revert. Two build variants — `bare` (debug, app @ 0x0, no bootloader) and `prod` (`--sysbuild`, signed @ slot0) — differ only by whether sysbuild runs.

**Tech Stack:** Zephyr (T2 west workspace), sysbuild, MCUboot, imgtool (ECDSA-P256), `USB_DEVICE_STACK_NEXT`, C++20 + ETL, native_sim + Twister, dfu-util.

**Spec:** `docs/superpowers/specs/2026-07-16-mcuboot-dfu-secure-bringup-design.md`

**Scope:** This plan covers **M0–M3** (partition redesign → sysbuild/MCUboot → DFU wiring → security hardening → tests/CI). **M4 (bootloader recovery, §6b)** is a separate later plan — its unknowns (bootloader USB stack on this UDC) must not block the core bringup.

## Global Constraints

- **COMMIT FREEZE ACTIVE.** The user has frozen commits. The `Commit` steps below are the normal TDD rhythm, but **do not run `git commit` / `git push` until the user lifts the freeze.** Stage and stop; batch the commits when cleared.
- **Zephyr env required.** `west` must be on PATH with the toolchain; run `west patch apply` after any `west update` (STM32 UDC iso-OUT recovery patch — required for UAC2 on fm_board).
- **No dynamic allocation, no exceptions, no RTTI.** Use `std::array`/`etl::*`/`std::span`/`std::optional` (contained type must also be heap-free). Errors via return/status types.
- **clang-format-18 mandatory** over `app/`, `boards/`, `tests/` — CI `clang_format` fails on any diff. Format before staging.
- **Driver result codes are `[[nodiscard]]`** — never discard a `sa818_*` return.
- **Thin firmware** — no persistence, no platform/access logic. The agent orchestrates updates (stops audio, triggers reboot); the FW does not enforce an idle precondition.
- **Two build variants** (verbatim): `west build -b fm_board app` = bare/debug; `west build -b fm_board --sysbuild app` = prod. Everything else lives in files.
- **Flash facts:** STM32U575xI, 2 MB dual-bank (Bank 1 = 0x000000–0x100000, Bank 2 = 0x100000–0x200000), 8 KB sectors. Slots must be equal size and bank-separated.
- **Version source:** `app/VERSION` (`VERSION_MAJOR/MINOR/PATCHLEVEL/TWEAK`) → consumed automatically by MCUboot signing.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `boards/oe5xrx/fm_board/fm_board.dts` | Bank-aligned partition table (boot/slot0/slot1/storage), drop scratch | 1 |
| `app/sysbuild.conf` | `SB_CONFIG_BOOTLOADER_MCUBOOT`, swap-using-offset, downgrade prevention, ECDSA-P256, key path | 2 |
| `app/sysbuild/mcuboot.conf` | MCUboot image config fragment (log level, crypto) | 2 |
| `app/prj.conf` | DFU flash backend + confirm/IWDG config guarded by `CONFIG_BOOTLOADER_MCUBOOT` | 4, 7 |
| `app/src/boot_confirm/health_gate.h/.cpp` | Portable, native_sim-testable health-gate state machine | 6 |
| `app/src/boot_confirm/boot_confirm_fm.cpp` | fm_board wiring: real probes (USB/shell/SA818), thread, IWDG | 7 |
| `tests/boot_confirm/` | Twister/ztest unit tests for the health-gate state machine (native_sim) | 6 |
| `.github/workflows/ci.yml` | Add `prod` (sysbuild, signed) build gate | 9 |
| `keys/` (git-ignored) + release wiring | ECDSA-P256 signing key handling | 8 |

---

## Task 1: Bank-aligned partition table

**Files:**
- Modify: `boards/oe5xrx/fm_board/fm_board.dts` (the `&flash0 { partitions { … } }` node, ~lines 280–317)

**Interfaces:**
- Produces: DT labels `boot_partition`, `slot0_partition`, `slot1_partition`, `storage_partition` at the bank-aligned offsets below. `zephyr,code-partition = &slot0_partition` already set (line 23) — leave it.

- [ ] **Step 1: Replace the partitions node** with the bank-aligned layout (drop `scratch_partition`):

```dts
&flash0 {
	partitions {
		compatible = "fixed-partitions";
		#address-cells = <1>;
		#size-cells = <1>;

		boot_partition: partition@0 {
			label = "mcuboot";
			reg = <0x00000000 0x00020000>;	/* 128 KB, Bank 1 */
			read-only;
		};

		slot0_partition: partition@20000 {
			label = "image-0";
			reg = <0x00020000 0x000E0000>;	/* 896 KB, ends at 0x100000 (Bank 1) */
		};

		slot1_partition: partition@100000 {
			label = "image-1";
			reg = <0x00100000 0x000E0000>;	/* 896 KB, Bank 2 */
		};

		storage_partition: partition@1e0000 {
			label = "storage";
			reg = <0x001E0000 0x00020000>;	/* 128 KB, Bank 2 */
		};
	};
};
```

- [ ] **Step 2: Verify the bare build still configures** (bare ignores code-partition, so this only checks DT validity):

Run: `west build -b fm_board -p always app`
Expected: build SUCCEEDS; no devicetree overlap/parse errors.

- [ ] **Step 3: Sanity-check offsets** — confirm arithmetic and equal slots:

Run: `python3 -c "b=0x20000;s0=0xE0000;print(hex(0x20000+s0), hex(0x100000+s0), hex(0x1E0000+0x20000)); assert 0x20000+s0==0x100000 and 0x100000+s0==0x1E0000 and 0x1E0000+0x20000==0x200000"`
Expected: prints `0x100000 0x1e0000 0x200000`, no assertion error.

- [ ] **Step 4: Stage (do NOT commit — freeze):**

```bash
git add boards/oe5xrx/fm_board/fm_board.dts
# git commit -m "feat(board): bank-aligned MCUboot A/B partition table on fm_board"  # blocked by commit freeze
```

---

## Task 2: Sysbuild + MCUboot config (M0 build)

**Files:**
- Create: `app/sysbuild.conf`
- Create: `app/sysbuild/mcuboot.conf`

**Interfaces:**
- Consumes: partition labels from Task 1.
- Produces: `west build -b fm_board --sysbuild app` builds MCUboot + a signed app image. Sysbuild auto-sets the app's `CONFIG_BOOTLOADER_MCUBOOT=y` (→ `USE_DT_CODE_PARTITION` → link @ slot0).

- [ ] **Step 1: Create `app/sysbuild.conf`** (dev key for M0/M1; real key in Task 8):

```
# Build MCUboot together with the signed application.
SB_CONFIG_BOOTLOADER_MCUBOOT=y

# Swap-using-offset: scratch-less, interrupt-safe rollback (current Zephyr default).
SB_CONFIG_MCUBOOT_MODE_SWAP_USING_OFFSET=y

# ECDSA-P256 signatures (HW-accelerated by the U5 PKA).
SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y

# M0/M1 bringup: MCUboot's insecure development key. Replaced in Task 8.
# (Left at the MCUboot default key here — do NOT ship this.)
```

- [ ] **Step 2: Create `app/sysbuild/mcuboot.conf`** (MCUboot image config fragment):

```
# Reject downgrades: an update whose version is <= the running image is refused.
CONFIG_MCUBOOT_DOWNGRADE_PREVENTION=y

# Keep MCUboot small and quiet; logs go to SWO/RTT/UART on the bench (not USB).
CONFIG_MCUBOOT_LOG_LEVEL_INF=y
```

- [ ] **Step 3: Build prod and verify artifacts exist:**

Run: `west build -b fm_board -p always --sysbuild app`
Expected: build SUCCEEDS. Then:
Run: `ls build/mcuboot/zephyr/zephyr.hex build/app/zephyr/zephyr.signed.bin build/app/zephyr/zephyr.signed.hex`
Expected: all three files exist (MCUboot image + signed app in bin and hex).

- [ ] **Step 4: Confirm the app linked at the slot0 offset** (proves the sysbuild→CONFIG_BOOTLOADER_MCUBOOT→code-partition chain):

Run: `grep -E "CONFIG_BOOTLOADER_MCUBOOT|CONFIG_USE_DT_CODE_PARTITION|CONFIG_FLASH_LOAD_OFFSET" build/app/zephyr/.config`
Expected: `CONFIG_BOOTLOADER_MCUBOOT=y`, `CONFIG_USE_DT_CODE_PARTITION=y`, `CONFIG_FLASH_LOAD_OFFSET=0x20000`.

- [ ] **Step 5: Confirm bare build is unaffected** (links @ 0x0, no MCUboot):

Run: `west build -b fm_board -p always app && grep -E "CONFIG_BOOTLOADER_MCUBOOT|CONFIG_FLASH_LOAD_OFFSET" build/zephyr/.config`
Expected: `CONFIG_BOOTLOADER_MCUBOOT` is NOT set (or `=n`); `CONFIG_FLASH_LOAD_OFFSET=0x0`.

- [ ] **Step 6: Stage (no commit — freeze).**

```bash
git add app/sysbuild.conf app/sysbuild/mcuboot.conf
```

---

## Task 3: HW M0 — app boots through MCUboot (bench)

**Hardware verification task — no automated test. Requires the fm_board + SWD probe + a serial console on SWO/RTT or the CDC-ACM shell.**

**Files:** none (uses Task 2 artifacts).

- [ ] **Step 1: Flash MCUboot + signed app** (sysbuild flashes both domains):

Run: `west build -b fm_board --sysbuild app && west flash`
Expected: both `mcuboot` and `app` domains flash without error.

- [ ] **Step 2: Verify boot chain.** Observe MCUboot on SWO/RTT (or logic: it validates slot0 then chainloads), then the app banner over the CDC-ACM shell.
Expected on the app console: `SA818 USB Audio Application Starting...` and `USB device enabled`. The device enumerates on the host (`lsusb` shows VID 0x2FE3 / PID 0x0012 "FM Transceiver Board").

- [ ] **Step 3: Record result** in the PR description / a bringup note (pass/fail + MCUboot version line). If fail: MCUboot rejects the image → re-check signature type + key match between `sysbuild.conf` and the MCUboot build; check slot0 offset == `CONFIG_FLASH_LOAD_OFFSET`.

---

## Task 4: USB-DFU flash backend wiring

**Files:**
- Modify: `app/prj.conf` (append a DFU section)
- Verify/Modify: how `sample_usbd_init_device()` registers the DFU class (in `samples/subsys/usb/common` usbd next glue + DT). Confirm the DFU image points at `slot1`.

**Interfaces:**
- Consumes: `slot1_partition` (Task 1), MCUboot build (Task 2).
- Produces: in the `prod` build, the running composite exposes a DFU alt-setting that writes to slot1; `dfu-util --detach` switches to DFU mode.

- [ ] **Step 1: Read the current DFU registration path.** Confirm whether `CONFIG_USBD_DFU=y` (in `fm_board_defconfig`) plus the common `sample_usbd` glue already registers a DFU instance, and how the image/backend is declared (look for `USBD_DEFINE_DFU_IMAGE` / DFU image DT nodes / `CONFIG_USBD_DFU_ENABLE_UPLOAD`). Follow `samples/subsys/usb/dfu` for the flash-backend pattern.

Run: `grep -rniE "dfu" $ZEPHYR_BASE/samples/subsys/usb/dfu/ | grep -iE "flash|slot|image|backend" | head`
Expected: identifies the `CONFIG_APP_USB_DFU_USE_FLASH_BACKEND`-equivalent wiring and the DFU image definition macro used by the usbd-next DFU class.

- [ ] **Step 2: Append the DFU config to `app/prj.conf`** (guarded so it only applies to the MCUboot/prod build):

```
# =============================================================================
# Firmware update (MCUboot + USB DFU) — prod build only
# =============================================================================
# All of this is inert in the `bare` build because CONFIG_BOOTLOADER_MCUBOOT is
# only set by sysbuild. The DFU flash backend targets slot1; MCUboot swaps on
# reset. Do NOT enable USB_DFU_PERMANENT_DOWNLOAD — we want the test/confirm
# cycle so a bad image auto-reverts.
CONFIG_BOOTLOADER_MCUBOOT=n  # sysbuild overrides to =y for prod; keep bare default explicit

# Image manager + flash image writer for the DFU backend (effective only when
# CONFIG_BOOTLOADER_MCUBOOT=y, i.e. under sysbuild).
CONFIG_IMG_MANAGER=y
CONFIG_IMG_ERASE_PROGRESSIVELY=y
CONFIG_STREAM_FLASH=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
```

> NOTE for implementer: the exact `USBD_DFU_*` symbol set and the DFU image macro depend on the Zephyr version resolved in this workspace — take them from `samples/subsys/usb/dfu` (Step 1). Add only what that sample requires for the flash backend; keep the audio composite intact.

- [ ] **Step 3: Build prod and confirm DFU + img_mgmt compiled in:**

Run: `west build -b fm_board -p always --sysbuild app && grep -E "CONFIG_USBD_DFU=y|CONFIG_IMG_MANAGER=y" build/app/zephyr/.config`
Expected: both present.

- [ ] **Step 4: Confirm bare build still links (DFU/img_mgmt inert):**

Run: `west build -b fm_board -p always app`
Expected: build SUCCEEDS (no MCUboot, no img_mgmt pulled into the bare image).

- [ ] **Step 5: Stage (no commit — freeze).**

```bash
git add app/prj.conf
```

---

## Task 5: HW M1 = Gate 1 — DFU swap on hardware (bench)

**Hardware verification task. Requires fm_board (running Task 3 image) + host with `dfu-util`.** This is the milestone that proves DFU works on this UDC.

- [ ] **Step 1: Build a distinguishable second image** (bump `app/VERSION` PATCHLEVEL and change the banner so you can tell them apart), keep the dev key:

Run: edit `app/VERSION` PATCHLEVEL 0→1; `west build -b fm_board --sysbuild app`; copy `build/app/zephyr/zephyr.signed.bin` to `/tmp/fw_v101.bin`.

- [ ] **Step 2: Enter DFU mode and list interfaces:**

Run: `dfu-util -l`
Expected: the device appears with its DFU alt-settings; note the slot1/download alt number (e.g. `--alt 1`). If only runtime-mode DFU appears: `dfu-util -e` (detach) then re-list.

- [ ] **Step 3: Download the new image to slot1:**

Run: `dfu-util --alt 1 --download /tmp/fw_v101.bin`
Expected: transfer completes 100%.

- [ ] **Step 4: Reset and verify the swap:** power-cycle / reset the board.
Expected: the console banner now shows the v1.0.1 build; `lsusb` still enumerates. MCUboot swapped slot1→slot0.

- [ ] **Step 5: Record result.** DFU-on-HW verified ⇒ this is a candidate build-in-public milestone (see spec §9). If fail: check the DFU alt mapping to slot1, that the uploaded artifact is `zephyr.signed.bin` (not raw), and MCUboot logs for signature/version rejection.

---

## Task 6: Health-gate state machine (native_sim, TDD)

**Files:**
- Create: `app/src/boot_confirm/health_gate.h`
- Create: `app/src/boot_confirm/health_gate.cpp`
- Create: `tests/boot_confirm/CMakeLists.txt`
- Create: `tests/boot_confirm/prj.conf`
- Create: `tests/boot_confirm/testcase.yaml`
- Create: `tests/boot_confirm/src/main.cpp`

**Interfaces:**
- Produces (consumed by Task 7):

```cpp
namespace boot {

// Pollable readiness predicate for one subsystem. Returns true once alive.
using HealthProbe = bool (*)(void *ctx);

struct HealthCriterion {
  const char *name;
  HealthProbe probe;
  void *ctx;
};

// Injectable side effects so the state machine is testable without hardware.
struct GateHooks {
  int64_t (*now_ms)(void *ctx);
  void    (*sleep_ms)(void *ctx, int64_t ms);
  bool    (*already_confirmed)(void *ctx);  // wraps boot_is_img_confirmed()
  int     (*confirm)(void *ctx);            // wraps boot_write_img_confirmed(); 0 = ok
  void    (*reboot)(void *ctx);             // wraps sys_reboot(SYS_REBOOT_COLD)
  void   *ctx;
};

struct GateConfig {
  int64_t deadline_ms;       // give up (reboot->revert) after this
  int64_t dwell_ms;          // require continuous health this long before confirming
  int64_t poll_interval_ms;  // spacing between probe sweeps
};

enum class GateOutcome { AlreadyConfirmed, Confirmed, RevertRebooted };

// Runs to a terminal outcome. Pure logic: all time/effects go through hooks.
GateOutcome run_health_gate(const GateConfig &cfg, const GateHooks &hooks,
                            const HealthCriterion *criteria, size_t n_criteria);

}  // namespace boot
```

- [ ] **Step 1: Write the failing tests** in `tests/boot_confirm/src/main.cpp` (ztest; drives the state machine with a fake clock and scripted probes):

```cpp
#include <zephyr/ztest.h>
#include "health_gate.h"

using namespace boot;

// --- Fake environment -------------------------------------------------------
struct FakeEnv {
  int64_t now = 0;
  bool    confirmed_flag = false;   // starts unconfirmed (trial boot)
  int     confirm_calls = 0;
  int     reboot_calls = 0;
};

static int64_t f_now(void *c)               { return static_cast<FakeEnv *>(c)->now; }
static void    f_sleep(void *c, int64_t ms) { static_cast<FakeEnv *>(c)->now += ms; }
static bool    f_confd(void *c)             { return static_cast<FakeEnv *>(c)->confirmed_flag; }
static int     f_confirm(void *c)           { static_cast<FakeEnv *>(c)->confirm_calls++; return 0; }
static void    f_reboot(void *c)            { static_cast<FakeEnv *>(c)->reboot_calls++; }

static GateHooks make_hooks(FakeEnv &e) {
  return GateHooks{f_now, f_sleep, f_confd, f_confirm, f_reboot, &e};
}
static const GateConfig CFG{/*deadline*/ 30000, /*dwell*/ 3000, /*poll*/ 250};

// Scripted probe: healthy once env.now >= threshold.
struct Thresh { FakeEnv *e; int64_t at; };
static bool probe_after(void *c) { auto *t = static_cast<Thresh *>(c); return t->e->now >= t->at; }

ZTEST(health_gate, test_already_confirmed_is_noop) {
  FakeEnv e; e.confirmed_flag = true;
  auto out = run_health_gate(CFG, make_hooks(e), nullptr, 0);
  zassert_equal(out, GateOutcome::AlreadyConfirmed);
  zassert_equal(e.confirm_calls, 0);
  zassert_equal(e.reboot_calls, 0);
}

ZTEST(health_gate, test_all_healthy_confirms_after_dwell) {
  FakeEnv e;
  Thresh t{&e, 0};  // healthy immediately
  HealthCriterion crit{"x", probe_after, &t};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::Confirmed);
  zassert_equal(e.confirm_calls, 1);
  zassert_equal(e.reboot_calls, 0);
  zassert_true(e.now >= CFG.dwell_ms);        // waited the dwell
  zassert_true(e.now < CFG.deadline_ms);
}

ZTEST(health_gate, test_late_but_within_deadline_confirms) {
  FakeEnv e;
  Thresh t{&e, 20000};  // becomes healthy at 20s (radio power-up latency)
  HealthCriterion crit{"sa818", probe_after, &t};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::Confirmed);
  zassert_true(e.now >= 20000 + CFG.dwell_ms);
}

ZTEST(health_gate, test_never_healthy_reverts_at_deadline) {
  FakeEnv e;
  Thresh t{&e, 999999};  // never within deadline
  HealthCriterion crit{"dead", probe_after, &t};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::RevertRebooted);
  zassert_equal(e.confirm_calls, 0);
  zassert_equal(e.reboot_calls, 1);
  zassert_true(e.now >= CFG.deadline_ms);
}

ZTEST(health_gate, test_health_flapping_resets_dwell) {
  // Healthy at t=0, but flaps unhealthy before dwell elapses -> must not confirm early.
  static FakeEnv e;
  struct Flap { int64_t on1, off, on2; };
  static Flap fl{0, 1000, 5000};
  auto probe = +[](void *c) -> bool {
    (void)c;
    int64_t n = e.now;
    return (n >= fl.on1 && n < fl.off) || (n >= fl.on2);
  };
  HealthCriterion crit{"flap", probe, nullptr};
  auto out = run_health_gate(CFG, make_hooks(e), &crit, 1);
  zassert_equal(out, GateOutcome::Confirmed);
  // Confirm only after a continuous dwell starting no earlier than on2.
  zassert_true(e.now >= fl.on2 + CFG.dwell_ms);
}

ZTEST_SUITE(health_gate, NULL, NULL, NULL, NULL, NULL);
```

- [ ] **Step 2: Create the test harness files.**

`tests/boot_confirm/prj.conf`:
```
CONFIG_ZTEST=y
CONFIG_CPP=y
CONFIG_STD_CPP20=y
CONFIG_EXTERNAL_LIBCPP=y
```

`tests/boot_confirm/testcase.yaml`:
```yaml
tests:
  boot_confirm.health_gate:
    platform_allow: native_sim
    tags: boot_confirm mcuboot
```

`tests/boot_confirm/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(boot_confirm_test LANGUAGES CXX)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../app/src/boot_confirm)
target_sources(app PRIVATE
    src/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../app/src/boot_confirm/health_gate.cpp)
```

- [ ] **Step 3: Run the tests to verify they FAIL** (header/impl absent):

Run: `west twister -T tests/boot_confirm -p native_sim/native/64 -v`
Expected: FAIL — `health_gate.h` not found / `run_health_gate` undefined.

- [ ] **Step 4: Write `app/src/boot_confirm/health_gate.h`** — the interface block from "Interfaces" above (namespace `boot`, all declarations), with an `#pragma once` and `#include <cstddef>`, `#include <cstdint>`.

- [ ] **Step 5: Write `app/src/boot_confirm/health_gate.cpp`** — the pure state machine:

```cpp
#include "health_gate.h"

namespace boot {

static bool all_healthy(const HealthCriterion *c, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (!c[i].probe(c[i].ctx)) {
      return false;
    }
  }
  return true;
}

GateOutcome run_health_gate(const GateConfig &cfg, const GateHooks &hooks,
                            const HealthCriterion *criteria, size_t n_criteria) {
  if (hooks.already_confirmed(hooks.ctx)) {
    return GateOutcome::AlreadyConfirmed;
  }

  const int64_t start = hooks.now_ms(hooks.ctx);
  int64_t healthy_since = -1;  // -1 == not currently in a healthy streak

  for (;;) {
    const int64_t now = hooks.now_ms(hooks.ctx);

    if (all_healthy(criteria, n_criteria)) {
      if (healthy_since < 0) {
        healthy_since = now;
      }
      if (now - healthy_since >= cfg.dwell_ms) {
        hooks.confirm(hooks.ctx);
        return GateOutcome::Confirmed;
      }
    } else {
      healthy_since = -1;  // streak broken -> restart dwell
    }

    if (now - start >= cfg.deadline_ms) {
      hooks.reboot(hooks.ctx);
      return GateOutcome::RevertRebooted;
    }

    hooks.sleep_ms(hooks.ctx, cfg.poll_interval_ms);
  }
}

}  // namespace boot
```

- [ ] **Step 6: Run the tests to verify they PASS:**

Run: `west twister -T tests/boot_confirm -p native_sim/native/64 -v`
Expected: PASS — all 5 `health_gate` tests green.

- [ ] **Step 7: clang-format + stage (no commit — freeze):**

```bash
clang-format-18 -i app/src/boot_confirm/health_gate.h app/src/boot_confirm/health_gate.cpp tests/boot_confirm/src/main.cpp
git add app/src/boot_confirm/health_gate.* tests/boot_confirm/
```

---

## Task 7: fm_board wiring — real probes, IWDG, confirm thread

**Files:**
- Create: `app/src/boot_confirm/boot_confirm_fm.cpp`
- Modify: `app/CMakeLists.txt` (add the two boot_confirm sources to the USB/hardware branch)
- Modify: `app/src/main_usb_audio.cpp` (register a USB message callback that records "configured"; start the confirm thread)
- Modify: `app/prj.conf` (IWDG + confirm config)
- Modify: `boards/oe5xrx/fm_board/fm_board.dts` (ensure `iwdg` node enabled) — verify first

**Interfaces:**
- Consumes: `run_health_gate` (Task 6); `sa818_at_connect()` / `device_is_ready()` (`<sa818/sa818.h>`, `<sa818/sa818_at.h>`); `usbd_msg_register_cb` + `USBD_MSG_CONFIGURATION`; `boot_is_img_confirmed()` / `boot_write_img_confirmed()` (`<zephyr/dfu/mcuboot.h>`); `sys_reboot()`; `task_wdt`/`hwinfo` IWDG.
- Produces: `boot_confirm_fm_start(const struct device *sa818, struct usbd_context *usbd)` — spins the confirm thread; and `boot_confirm_fm_usb_msg(...)` to feed USB config events.

- [ ] **Step 1: Verify the IWDG node** exists/can be enabled for fm_board:

Run: `grep -rniE "iwdg|watchdog|wdt" boards/oe5xrx/fm_board/fm_board.dts $ZEPHYR_BASE/dts/arm/st/u5/stm32u575.dtsi | head`
Expected: an `iwdg`/`iwdg1` node is available to set `status = "okay"`. If not present in the board DTS, add an overlay enabling it.

- [ ] **Step 2: Append confirm/IWDG config to `app/prj.conf`:**

```
# --- Health-gated confirm + watchdog (prod build) ---
CONFIG_REBOOT=y
CONFIG_WATCHDOG=y
CONFIG_TASK_WDT=y
```

- [ ] **Step 3: Write `app/src/boot_confirm/boot_confirm_fm.cpp`** — wire real probes to the gate. Only compiled for the hardware/USB build.

```cpp
#include "health_gate.h"

#include <sa818/sa818.h>
#include <sa818/sa818_at.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/usb/usbd.h>

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <zephyr/dfu/mcuboot.h>
#endif

LOG_MODULE_REGISTER(boot_confirm, LOG_LEVEL_INF);

namespace {

struct Env {
  const struct device *sa818;
  volatile bool usb_configured;  // set from the USBD message callback
};

Env g_env;

// --- probes -----------------------------------------------------------------
bool probe_usb(void *c)   { return static_cast<Env *>(c)->usb_configured; }
bool probe_shell(void *c) {
  // Shell transport ready == USB composite configured (CDC-ACM interface is part
  // of it) AND the shell backend is initialized. We deliberately do NOT gate on
  // DTR / a host opening the port.
  (void)c;
  return IS_ENABLED(CONFIG_SHELL);  // shell backend is SYS_INIT-armed by boot
}
bool probe_sa818(void *c) {
  const struct device *d = static_cast<Env *>(c)->sa818;
  if (!device_is_ready(d)) {
    return false;
  }
  return sa818_at_connect(d) == SA818_OK;  // AT+DMOCONNECT roundtrip
}

// --- hooks ------------------------------------------------------------------
int64_t h_now(void *)              { return k_uptime_get(); }
void    h_sleep(void *, int64_t m) { k_msleep(static_cast<int32_t>(m)); }
bool    h_confd(void *) {
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
  return boot_is_img_confirmed();
#else
  return true;  // bare build: nothing to confirm
#endif
}
int h_confirm(void *) {
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
  int rc = boot_write_img_confirmed();
  LOG_INF("image confirmed (rc=%d)", rc);
  return rc;
#else
  return 0;
#endif
}
void h_reboot(void *) {
  LOG_WRN("health gate deadline exceeded -> reboot for MCUboot revert");
  sys_reboot(SYS_REBOOT_COLD);
}

K_THREAD_STACK_DEFINE(g_stack, 2048);
struct k_thread g_thread;

void gate_thread(void *, void *, void *) {
  using namespace boot;
  static HealthCriterion crit[] = {
      {"usb", probe_usb, &g_env},
      {"shell", probe_shell, &g_env},
      {"sa818", probe_sa818, &g_env},
  };
  const GateHooks hooks{h_now, h_sleep, h_confd, h_confirm, h_reboot, nullptr};
  const GateConfig cfg{/*deadline*/ 30000, /*dwell*/ 3000, /*poll*/ 250};
  boot::run_health_gate(cfg, hooks, crit, ARRAY_SIZE(crit));
  // Confirmed or (unreached: reboot). Thread exits after confirm.
}

}  // namespace

extern "C" void boot_confirm_fm_usb_configured(void) { g_env.usb_configured = true; }

extern "C" void boot_confirm_fm_start(const struct device *sa818) {
  g_env.sa818 = sa818;
  g_env.usb_configured = false;
  k_thread_create(&g_thread, g_stack, K_THREAD_STACK_SIZEOF(g_stack), gate_thread,
                  NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
}
```

- [ ] **Step 4: Hook USB config events + start the gate in `main_usb_audio.cpp`.** Register a USBD message callback that flags "configured", and start the confirm thread after `usbd_enable` and after the SA818 device is fetched:

```cpp
// near the top:
extern "C" void boot_confirm_fm_usb_configured(void);
extern "C" void boot_confirm_fm_start(const struct device *sa818);

static void usbd_msg_cb(struct usbd_context *const, const struct usbd_msg *const msg) {
  if (msg->type == USBD_MSG_CONFIGURATION) {
    boot_confirm_fm_usb_configured();
  }
}

// after sample_usbd_init_device(NULL) returns `sample_usbd`, before usbd_enable():
usbd_msg_register_cb(sample_usbd, usbd_msg_cb);

// after `sa818` is fetched and confirmed ready:
boot_confirm_fm_start(sa818);
```

- [ ] **Step 5: Add sources to `app/CMakeLists.txt`** in the `CONFIG_USB_DEVICE_STACK_NEXT` branch:

```cmake
    target_sources(app PRIVATE
        src/main_usb_audio.cpp
        src/usb_audio_bridge.cpp
        src/feedback.cpp
        src/boot_confirm/health_gate.cpp
        src/boot_confirm/boot_confirm_fm.cpp
    )
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/boot_confirm)
```

- [ ] **Step 6: Build prod and bare:**

Run: `west build -b fm_board -p always --sysbuild app`
Expected: SUCCEEDS; `grep CONFIG_TASK_WDT build/app/zephyr/.config` → `=y`.
Run: `west build -b fm_board -p always app`
Expected: SUCCEEDS (confirm code compiles; MCUboot calls stubbed out via the `#if`).

- [ ] **Step 7: clang-format + stage (no commit — freeze):**

```bash
clang-format-18 -i app/src/boot_confirm/boot_confirm_fm.cpp app/src/main_usb_audio.cpp
git add app/src/boot_confirm/boot_confirm_fm.cpp app/src/main_usb_audio.cpp app/CMakeLists.txt app/prj.conf boards/oe5xrx/fm_board/fm_board.dts
```

---

## Task 8: HW M2 = Gate 2 — own key, downgrade + revert (bench + config)

**Files:**
- Create: `keys/oe5xrx-fw-ecdsa-p256.pem` (**git-ignored**, generated, never committed)
- Modify: `.gitignore` (add `keys/`)
- Modify: `app/sysbuild.conf` (point the signing key at the file)

**Interfaces:**
- Consumes: the full prod build (Tasks 2, 4, 7).

- [ ] **Step 1: Generate the production key** (kept out of the repo):

Run: `mkdir -p keys && $ZEPHYR_BASE/../bootloader/mcuboot/scripts/imgtool.py keygen -k keys/oe5xrx-fw-ecdsa-p256.pem -t ecdsa-p256`
Expected: key file created. Add `keys/` to `.gitignore`.

- [ ] **Step 2: Point sysbuild at the key** — in `app/sysbuild.conf`:

```
SB_CONFIG_BOOT_SIGNATURE_KEY_FILE="keys/oe5xrx-fw-ecdsa-p256.pem"
```

- [ ] **Step 3: Rebuild + flash** MCUboot (now embedding the public key) + app signed with the private key:

Run: `west build -b fm_board -p always --sysbuild app && west flash`
Expected: boots (Task 3 procedure) — proves the key pair matches.

- [ ] **Step 4: HW — foreign image is rejected.** Sign a build with the MCUboot dev key, try to DFU it to slot1, reset.
Expected: MCUboot **rejects** the swap (bad signature); slot0 image continues. Record.

- [ ] **Step 5: HW — downgrade is rejected.** With running version 1.0.1, build+sign a 1.0.0 image (own key), DFU to slot1, reset.
Expected: MCUboot **refuses** the older image (downgrade prevention); 1.0.1 continues. Record.

- [ ] **Step 6: HW — auto-revert of a broken image.** Build an image whose SA818 path is deliberately broken (e.g. temporarily point the SA818 UART at an unused pin so `sa818_at_connect` never returns OK), own key, DFU to slot1, reset.
Expected: image boots in test mode, the health gate never satisfies criterion 3, the deadline reboot fires, and MCUboot **reverts** to the previous good 1.0.1. Confirm the good image runs after the revert. Record.

- [ ] **Step 7: Stage `.gitignore` + `app/sysbuild.conf`** (no commit — freeze). **Never** `git add` the key.

---

## Task 9: M3 — CI prod gate + docs

**Files:**
- Modify: `.github/workflows/ci.yml` (add a prod/signed build job)
- Modify: `CLAUDE.md` (document the two build variants, DFU update flow, key handling)

**Interfaces:** none (CI + docs).

- [ ] **Step 1: Read the current CI** to match style/setup (`action-zephyr-setup`, `west patch apply`):

Run: `sed -n '1,120p' .github/workflows/ci.yml`
Expected: identifies the existing `build_and_tests` job and the setup steps to reuse.

- [ ] **Step 2: Add a `prod_build` job** that builds the signed MCUboot image with a **CI dev key** (not the production key — CI signs with the MCUboot default/dev key so the build is verified without exposing the real secret):

```yaml
  prod_build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with: { path: FW-RemoteStation }
      - uses: zephyrproject-rtos/action-zephyr-setup@v1
        with:
          app-path: FW-RemoteStation
          toolchains: arm-zephyr-eabi
      - name: Apply downstream patches
        run: west patch apply
        working-directory: FW-RemoteStation
      - name: Build prod (MCUboot + signed app, dev key)
        run: west build -b fm_board --sysbuild app
        working-directory: FW-RemoteStation
      - name: Assert signed artifacts exist
        run: test -f build/app/zephyr/zephyr.signed.bin && test -f build/mcuboot/zephyr/zephyr.hex
        working-directory: FW-RemoteStation
```

> NOTE: keep `app/sysbuild.conf`'s `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` pointing at the real key for local/release builds; CI overrides it to the dev key via `-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=...` OR the release workflow injects the real key from a secret. Confirm which against `.github/workflows/release.yml` during implementation.

- [ ] **Step 3: Add the `bare` twister/build already covered?** Confirm the existing `build_and_tests` still runs the native_sim build + the new `tests/boot_confirm` suite:

Run: `west twister -T tests/boot_confirm -T tests/sim_shell -T tests/etl -p native_sim/native/64 -v`
Expected: all suites PASS (boot_confirm added).

- [ ] **Step 4: Update `CLAUDE.md`** — add a short "Firmware update / MCUboot" section: two build variants (`app` vs `--sysbuild app`), the DFU flow (`dfu-util --alt <slot1> --download zephyr.signed.bin` → reset → swap), health-gated confirm + auto-revert, and that the signing key lives in git-ignored `keys/` (never committed).

- [ ] **Step 5: clang-format check + stage (no commit — freeze):**

```bash
git add .github/workflows/ci.yml CLAUDE.md
```

---

## Self-Review Notes

- **Spec coverage:** §3 partitions → T1; §4 sysbuild/version → T2; §5 DFU wiring + agent-orchestration/MCUboot-debug notes → T4 (+ prose); §6 health gate → T6 (logic) + T7 (wiring); §6b recovery → **out of scope (M4, separate plan)**, noted; §7 build variants → T2 (verified bare vs prod); §8 testing → T6 (sim) + T3/T5/T8 (HW); §9 milestones → T3 (M0), T5 (M1), T8 (M2), T9 (M3); §10 key mgmt → T8; §11 future work (build wrapper) → untouched by design.
- **HW-only tasks (3, 5, 8):** no automated test possible; each ends with a recorded pass/fail bench procedure and failure-triage hints.
- **Commit freeze:** every task stages only; the `git commit` lines are commented/blocked until the user lifts the freeze.
