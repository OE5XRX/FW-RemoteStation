# Firmware Update (MCUboot + USB-DFU)

How the fm_board updates its firmware securely over USB, from three viewpoints:
the **operator** (manual bench/field update), the **station-agent** (automated update
over the network), and the **release/key** side (how signed images are produced).

All procedures below are verified on real STM32U575 hardware.

---

## 1. How it works (model)

The STM32 runs **MCUboot** as an immutable bootloader in the first flash region, plus an
**A/B slot** layout for the application:

| Partition | Address | Size | Role |
|---|---|---|---|
| `boot` (MCUboot) | 0x08000000 | 128 KB | bootloader — flashed once via SWD |
| `slot0` (`image-0`) | 0x08020000 | 896 KB | **primary** — the running image |
| `slot1` (`image-1`) | 0x08100000 | 896 KB | **secondary** — DFU upload target |
| `storage` | 0x081E0000 | 128 KB | NVS reserve |

Update cycle (**swap-using-offset**, no scratch partition):

1. A new **signed** image is written to **slot1** (via USB-DFU).
2. The device is told to upgrade and reset.
3. On boot, MCUboot **verifies slot1** (SHA-256 + **ECDSA-P256 signature** against the public
   key baked into the bootloader) and, if the version is not a downgrade, **swaps** slot1 into
   slot0 and boots it in **test mode**.
4. The new firmware runs a **health gate**; only if it proves itself alive does it *confirm*
   the image. Otherwise MCUboot **auto-reverts** to the previous image.

**What this guarantees**
- Only firmware **signed by the OE5XRX key** ever runs (bad/foreign signature → rejected).
- **No downgrades** — an older version is refused.
- **No brick** — a firmware that fails to boot *or* boots-but-is-non-functional is automatically
  rolled back to the last good image.

### The health gate ("boot passt")

After a swap the new image boots in test mode (`image_ok` unset). A low-priority thread confirms
the image (`boot_write_img_confirmed()`) **only after all three** checks pass, polled for up to
~30 s:

1. **USB enumerated** — host issued SET_CONFIGURATION.
2. **Shell transport ready** — CDC-ACM interface up + shell subsystem initialized (this is a
   firmware-internal readiness flag; it does **not** require a host to open the terminal / assert
   DTR).
3. **SA818 reachable** — `AT+DMOCONNECT` round-trip returns `+DMOCONNECT:0`.

If the checks don't all pass within the deadline, the firmware deliberately reboots; because the
image was never confirmed, MCUboot reverts. An independent watchdog (IWDG, via `task_wdt`) covers
the case where the image hangs before it can even reach the deadline.

---

## 2. Operator view — manual update over USB

Requires `dfu-util` on the host and the board connected via its USB port.

### 2a. One-time provisioning (SWD, via ST-Link + pyocd)

The bootloader is **not** field-updatable over DFU — it is flashed once together with an initial
signed app. This also re-flashes a bricked or blank board.

```bash
# from a --sysbuild build (see §4), flash MCUboot + the signed app:
pyocd flash --target stm32u575citx --erase chip \
    build/mcuboot/zephyr/zephyr.hex \
    build/app/zephyr/zephyr.signed.hex
```

Verify it booted through MCUboot: the app runs from slot0 (PC in the `0x0802xxxx` range) and the
image header magic `0x96f3b83d` is present at `0x08020000`.

### 2b. Normal update (USB-DFU, no cable-swap, no SWD)

```bash
# 1. The board runs the composite (UAC2 + CDC-ACM + DFU) in "runtime" mode.
#    Switch it into DFU mode:
dfu-util -e
#    -> device re-enumerates; `dfu-util -l` now shows alt name="slot1_image".

# 2. Download the SIGNED image to slot1 and reset:
dfu-util -a 0 -D fm-sa818-2m.signed.bin

# 3. Reset the board (power-cycle, or `kernel reboot` on the shell, or `dfu-util -R`).
#    MCUboot swaps slot1 -> slot0 and boots the new image.

# 4. Confirm the update landed (over the CDC-ACM shell):
#    the `version` command reports the new APP-VERSION.
```

Notes:
- Use the **`.signed.bin`** asset — a raw/bare `.bin` will be rejected by MCUboot.
- If the version does **not** change after reset, MCUboot rejected the image (bad signature or a
  downgrade) — this is the security working as designed.
- If a functionally-broken image is flashed, it runs for ~30 s then auto-reverts; the shell will
  report the **previous** version again.

---

## 3. Station-agent view — automated network update

The **CM4 is the USB host**; the station-agent drives the STM32 update programmatically. This is
the STM32-side OTA path and is independent of the CM4's own Yocto A/B OTA.

Recommended sequence (the agent owns orchestration; the firmware stays thin):

1. **Fetch** the target's signed image (`<target>.signed.bin`) + its `SHA256SUMS`/cosign bundle
   from the GitHub release; verify the download.
2. **Quiesce** the station — stop audio streaming / ensure not transmitting (the firmware does
   **not** enforce an idle precondition; the agent must).
3. **Enter DFU mode** — send the DFU detach (e.g. `dfu-util -e`, or the equivalent USB control
   request); wait for the device to re-enumerate exposing alt `slot1_image`.
4. **Download** the image to slot1 (`dfu-util -a 0 -D <target>.signed.bin`). On completion the
   firmware marks slot1 for upgrade (`boot_request_upgrade`).
5. **Reset** the STM32 (shell `kernel reboot`, or `dfu-util -R`, or a USB re-plug).
6. **Verify** after re-enumeration: query the shell `version`. Expected = the new version.
   - Version changed → swap accepted.
   - Version unchanged → **rejected** (foreign signature or downgrade). Do not retry blindly; the
     image is not authentic/eligible.
7. **Confirm stability** — wait past the health-gate deadline (~40 s) and re-check `version`. If
   it is **still** the new version, the health gate confirmed it (permanent). If it **reverted**
   to the old version, the new image booted but failed its self-check — treat as a failed update
   and keep the old firmware.

The agent never needs the signing key — it only ships an already-signed image. Authenticity is
enforced on-device by MCUboot.

---

## 4. Build variants

| Variant | Command | Output | Use |
|---|---|---|---|
| **bare** (debug) | `west build -b fm_board app` | `build/zephyr/zephyr.bin` (@0x0, no bootloader, unsigned) | dev/debug; direct SWD flash; **not** DFU-capable |
| **prod** (signed) | `west build -b fm_board --sysbuild app` | `build/app/zephyr/zephyr.signed.bin` + `build/mcuboot/zephyr/zephyr.hex` | production; DFU + provisioning |

The only switch is `--sysbuild`; everything else lives in files (`app/sysbuild.conf`,
`app/sysbuild/app.conf`). Before a `--sysbuild` build, run `west packages pip --install` once so
`imgtool` (MCUboot signing) is available.

---

## 5. Signing key & release integration

### 5.1 The key model

- MCUboot uses an **ECDSA-P256** key pair. The **public** key is compiled into the bootloader;
  the **private** key signs each application image (`imgtool`, run automatically by sysbuild).
- The bootloader on a device trusts exactly the public key it was built with. Therefore **every**
  app image ever DFU-updated onto that device must be signed with the **same** private key.
- **Key rotation is expensive**: changing the key means rebuilding MCUboot with the new public
  key and re-flashing every bootloader **via SWD** (the bootloader is not field-updatable). Treat
  the production key as long-lived and guard it accordingly.

### 5.2 Where the key is configured

Selected in `app/sysbuild.conf`:

```
SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y
# SB_CONFIG_BOOT_SIGNATURE_KEY_FILE is intentionally NOT set here.
```

- **When `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` is unset** (the committed default), MCUboot uses its
  bundled **development** key (`root-ec-p256.pem`). This is what **CI and local dev builds** use —
  convenient, reproducible, and **not secret**. Never ship a dev-key-signed image to the field.
- **For production**, point it at the real key **without committing the key**, by passing it on
  the build command line: `-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="/abs/path/to/oe5xrx-fw.pem"`.
  `keys/` is git-ignored for exactly this.

Generate the production key once (keep it offline/secured):

```bash
python3 bootloader/mcuboot/scripts/imgtool.py keygen \
    -k keys/oe5xrx-fw-ecdsa-p256.pem -t ecdsa-p256
```

### 5.3 Binding it into the GitHub release flow

**Current gap:** `.github/workflows/release.yml` builds the **bare** app
(`west build -b "$board" app`, no `--sysbuild`) and ships `zephyr.bin` — unsigned and without a
bootloader. That asset is **not** DFU-deployable. To ship secure DFU firmware the release must
build the **`--sysbuild`** variant signed with the production key.

Setup (once):
1. Store the private key PEM as a secret — recommended at the **OE5XRX org** level so all release
   workflows share it: `MCUBOOT_SIGNING_KEY_ECDSA_P256` = full PEM contents.

Release-workflow changes (per real firmware target):
```yaml
# after "Setup Zephyr":
- name: Install MCUboot signing deps
  working-directory: fw
  run: west packages pip --install     # provides imgtool

- name: Materialize signing key
  run: |
    umask 077
    printf '%s' "${{ secrets.MCUBOOT_SIGNING_KEY_ECDSA_P256 }}" > "$RUNNER_TEMP/oe5xrx-fw.pem"

# replace the firmware build with a signed sysbuild build:
- name: Build signed DFU firmware
  working-directory: fw
  run: |
    west build -b "$board" --sysbuild app -p always -- \
      -DEXTRA_DTC_OVERLAY_FILE="$overlay" \
      -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="$RUNNER_TEMP/oe5xrx-fw.pem"
    cp build/app/zephyr/zephyr.signed.bin   "release/out/${name}.signed.bin"   # DFU asset
    cp build/mcuboot/zephyr/zephyr.hex      "release/out/${name}.mcuboot.hex"  # provisioning
    # optional: also keep the bare zephyr.elf for debugging
```

Then the existing SHA256SUMS + cosign-keyless signing already cover the new assets (cosign signs
the *files* for supply-chain provenance; that is separate from — and complementary to — the
MCUboot **image** signature that the bootloader verifies on-device).

Result per target:
- `<target>.signed.bin` — the DFU update asset (operators / the station-agent upload this).
- `<target>.mcuboot.hex` — for one-time SWD provisioning of new boards.

> Never delete the key or lose the secret: without it you cannot sign updates that existing
> field bootloaders will accept, and recovery requires SWD access to every device.
