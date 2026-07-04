# FW-RemoteStation Release-Prozess Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Per CLAUDE.md: **forge** for CI/build/workflow, **rhythm** for Zephyr config/static/app-VERSION. Two-stage review (atlas spec-compliance → audit code-quality) after each task.

> **Scope note:** This plan covers **FW-RemoteStation only**. The linux-image consumption side (pin recipes, `pin-*.sh`, bundle, preflight) from spec §9 is handled in a **separate session/PR** and is intentionally out of scope here.

**Goal:** Give FW-RemoteStation a one-button, signed, date-versioned release process that publishes board-named artifacts (real `.bin`/`.elf` + static `native_sim`) for both FM variants (2m/70cm).

**Architecture:** A manual `workflow_dispatch` "Release" job computes the next `YY.MM.DD-NN`, stamps it into Zephyr's native `app/VERSION`, iterates a declarative `release-targets.yaml`, builds each target (real via `fm_board`, sim via `native_sim` + static link + band overlay), runs a describe/version/static smoke, cosign-keyless-signs every asset, emits `SHA256SUMS`, then commits the stamp, tags, and publishes a GitHub Release.

**Tech Stack:** Zephyr RTOS (west, `app/VERSION` → `app_version.h`), STM32U575 (`fm_board`) + `native_sim/native/64`, GitHub Actions, cosign keyless (Sigstore/OIDC).

## Global Constraints

- **One repo, one branch, one PR.** FW-RemoteStation → `feature/fw-release-process` (PR body: `Closes #48`, spec rides along). Worktree already created at `/home/pbuchegger/OE5XRX/FW-RemoteStation/.worktrees/fw-release-process`.
- **Version scheme:** `YY.MM.DD-NN`, repo-wide, 2-digit year (e.g. `26.07.04-01`). Fields map to `app/VERSION`: `VERSION_MAJOR=YY`, `VERSION_MINOR=MM`, `PATCHLEVEL=DD`, `VERSION_TWEAK=NN`. All ≤255. **Numeric fields written WITHOUT leading zeros** (e.g. `VERSION_MINOR = 7`, not `07`) — Zephyr/CMake parse them as decimal ints. Zero-padding is a **display-only** concern in the runtime `version` command.
- **Version computed ONCE per release** and fed to tag, asset names, and `app/VERSION` from the same source (risk mitigation §11).
- **Targets are declarative** in `release-targets.yaml` — a new board = one entry.
- **Shipped == tested:** `CONFIG_MODULE_SA818` lives in `app/prj.conf`, so the released binary answers `module fm describe`, not only the Twister suite.
- **Static native_sim:** primary mechanism `CONFIG_NATIVE_SIMULATOR_STATIC_LINKING=y` (Zephyr adds `-static` to the native link — confirmed in `boards/native/common/natsim_config.cmake`). Fallback (b) musl per spec §6. `ldd <asset>` MUST report `not a dynamic executable`.
- **Signing:** cosign keyless `sign-blob --bundle`, one `.bundle` per asset + a `SHA256SUMS` manifest — mirrors linux-image `release.yml`.
- **Variant mechanism:** the `sa818` node's `band` property (enum `vhf`/`uhf`) is a build-time constant. Both `fm_board.dts` and `app/boards/native_sim_native_64.overlay` default `band = "vhf"`; the 70cm target applies an **additive** `EXTRA_DTC_OVERLAY_FILE` that sets `band = "uhf"`.
- **Asset names (from the `name` field):** `fm-sa818-2m.{bin,elf,native_sim}`, `fm-sa818-70cm.{bin,elf,native_sim}`, plus `SHA256SUMS`.
- **Out of scope:** linux-image pin/bundle/preflight (separate session), FW-OTA via station-manager, on-device secure boot, additional boards (HF).

---

All paths below are relative to the worktree root `/home/pbuchegger/OE5XRX/FW-RemoteStation/.worktrees/fw-release-process`.

## File Structure

- `app/prj.conf` — **modify**: enable `CONFIG_MODULE` + `CONFIG_MODULE_SA818` (describe on shipped binary).
- `app/src/version_shell.cpp` — **create**: `version` shell command printing the stamped release version from `app_version.h`.
- `app/CMakeLists.txt` — **modify**: add `version_shell.cpp` to app sources.
- `app/VERSION` — **modify at release time** by the workflow (committed default stays as a valid file).
- `release/overlays/band_vhf.overlay`, `release/overlays/band_uhf.overlay` — **create**: additive band selectors.
- `release/native_sim_static.conf` — **create**: `CONFIG_NATIVE_SIMULATOR_STATIC_LINKING=y` (+ doc comment).
- `release/release-targets.yaml` — **create**: declarative target manifest.
- `scripts/release_smoke.sh` — **create**: static-check + `version` + `module fm describe` smoke against a native_sim asset.
- `.github/workflows/release.yml` — **create**: the one-button release job.

---

### Task 1: Ship the module platform in the app binary

**Files:**
- Modify: `app/prj.conf` (append a Module Platform block)

**Interfaces:**
- Produces: the `app` native_sim/real binary registers the `module` shell root command; `module fm describe` emits `MODULE-DESCRIBE {…}` with `identity.type=fm_transceiver`, `identity.version=<band>`.

- [ ] **Step 1: Write the failing check.** Build the app for native_sim and drive it:

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation/.worktrees/fw-release-process
west build -b native_sim/native/64 app -p always
printf 'module fm describe\n' | timeout 15 ./build/zephyr/zephyr.exe -uart_stdinout 2>&1 | tee /tmp/t1.out
grep -q 'MODULE-DESCRIBE.*"type":"fm_transceiver"' /tmp/t1.out && echo T1-PASS || echo T1-FAIL
```

- [ ] **Step 2: Verify it FAILs.** Expected: `T1-FAIL` — the shipped `app/prj.conf` has `CONFIG_SA818` but not `CONFIG_MODULE`, so the `module` command is absent.

- [ ] **Step 3: Enable the module platform.** Append to `app/prj.conf`:

```conf

# =============================================================================
# Module Platform (self-describing capability interface)
# =============================================================================
# Ship the SAME interface the Twister suite tests: `module fm describe` must
# answer on the shipped app binary, not only in tests/sim_shell. MODULE_SA818
# depends on SA818 (enabled above) and selects CBPRINTF_FP_SUPPORT (needed for
# the %.4f floats in MODULE-RESULT).
CONFIG_MODULE=y
CONFIG_MODULE_SA818=y
```

- [ ] **Step 4: Verify it PASSES.** Re-run Step 1. Expected: `T1-PASS`, and the describe line contains `"version":"vhf"` (native_sim overlay default band).

- [ ] **Step 5: Confirm Twister still green** (config parity):

```bash
west twister -T tests/sim_shell -p native_sim/native/64 -v --inline-logs
```
Expected: PASS (unchanged — sim_shell already carried these symbols).

- [ ] **Step 6: Commit.**
```bash
git add app/prj.conf
git commit -m "app: ship module platform (CONFIG_MODULE_SA818) in shipped binary"
```

---

### Task 2: Runtime `version` command from `app/VERSION`

**Files:**
- Create: `app/src/version_shell.cpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: Zephyr-generated `<zephyr/app_version.h>` macros `APP_VERSION_MAJOR`, `APP_VERSION_MINOR`, `APP_PATCHLEVEL`, `APP_TWEAK` (from `app/VERSION` fields `VERSION_MAJOR/VERSION_MINOR/PATCHLEVEL/VERSION_TWEAK`). Confirmed generated at `${build}/zephyr/include/generated/zephyr/app_version.h`.
- Produces: shell command `version` → prints exactly one line `APP-VERSION %02u.%02u.%02u-%02u`. With the default committed `app/VERSION` (1.0.0.0) that is `APP-VERSION 01.00.00-00`.

- [ ] **Step 1: Write the failing check.**
```bash
west build -b native_sim/native/64 app -p always
printf 'version\n' | timeout 15 ./build/zephyr/zephyr.exe -uart_stdinout 2>&1 | tee /tmp/t2.out
grep -Eq 'APP-VERSION [0-9]{2}\.[0-9]{2}\.[0-9]{2}-[0-9]{2}' /tmp/t2.out && echo T2-PASS || echo T2-FAIL
```
- [ ] **Step 2: Verify it FAILs.** Expected: `T2-FAIL` — no `version` command yet (shell prints `command not found`).

- [ ] **Step 3: Create `app/src/version_shell.cpp`:**
```cpp
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * `version` shell command — reports the stamped firmware release version.
 * The release job writes app/VERSION as MAJOR=YY MINOR=MM PATCHLEVEL=DD
 * TWEAK=NN, so the runtime string matches the release tag and asset names
 * exactly. Zero-padding is display-only; app/VERSION stores plain ints.
 */
#include <zephyr/shell/shell.h>
#include <zephyr/app_version.h>

static int cmd_version(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "APP-VERSION %02u.%02u.%02u-%02u",
		    APP_VERSION_MAJOR, APP_VERSION_MINOR,
		    APP_PATCHLEVEL, APP_TWEAK);
	return 0;
}

SHELL_CMD_REGISTER(version, NULL,
		   "Print the stamped firmware release version (YY.MM.DD-NN)",
		   cmd_version);
```

- [ ] **Step 4: Wire it into the build.** In `app/CMakeLists.txt`, directly after the `target_sources(app PRIVATE src/main.cpp)` line, add:
```cmake
target_sources(app PRIVATE src/version_shell.cpp)
```

- [ ] **Step 5: Verify it PASSES.** Re-run Step 1. Expected: `T2-PASS` with line `APP-VERSION 01.00.00-00`.

- [ ] **Step 6: Commit.**
```bash
git add app/src/version_shell.cpp app/CMakeLists.txt
git commit -m "app: add \`version\` shell command reporting stamped release version"
```

---

### Task 3: Static native_sim + combined release smoke

**Files:**
- Create: `release/native_sim_static.conf`
- Create: `scripts/release_smoke.sh`

**Interfaces:**
- Consumes: Task 1 (`module fm describe`), Task 2 (`version`). Native_sim built with `-DEXTRA_CONF_FILE=release/native_sim_static.conf`.
- Produces: `scripts/release_smoke.sh <binary> <expected_version> <expected_band>` — exit 0 iff the binary is static AND reports `<expected_version>` AND describes `fm_transceiver`/`<expected_band>`. Reused verbatim by `release.yml`.

- [ ] **Step 1: Create `release/native_sim_static.conf`:**
```conf
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Static-link the native_sim executable so the D2 simulation layer can run it
# under any target glibc (Yocto scarthgap) without container gymnastics.
# Zephyr's native_simulator honours this by appending `-static` to the host
# link (boards/native/common/natsim_config.cmake). Applied ONLY to release
# native_sim builds via EXTRA_CONF_FILE, so normal CI native_sim builds are
# unaffected.
#
# Link path (verified): (a) -static against host glibc. If a future toolchain
# cannot static-link glibc, fall back to (b) musl per spec section 6.
CONFIG_NATIVE_SIMULATOR_STATIC_LINKING=y
```

- [ ] **Step 2: Create `scripts/release_smoke.sh`** (mode 0755):
```bash
#!/usr/bin/env bash
#
# Smoke-test a released native_sim FM artifact:
#   1. it is statically linked (ldd -> "not a dynamic executable")
#   2. `version` reports the expected YY.MM.DD-NN release version
#   3. `module fm describe` emits fm_transceiver with the expected band
#
# Usage: scripts/release_smoke.sh <native_sim_binary> <expected_version> <expected_band>
set -euo pipefail

bin=${1:?binary path required}
expected_ver=${2:?expected version required}     # e.g. 26.07.04-01
expected_band=${3:?expected band required}       # vhf | uhf

fail() { echo "::error::release_smoke: $*" >&2; exit 1; }

[ -x "$bin" ] || fail "$bin is not executable"

# 1. Static check.
ldd_out=$(ldd "$bin" 2>&1 || true)
printf '%s\n' "$ldd_out" | grep -q "not a dynamic executable" \
	|| fail "$bin is not statically linked: ${ldd_out}"

# 2+3. Drive the sim over stdio. timeout guards the EOF-hang; || true swallows
# the timeout's 124 so we can inspect whatever it printed.
out=$(printf 'version\nmodule fm describe\n' | timeout 15 "$bin" -uart_stdinout 2>&1 || true)

printf '%s\n' "$out" | grep -qF "APP-VERSION ${expected_ver}" \
	|| fail "version mismatch; expected APP-VERSION ${expected_ver}. Got:\n${out}"

desc=$(printf '%s\n' "$out" | grep -o 'MODULE-DESCRIBE {.*}' | head -1)
[ -n "$desc" ] || fail "no MODULE-DESCRIBE line. Got:\n${out}"
printf '%s\n' "$desc" | grep -q '"type":"fm_transceiver"' \
	|| fail "identity.type != fm_transceiver: ${desc}"
printf '%s\n' "$desc" | grep -q "\"version\":\"${expected_band}\"" \
	|| fail "identity.version != ${expected_band}: ${desc}"

echo "OK — ${bin}: static, version ${expected_ver}, band ${expected_band}"
```

- [ ] **Step 3: Verify the smoke FAILs on a non-static build** (proves the static check bites):
```bash
west build -b native_sim/native/64 app -p always
./scripts/release_smoke.sh ./build/zephyr/zephyr.exe 01.00.00-00 vhf; echo "exit=$?"
```
Expected: non-zero exit with `not statically linked`.

- [ ] **Step 4: Build static and verify smoke PASSES:**
```bash
west build -b native_sim/native/64 app -p always -- -DEXTRA_CONF_FILE=$PWD/release/native_sim_static.conf
ldd ./build/zephyr/zephyr.exe   # expect: "not a dynamic executable"
./scripts/release_smoke.sh ./build/zephyr/zephyr.exe 01.00.00-00 vhf; echo "exit=$?"
```
Expected: `ldd` reports `not a dynamic executable`; smoke prints `OK …`; `exit=0`.
> If (a) static-glibc link fails: install static libs (`sudo apt-get install -y libc6-dev`) and retry; if still failing, switch to fallback (b) musl per spec §6 and document the winning config in `release/native_sim_static.conf`.

- [ ] **Step 5: Commit.**
```bash
git add release/native_sim_static.conf scripts/release_smoke.sh
git commit -m "release: static native_sim conf + release_smoke.sh (static/version/describe)"
```

---

### Task 4: Band-variant overlays (2m/70cm)

**Files:**
- Create: `release/overlays/band_vhf.overlay`
- Create: `release/overlays/band_uhf.overlay`

**Interfaces:**
- Consumes: the `sa818` nodelabel present in both `fm_board.dts` and `app/boards/native_sim_native_64.overlay`.
- Produces: additive overlays selecting the band. Applied via `-DEXTRA_DTC_OVERLAY_FILE=<abs path>` (additive — keeps the auto-applied native_sim board overlay).

- [ ] **Step 1: Create `release/overlays/band_vhf.overlay`:**
```dts
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Release variant selector: 2m / VHF (SA818-V, 134-174 MHz).
 */
&sa818 {
	band = "vhf";
};
```

- [ ] **Step 2: Create `release/overlays/band_uhf.overlay`:**
```dts
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Release variant selector: 70cm / UHF (SA818-U, 400-480 MHz).
 */
&sa818 {
	band = "uhf";
};
```

- [ ] **Step 3: Verify uhf overlay flips the band** (this is the failing→passing check — default is vhf):
```bash
west build -b native_sim/native/64 app -p always \
  -- -DEXTRA_DTC_OVERLAY_FILE=$PWD/release/overlays/band_uhf.overlay \
     -DEXTRA_CONF_FILE=$PWD/release/native_sim_static.conf
./scripts/release_smoke.sh ./build/zephyr/zephyr.exe 01.00.00-00 uhf; echo "exit=$?"
```
Expected: `exit=0` — describe now reports `"version":"uhf"` and `"model":"SA818-U"`.

- [ ] **Step 4: Verify vhf overlay keeps vhf** (regression guard):
```bash
west build -b native_sim/native/64 app -p always \
  -- -DEXTRA_DTC_OVERLAY_FILE=$PWD/release/overlays/band_vhf.overlay \
     -DEXTRA_CONF_FILE=$PWD/release/native_sim_static.conf
./scripts/release_smoke.sh ./build/zephyr/zephyr.exe 01.00.00-00 vhf; echo "exit=$?"
```
Expected: `exit=0`.

- [ ] **Step 5: Sanity-build the REAL target for both bands** (no smoke — no sim shell on hardware; just confirm it compiles + links):
```bash
west build -b fm_board app -p always -- -DEXTRA_DTC_OVERLAY_FILE=$PWD/release/overlays/band_vhf.overlay
test -f build/zephyr/zephyr.bin && test -f build/zephyr/zephyr.elf && echo REAL-VHF-OK
west build -b fm_board app -p always -- -DEXTRA_DTC_OVERLAY_FILE=$PWD/release/overlays/band_uhf.overlay
test -f build/zephyr/zephyr.bin && test -f build/zephyr/zephyr.elf && echo REAL-UHF-OK
```
Expected: `REAL-VHF-OK` and `REAL-UHF-OK`.

- [ ] **Step 6: Commit.**
```bash
git add release/overlays/band_vhf.overlay release/overlays/band_uhf.overlay
git commit -m "release: band-variant DT overlays (vhf/uhf) for 2m/70cm targets"
```

---

### Task 5: Declarative target manifest

**Files:**
- Create: `release/release-targets.yaml`

**Interfaces:**
- Produces: the target list the workflow iterates (`.targets[]` with `name`, `board`, `band`, `artifacts`). Parsed with `yq`.

- [ ] **Step 1: Create `release/release-targets.yaml`:**
```yaml
# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Declarative release targets (spec Ansatz A). One entry per board/variant.
# The release workflow iterates this list. A new board = one entry here.
#   name      -> asset basename (fm-sa818-2m -> fm-sa818-2m.bin/.elf/.native_sim)
#   board     -> west -b <board>
#   band      -> selects release/overlays/band_<band>.overlay (vhf|uhf)
#   artifacts -> firmware (real .bin/.elf) and/or native_sim (static .native_sim)
targets:
  - name: fm-sa818-2m
    board: fm_board
    band: vhf
    artifacts: [firmware, native_sim]
  - name: fm-sa818-70cm
    board: fm_board
    band: uhf
    artifacts: [firmware, native_sim]
```

- [ ] **Step 2: Verify it parses and is well-formed:**
```bash
yq -o=json '.targets' release/release-targets.yaml
yq -e '.targets | length == 2' release/release-targets.yaml && echo MANIFEST-OK
# every target has a matching band overlay:
for b in $(yq -r '.targets[].band' release/release-targets.yaml); do
  test -f "release/overlays/band_${b}.overlay" || { echo "MISSING band_${b}.overlay"; exit 1; }
done; echo OVERLAYS-OK
```
Expected: JSON dump, `MANIFEST-OK`, `OVERLAYS-OK`.

- [ ] **Step 3: Commit.**
```bash
git add release/release-targets.yaml
git commit -m "release: declarative release-targets.yaml (fm-sa818-2m + 70cm)"
```

---

### Task 6: One-button Release workflow

**Files:**
- Create: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: Tasks 1–5 (module config, version command, static conf, band overlays, manifest, smoke script).
- Produces: on `workflow_dispatch`, a git tag `YY.MM.DD-NN`, a commit stamping `app/VERSION`, and a signed GitHub Release with `fm-sa818-2m.{bin,elf,native_sim}`, `fm-sa818-70cm.{bin,elf,native_sim}`, each `*.bundle`, and `SHA256SUMS`. `dry_run=true` builds + smokes without committing/tagging/publishing.

- [ ] **Step 1: Create `.github/workflows/release.yml`:**
```yaml
name: Release

# One-button manual release. Computes the next YY.MM.DD-NN, stamps app/VERSION,
# iterates release-targets.yaml (real + static native_sim), smoke-tests, cosign
# keyless-signs, emits SHA256SUMS, then commits the stamp + tags + publishes.
# dry_run builds + smokes everything but skips commit/tag/publish.
on:
  workflow_dispatch:
    inputs:
      dry_run:
        description: "Build + smoke only; do not commit/tag/publish"
        type: boolean
        default: false

permissions:
  contents: write     # push stamp commit + tag, create release
  id-token: write     # cosign keyless OIDC

concurrency:
  group: fw-release
  cancel-in-progress: false

jobs:
  release:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          path: fw
          fetch-depth: 0        # need tags to compute NN
          persist-credentials: true

      - name: Compute version
        id: ver
        working-directory: fw
        run: |
          set -euo pipefail
          git fetch --tags --quiet
          today=$(date -u +%y.%m.%d)                 # e.g. 26.07.04
          n=$(git tag -l "${today}-*" | wc -l | tr -d ' ')
          nn=$(printf '%02d' $((n + 1)))
          version="${today}-${nn}"
          IFS=. read -r yy mm dd <<< "$today"
          {
            echo "version=${version}"
            echo "major=$((10#$yy))"
            echo "minor=$((10#$mm))"
            echo "patch=$((10#$dd))"
            echo "tweak=$((10#$nn))"
          } >> "$GITHUB_OUTPUT"
          echo "Release version: ${version}"

      - name: Stamp app/VERSION
        working-directory: fw
        env:
          MAJOR: ${{ steps.ver.outputs.major }}
          MINOR: ${{ steps.ver.outputs.minor }}
          PATCH: ${{ steps.ver.outputs.patch }}
          TWEAK: ${{ steps.ver.outputs.tweak }}
        run: |
          set -euo pipefail
          cat > app/VERSION <<EOF
          VERSION_MAJOR = ${MAJOR}
          VERSION_MINOR = ${MINOR}
          PATCHLEVEL = ${PATCH}
          VERSION_TWEAK = ${TWEAK}
          EXTRAVERSION =
          EOF
          cat app/VERSION

      - name: Setup Zephyr
        uses: zephyrproject-rtos/action-zephyr-setup@v1
        with:
          app-path: fw
          toolchains: arm-zephyr-eabi

      - name: Install static link deps
        run: sudo apt-get update && sudo apt-get install -y libc6-dev

      - name: Install cosign
        if: ${{ !inputs.dry_run }}
        uses: sigstore/cosign-installer@v3

      - name: Build all targets
        working-directory: fw
        env:
          VERSION: ${{ steps.ver.outputs.version }}
        run: |
          set -euo pipefail
          mkdir -p release/out
          count=$(yq -r '.targets | length' release/release-targets.yaml)
          for i in $(seq 0 $((count - 1))); do
            name=$(yq -r ".targets[$i].name"  release/release-targets.yaml)
            board=$(yq -r ".targets[$i].board" release/release-targets.yaml)
            band=$(yq -r ".targets[$i].band"  release/release-targets.yaml)
            overlay="$PWD/release/overlays/band_${band}.overlay"
            arts=$(yq -r ".targets[$i].artifacts[]" release/release-targets.yaml)
            echo "== ${name} (board=${board} band=${band}) =="

            for art in $arts; do
              if [ "$art" = "firmware" ]; then
                west build -b "$board" app -p always -- -DEXTRA_DTC_OVERLAY_FILE="$overlay"
                cp build/zephyr/zephyr.bin "release/out/${name}.bin"
                cp build/zephyr/zephyr.elf "release/out/${name}.elf"
              elif [ "$art" = "native_sim" ]; then
                west build -b native_sim/native/64 app -p always -- \
                  -DEXTRA_DTC_OVERLAY_FILE="$overlay" \
                  -DEXTRA_CONF_FILE="$PWD/release/native_sim_static.conf"
                cp build/zephyr/zephyr.exe "release/out/${name}.native_sim"
                ./scripts/release_smoke.sh "release/out/${name}.native_sim" "$VERSION" "$band"
              fi
            done
          done
          ls -lh release/out/

      - name: Generate SHA256SUMS
        working-directory: fw/release/out
        run: |
          set -euo pipefail
          sha256sum * > SHA256SUMS
          cat SHA256SUMS

      - name: Sign assets (cosign keyless)
        if: ${{ !inputs.dry_run }}
        working-directory: fw/release/out
        run: |
          set -euo pipefail
          for f in $(ls | grep -v '\.bundle$'); do
            cosign sign-blob --yes --bundle "${f}.bundle" "${f}"
          done
          ls -lh

      - name: Commit stamp, tag, and push
        if: ${{ !inputs.dry_run }}
        working-directory: fw
        env:
          VERSION: ${{ steps.ver.outputs.version }}
        run: |
          set -euo pipefail
          git config user.name  "github-actions[bot]"
          git config user.email "github-actions[bot]@users.noreply.github.com"
          git add app/VERSION
          git commit -m "release: FW ${VERSION}"
          git tag -a "${VERSION}" -m "FW-RemoteStation ${VERSION}"
          git push origin HEAD:"${GITHUB_REF_NAME}"
          git push origin "${VERSION}"

      - name: Create GitHub Release
        if: ${{ !inputs.dry_run }}
        uses: softprops/action-gh-release@v3
        with:
          tag_name: ${{ steps.ver.outputs.version }}
          files: |
            fw/release/out/*
          generate_release_notes: true
          draft: false
          prerelease: false
          body: |
            ## FW-RemoteStation ${{ steps.ver.outputs.version }}

            | Target | Real (DFU) | Debug | Sim (static) |
            |---|---|---|---|
            | fm-sa818-2m (VHF/2m) | `fm-sa818-2m.bin` | `fm-sa818-2m.elf` | `fm-sa818-2m.native_sim` |
            | fm-sa818-70cm (UHF/70cm) | `fm-sa818-70cm.bin` | `fm-sa818-70cm.elf` | `fm-sa818-70cm.native_sim` |

            Each asset ships a cosign `.bundle` signature; `SHA256SUMS` covers all.
            The firmware reports this version at runtime via the `version` shell command.
```

- [ ] **Step 2: Lint the workflow YAML locally:**
```bash
yq -e '.on.workflow_dispatch' .github/workflows/release.yml >/dev/null && echo WF-YAML-OK
```
Expected: `WF-YAML-OK`. (Full run is validated via the `workflow_dispatch` dry-run in Task 7.)

- [ ] **Step 3: Commit.**
```bash
git add .github/workflows/release.yml
git commit -m "ci: one-button FW release workflow (stamp/build/smoke/sign/publish)"
```

---

### Task 7: End-to-end dry-run verification (superpowers:verification-before-completion)

**Files:** none (verification only)

- [ ] **Step 1: Push the branch and trigger a dry-run:**
```bash
git push -u origin feature/fw-release-process
gh workflow run release.yml --ref feature/fw-release-process -f dry_run=true
```

- [ ] **Step 2: Watch it and confirm green:**
```bash
run=$(gh run list --workflow=release.yml --branch feature/fw-release-process --limit 1 --json databaseId -q '.[0].databaseId')
gh run watch "$run" --exit-status
```
Expected: success. In the "Build all targets" log, four builds complete and both native_sim smokes print `OK — …: static, version <computed>, band vhf|uhf`. `SHA256SUMS` lists 6 assets (2×.bin, 2×.elf, 2×.native_sim). No tag/commit/release created (dry-run).

- [ ] **Step 3: Confirm no side effects from dry-run:**
```bash
git ls-remote --tags origin | grep -E "$(date -u +%y.%m.%d)-" && echo "UNEXPECTED TAG" || echo "NO-TAG-OK"
```
Expected: `NO-TAG-OK`.

> The real (non-dry) release is cut AFTER the PR merges (Task 8), so the published tag lands on `main`.

---

### Task 8: PR + copilot-loop + cut the first real release

- [ ] **Step 1: Open the PR** (spec rides along on this branch):
```bash
gh pr create --repo OE5XRX/FW-RemoteStation --base main --head feature/fw-release-process \
  --title "FW release process: signed, board-named, version-stamped releases (Sim + Real)" \
  --body "Implements the FW-RemoteStation release process per docs/superpowers/specs/2026-07-04-fw-release-process-design.md.

Closes #48

- One-button workflow_dispatch release: computes YY.MM.DD-NN, stamps app/VERSION, builds all targets, static native_sim, cosign keyless, SHA256SUMS, tag + release.
- release-targets.yaml (fm-sa818-2m + fm-sa818-70cm), band overlays, CONFIG_MODULE_SA818 in app/prj.conf, runtime \`version\` command.
- Verified via dry-run: 6 assets, both native_sim smokes green (static + describe + version).

linux-image consumption (pin recipes + bundle + preflight) lands in a separate PR."
```

- [ ] **Step 2: Run the copilot review loop** per `~/.claude/skills/copilot-loop/` (Opus for code-quality). Address rounds until clean.

- [ ] **Step 3: Merge** (follow FW-RemoteStation maintainer preference). After merge:
```bash
gh workflow run release.yml --ref main       # real release (dry_run=false default)
```
- [ ] **Step 4: Confirm the release published** and record the tag (the linux-image session needs it to pin):
```bash
gh release view "$(gh release list --repo OE5XRX/FW-RemoteStation --limit 1 --json tagName -q '.[0].tagName')" \
  --repo OE5XRX/FW-RemoteStation
```
Expected: assets `fm-sa818-2m.{bin,elf,native_sim}`, `fm-sa818-70cm.{bin,elf,native_sim}`, `SHA256SUMS`, and `*.bundle` present. **Report the tag** (e.g. `26.07.04-01`) so the linux-image session can pin it.

---

## Definition of Done (spec §9/§12, FW scope) — final checklist

- [ ] Manual `workflow_dispatch` release produces tag `YY.MM.DD-NN` + signed GitHub Release with board-named assets (2m + 70cm, real `.bin`/`.elf` + static `.native_sim`) + `SHA256SUMS` + `*.bundle`. *(Task 6, 8)*
- [ ] Firmware reports the release version at runtime via `version` (from `app_version.h`). *(Task 2, verified in Task 3/7 smoke)*
- [ ] native_sim is static (`ldd` = "not a dynamic executable") and answers `module fm describe` with the correct band. *(Task 3, 4, 7)*
- [ ] Dry-run CI green; first real release published and tag reported for the linux-image session. *(Task 7, 8)*

## Self-Review notes (author)

- **Spec coverage (FW scope):** §2 decisions → T1/T3/T5/T6; §3 version/trigger/stamp → T2/T6; §4 manifest → T5; §5 describe → T1; §6 static → T3; §7 naming → T6; §8 signing → T6; §12 tests (version-stamp, describe-smoke, static-check) → T2/T3/T4/T7. §9 (linux-image bundle/pin/preflight) is **out of scope — separate session**.
- **west.yml pins zephyr `revision: main`** (floating) — reproducibility risk noted but out of scope for this plan.
- **Type consistency:** asset basenames (`fm-sa818-2m` / `fm-sa818-70cm`), version line prefix `APP-VERSION`, describe marker `MODULE-DESCRIBE`, and smoke arg order `<binary> <version> <band>` are used identically across T3/T6.
