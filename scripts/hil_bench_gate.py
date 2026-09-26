#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""HIL bench gate for FW-RemoteStation.

Orchestrates the three-step bench gate:
  1. USB composite assert  (sanity check: board is alive and enumerates correctly)
  2. DFU update cycle      (baseline → update → healthy image sticks)
  3. DFU revert cycle      (baseline → unhealthy → MCUboot reverts to baseline)

Called by .github/workflows/hil.yml on the [self-hosted, hil, fm_board] runner.
Requires fw_hil pre-installed in /opt/fw-hil-venv (FW-HIL ansible/site.yml).

# TODO: finalize once fw_hil exposes a dedicated bench-gate CLI entrypoint.
"""

import argparse
import os
import re
import sys


def _parse_version(fw_repo_dir: str, tweak_override: int | None = None) -> str:
    """Parse app/VERSION and return the 'APP-VERSION MM.NN.PP-TW' string.

    The CI build steps temporarily override VERSION_TWEAK to produce images
    with distinct version strings; pass tweak_override to mirror that.
    """
    path = os.path.join(fw_repo_dir, "app", "VERSION")
    fields: dict[str, int] = {}
    with open(path) as f:
        for line in f:
            m = re.match(r"(\w+)\s*=\s*(\d+)", line)
            if m:
                fields[m.group(1)] = int(m.group(2))
    tweak = tweak_override if tweak_override is not None else fields["VERSION_TWEAK"]
    return (
        f"APP-VERSION "
        f"{fields['VERSION_MAJOR']:02d}."
        f"{fields['VERSION_MINOR']:02d}."
        f"{fields['PATCHLEVEL']:02d}-"
        f"{tweak:02d}"
    )


def main() -> int:
    ap = argparse.ArgumentParser(
        description="HIL bench gate: USB enum + DFU update/revert cycle"
    )
    ap.add_argument("--fw-repo-dir", required=True, help="FW-RemoteStation west workspace root")
    ap.add_argument(
        "--baseline-build-dir",
        required=True,
        help="west sysbuild output dir for the baseline image (passed to flash_baseline)",
    )
    ap.add_argument("--probe-serial", required=True, help="ST-Link probe serial (pyocd --dev-id)")
    ap.add_argument("--cdc", default="/dev/fm-board-cdc", help="CDC ACM device path (udev symlink)")
    ap.add_argument(
        "--new-image",
        required=True,
        help="Signed update .bin (SKIP_SA818=y, VERSION_TWEAK=1) — passed to dfu_download",
    )
    ap.add_argument(
        "--unhealthy-image",
        required=True,
        help="Signed unhealthy .bin (no SKIP_SA818, VERSION_TWEAK=2) — health gate fails → revert",
    )
    args = ap.parse_args()

    # Derive expected version strings from the VERSION file in the workspace.
    # Tweak 0 = baseline build, tweak 1 = update build (bumped in CI).
    # The revert cycle only asserts the baseline version is restored; the
    # unhealthy image's version is never read after a successful revert.
    baseline_ver = _parse_version(args.fw_repo_dir, tweak_override=0)
    new_ver = _parse_version(args.fw_repo_dir, tweak_override=1)
    print(f"Expected baseline version : {baseline_ver!r}")
    print(f"Expected update version   : {new_ver!r}")

    from fw_hil.backends.live_dfu import LiveDfuOps
    from fw_hil.dfu import run_revert_cycle, run_update_cycle
    from fw_hil.usb_live import assert_fm_board

    ops = LiveDfuOps(
        fw_repo_dir=args.fw_repo_dir,
        probe_serial=args.probe_serial,
        cdc_path=args.cdc,
    )

    failures: list[str] = []

    # ── Step 1: USB composite assert (pre-gate) ──────────────────────────────
    print("\n=== Step 1: USB composite assert (pre-gate) ===")
    try:
        assert_fm_board()
        print("PASS")
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        failures.append(f"usb-enum-pre: {exc}")

    # ── Step 2: DFU update cycle ──────────────────────────────────────────────
    # flash_baseline expects a west sysbuild build directory (not a raw .bin);
    # it calls 'west flash -r pyocd -d <dir>' which flashes both MCUboot and the
    # signed app.  Pass args.baseline_build_dir directly.
    if not failures:
        print("\n=== Step 2: DFU update cycle ===")
        result = run_update_cycle(
            ops,
            baseline_image=args.baseline_build_dir,
            baseline_version=baseline_ver,
            new_image=args.new_image,
            new_version=new_ver,
        )
        if result.ok:
            print(f"PASS: {result.detail} (final={result.final_version})")
        else:
            msg = f"DFU update cycle FAILED: {result.detail} (final={result.final_version}, reverted={result.reverted})"
            print(msg, file=sys.stderr)
            failures.append(msg)

    # ── Step 3: DFU revert cycle ──────────────────────────────────────────────
    if not failures:
        print("\n=== Step 3: DFU revert cycle ===")
        result = run_revert_cycle(
            ops,
            baseline_image=args.baseline_build_dir,
            baseline_version=baseline_ver,
            unhealthy_image=args.unhealthy_image,
        )
        if result.ok:
            print(f"PASS: {result.detail} (final={result.final_version})")
        else:
            msg = f"DFU revert cycle FAILED: {result.detail} (final={result.final_version})"
            print(msg, file=sys.stderr)
            failures.append(msg)

    # ── Step 4: USB composite assert (post-gate) ─────────────────────────────
    if not failures:
        print("\n=== Step 4: USB composite assert (post-gate) ===")
        try:
            assert_fm_board()
            print("PASS")
        except Exception as exc:
            print(f"FAIL: {exc}", file=sys.stderr)
            failures.append(f"usb-enum-post: {exc}")

    # ── Summary ───────────────────────────────────────────────────────────────
    if not failures:
        print("\n=== HIL bench gate: ALL PASS ===")
        return 0

    print("\n=== HIL bench gate: FAILED ===", file=sys.stderr)
    for f in failures:
        print(f"  - {f}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
