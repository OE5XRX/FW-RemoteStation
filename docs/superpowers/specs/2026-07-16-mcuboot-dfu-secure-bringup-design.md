# Secure MCUboot + USB-DFU Bringup (fm_board / STM32U575)

**Status:** Design approved — pending implementation plan
**Date:** 2026-07-16
**Target:** `fm_board` (custom STM32U575xI, 2 MB flash), USB composite (UAC2 + CDC-ACM + DFU) on `USB_DEVICE_STACK_NEXT`
**Branch:** `worktree-mcuboot-dfu-bringup`

## 1. Purpose & Scope

Bring up a **secure, brick-proof firmware update path** for the STM32U575 part of the OE5XRX
Remote Station, using MCUboot as the bootloader and the Zephyr `USBD_DFU` class as the update
transport. Updates are delivered over USB by the CM4 host (the CM4 acts as USB host and pushes
firmware to the STM32 via `dfu-util`). This DFU path *is* the STM32-side OTA channel; it is
separate from the CM4's own Yocto A/B OTA.

### Security objective (threat model — Tier A)

Protect against the **network/OTA attack + fault surface**, software-only in Zephyr/MCUboot:

- Only firmware **signed by OE5XRX** runs on the device (authenticity + integrity).
- **Downgrades are rejected** (no rollback to older, potentially vulnerable firmware).
- A failed or non-functional update **auto-reverts instead of bricking** the station.

**Explicitly out of scope (Tier A):** protecting flash contents against a physical attacker with
a debugger (STM32U5 RDP / WRP / TrustZone / secure boot). Image encryption is not used
(confidentiality of flash is not a goal). These can be layered later without invalidating this
design.

## 2. Guiding Decisions (locked)

| Decision | Choice | Rationale |
|---|---|---|
| Threat tier | A — authentic FW + no brick, SW-only | Matches web-OTA surface without anti-tamper overhead |
| Bringup order | Gate 1 (DFU swap on HW, dev key) **before** hardening | De-risk the unknown (DFU on this UDC) with the real stack in minimal form; no throwaway code |
| Signature algorithm | **ECDSA-P256** | HW-accelerated by the U5 PKA; small; modern MCUboot default |
| Swap strategy | **swap-using-offset** | Current Zephyr default; scratch-less, interrupt-safe rollback, fewer flash writes than move |
| Downgrade prevention | `MCUBOOT_DOWNGRADE_PREVENTION` (version-based) | Blocks re-flashing older firmware |
| Confirm policy | **Health-gated self-confirm** + IWDG | FW confirms itself only after proving it is functionally alive; hung image → watchdog reset → auto-revert. No external coupling; FW stays thin |
| Build variants | `bare` (debug, default) vs `prod` (MCUboot), one switch (`--sysbuild`), rest in files | Simple day-to-day debug; reproducible release build |

## 3. Partition Layout (2 MB flash, redesign)

The current partition table is a copied-from-small-board placeholder: 32 KB slots that cannot
hold the ~150–250 KB composite firmware, and a stray "reserved" 64 KB gap. It is **replaced**.
`scratch` is dropped (swap-using-offset needs no scratch). Sector size = 8 KB (0x2000);
everything sector-aligned.

| Partition | Label | Offset | Size | Purpose |
|---|---|---|---|---|
| `boot_partition` | `mcuboot` | 0x000000 | 128 KB (0x20000) | MCUboot |
| `slot0_partition` | `image-0` | 0x020000 | 944 KB (0xEC000) | primary (running image) |
| `slot1_partition` | `image-1` | 0x10C000 | 944 KB (0xEC000) | secondary (DFU target) |
| `storage_partition` | `storage` | 0x1F8000 | 32 KB (0x8000) | NVS reserve (future/optional) |

Total = 0x200000 (2 MB) exactly. 944 KB per slot ≈ 4× headroom over the current composite
firmware — grows comfortably. `zephyr,code-partition = &slot0_partition` stays set in the board
DTS (see §7 for why this is harmless in the `bare` build).

## 4. Sysbuild + MCUboot Configuration

New files:

- `app/sysbuild.conf` — sysbuild-level config:
  - `SB_CONFIG_BOOTLOADER_MCUBOOT=y` — build MCUboot + signed app together.
  - swap-using-offset mode selection.
  - `MCUBOOT_DOWNGRADE_PREVENTION` enabled.
  - `SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y` and the signing-key path.
- `app/sysbuild/mcuboot.conf` — MCUboot image config fragment (log level, downgrade-only /
  upgrade behavior, any MCUboot-side tuning).

`overwrite-only` was rejected: it provides no rollback, contradicting the "no brick" objective.

## 5. USB-DFU Wiring (application)

`CONFIG_USBD_DFU=y` already exists in `fm_board_defconfig` but is **not wired** in source. Work:

- Add the DFU **flash backend** bound to `slot1` (via `img_mgmt` / `flash_img`), following
  `samples/subsys/usb/dfu` on the new `usbd` stack.
- DFU stays part of the existing composite (UAC2 + CDC-ACM + DFU) in runtime mode; `dfu-util
  --detach` switches to DFU mode; `--alt` selects the slot1 image interface.
- **Do not** set `USB_DFU_PERMANENT_DOWNLOAD` — we want the test/confirm cycle (§6), not an
  immediately-permanent download.

## 6. Confirm / Rollback — the "no brick" core

- **Test boot:** After a DFU download to slot1 and reset, MCUboot swaps and boots the new image
  in test mode (`image_ok` unset).
- **Health-gated self-confirm:** A boot task calls `boot_write_img_confirmed()`
  (`<zephyr/dfu/mcuboot.h>`) **only after** the image proves it is functionally alive —
  minimum: USB enumerated (SET_CONFIGURATION seen) **and** the shell / `module describe` path
  reachable. The exact health predicate is defined in the implementation plan; it must be behind
  an abstraction so the trigger logic is testable on `native_sim` (§8).
- **Independent watchdog (IWDG):** Enabled so an image that boots but hangs *before* confirming
  is reset → because it was never confirmed, MCUboot reverts to slot0 on the next boot.
- **Net guarantee:** An image that fails to boot **or** boots-but-is-non-functional is never
  made permanent.

## 7. Two Build Variants — one switch, everything else in files

The only *hard* difference between a debug and a production build is the code-partition (app at
flash origin 0x0 vs. at slot0 with an MCUboot header). This is handled **automatically** by
Zephyr's Kconfig chain — no `FILE_SUFFIX`, no `-D` arguments:

- `modules/Kconfig.mcuboot`: app `CONFIG_BOOTLOADER_MCUBOOT` **`select USE_DT_CODE_PARTITION`**.
- `Kconfig.zephyr`: `USE_DT_CODE_PARTITION` defaults **`n`** → with MCUboot off, the app links at
  flash origin 0x0 even though `zephyr,code-partition = &slot0_partition` is set (the chosen node
  is simply inactive).
- `share/sysbuild/image_configurations/MAIN_image_default.cmake`: sysbuild sets the app's
  `CONFIG_BOOTLOADER_MCUBOOT` **automatically** to `SB_CONFIG_BOOTLOADER_MCUBOOT`.

Therefore the entire variant difference collapses to *whether sysbuild runs*:

```
west build -b fm_board app                 # bare  : app @ 0x0, no bootloader, unsigned — debug/test (DEFAULT)
west build -b fm_board --sysbuild app       # prod  : MCUboot + signed app @ slot0 — release
```

Files (created once, never re-specified on the CLI):

- Board DTS: `zephyr,code-partition = &slot0_partition` **unconditionally** (inactive when
  MCUboot is off — no `#ifdef`).
- `app/sysbuild.conf`: everything MCUboot/signing/downgrade (see §4).
- `app/prj.conf`: DFU / confirm / IWDG code guarded by `CONFIG_BOOTLOADER_MCUBOOT` → compiled out
  of the `bare` build automatically, keeping debug firmware lean.

Consequences:

- `native_sim` always builds `bare` (no bootloader on the simulator).
- **CI builds both:** `bare` (existing gate) **and** `prod` (new gate — ensures the signed
  MCUboot image stays buildable over time).
- west persists board + `--sysbuild` per build directory, so after first configure of `build/`
  (debug) and `build-prod/` (prod), rebuilds need no extra tokens.

## 8. Testing

- **native_sim:** the health-gate / confirm-trigger logic sits behind an abstraction and is
  unit-/Twister-testable on `native_sim`. (The actual MCUboot swap is HW-only and not simulated.)
- **HW Gate 1 (M1):** `dfu-util` uploads a second image to slot1 → reset → swap → new image
  boots. **DFU verified on real hardware** with the MCUboot dev key.
- **HW Gate 2 (M2):** own ECDSA-P256 key rejects a foreign/dev-key-signed image; a downgrade is
  rejected; a deliberately broken image (e.g. USB never enumerates) triggers **auto-revert** to
  slot0.

## 9. Milestones

- **M0 — MCUboot boots the app on HW.** Partition redesign + sysbuild config; app boots *through
  MCUboot* on hardware (dev key). Establishes the chain.
- **M1 = Gate 1 — DFU swap on HW.** `dfu-util` → slot1 → reset → swap → new image runs. DFU
  proven on this UDC. (Milestone worth a build-in-public log if it lands cleanly.)
- **M2 = Gate 2 — Security hardening.** Own ECDSA-P256 signing key (kept out of the repo),
  downgrade prevention, health-gated self-confirm + IWDG + verified auto-revert.
- **M3 — Tests, docs, CI.** native_sim tests for the health-gate/confirm logic; CI `prod` build
  gate; update `CLAUDE.md` / hardware docs.

## 10. Key Management

- The production ECDSA-P256 signing key is **never committed** to the repo. Exact storage and
  handling (where the private key lives, how CI/release signs, how the public key is embedded in
  MCUboot) is defined in the implementation plan.
- Gate 1 (M0/M1) may use the MCUboot insecure **dev key** to prove mechanics; M2 replaces it.

## 11. Future Work (out of scope this session)

- **Build/test wrapper** around the debug/prod build directories and their arguments (Make or
  another tool) — nice-to-have ergonomics, not required (the `--sysbuild` switch + per-build-dir
  persistence already covers day-to-day use).
- Tier B/C hardening (RDP, WRP, immutable bootloader, TrustZone secure boot, image encryption)
  if the threat model expands.
- Hardware security counter / `MCUBOOT_HW_ROLLBACK_PROT` (needs a non-volatile counter) — a
  stronger-than-version-based downgrade defense, deferred with the hardware tier.

## 12. Open Assumptions (confirmed with user)

- CM4 is the USB host and pushes STM32 firmware via `dfu-util` over USB — **confirmed**.
- Debug-without-MCUboot is the everyday default; prod-with-MCUboot is the explicit release build —
  **confirmed**.
