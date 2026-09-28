#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""HIL audio-loopback gate for FW-RemoteStation (Baustein 8.4).

Flashes a ``CONFIG_FM_TEST_LOOPBACK=y`` fm_board build over SWD, then drives the
firmware's internal UAC2 loopback (USB OUT -> USB IN, SA818 bypassed) with a
reference tone played on the board's UAC2 *playback* ALSA device while recording
its *capture* device. The recording is scored with
:func:`fw_hil.audio_analysis.analyze_loopback`; the gate passes only when it
reports ``ok=True`` (correlation / SNR / dropout-fraction thresholds).

This is a purely **digital** firmware loopback: no analog path, no RF, no PTT,
no 12 V rail, and no ALSA loopback cable. ``LiveAudioLoopback.run_loopback()``
arms the loopback over the CDC shell (``audio loopback on``), runs one
play+capture measurement, and *always* disarms it (``audio loopback off``) in a
``finally`` block — even if the capture raises.

Kept separate from ``scripts/hil_bench_gate.py`` because the audio gate needs its
own firmware variant (``CONFIG_FM_TEST_LOOPBACK=y``, absent from the DFU gate's
production-shaped images) flashed over SWD; mixing it into the DFU cycle would
couple two unrelated firmware builds. Both scripts share the same fw_hil backends
(``fw_hil.backends.west`` for the SWD flash, ``fw_hil.audio_live`` for the
measurement).

Called by ``.github/workflows/hil.yml`` on the ``[self-hosted, hil, fm_board]``
runner. Requires fw_hil pre-installed in ``/opt/fw-hil-venv`` (FW-HIL
``ansible/site.yml``).
"""

import argparse
import sys
import time


def main() -> int:
    ap = argparse.ArgumentParser(
        description="HIL audio loopback gate (CONFIG_FM_TEST_LOOPBACK)"
    )
    ap.add_argument("--fw-repo-dir", required=True, help="FW-RemoteStation west workspace root")
    ap.add_argument(
        "--audio-build-dir",
        required=True,
        help="west sysbuild output dir for the CONFIG_FM_TEST_LOOPBACK build (flashed over SWD)",
    )
    ap.add_argument("--probe-serial", required=True, help="ST-Link probe serial (pyocd --dev-id)")
    ap.add_argument("--cdc", default="/dev/fm-board-cdc", help="CDC ACM device path (udev symlink)")
    ap.add_argument(
        "--card-hint",
        default="FM Transceiver Board",
        help="ALSA card name/id substring for the fm_board UAC2 device (aplay -l)",
    )
    ap.add_argument(
        "--boot-settle-s",
        type=float,
        default=8.0,
        help="Seconds to wait after flashing for the board to reboot + re-enumerate USB",
    )
    args = ap.parse_args()

    from fw_hil.audio_live import LiveAudioLoopback
    from fw_hil.backends.west import WestBackend

    # ── Step 1: flash the loopback firmware over SWD ──────────────────────────
    # west flash -r pyocd on a sysbuild dir flashes MCUboot + the signed app.
    # --dev-id pins the flash to this bench's ST-Link (multi-probe safe).
    print("=== Step 1: flash CONFIG_FM_TEST_LOOPBACK firmware (SWD) ===")
    west = WestBackend(workspace_dir=args.fw_repo_dir)
    west.flash(runner="pyocd", dev_id=args.probe_serial, build_dir=args.audio_build_dir)
    # Let the board reboot, MCUboot self-confirm (SKIP_SA818 build), and USB
    # re-enumerate (UAC2 + CDC) before we open the CDC shell / ALSA devices.
    time.sleep(args.boot_settle_s)

    # ── Step 2: drive the UAC2 loopback and score it ──────────────────────────
    # run_loopback() sends `audio loopback on`, plays the reference tone while
    # recording, scores it, and sends `audio loopback off` in a finally block.
    # No pyocd runs here — the SWD probe is idle during aplay/arecord.
    #
    # tone_duration_s + tail (= max(settle_s, 0.5)) must be a whole number of
    # seconds: LiveAudioLoopback records for ceil(tone + tail) seconds but only
    # *plays* tone + tail seconds of audio. In loopback mode the captured UAC2 IN
    # is echoed from the played UAC2 OUT, so once playback ends the IN source
    # dries out — if ceil() rounded the capture window up past the playback
    # length, arecord would xrun (EIO) on the silent tail and fail the gate.
    # tone=1.0 + settle=1.0 -> tail=1.0 -> a flat 2.0 s window, ceil is a no-op,
    # and playback exactly covers the capture. HW-verified: corr 1.0 / SNR 90 dB.
    print("=== Step 2: UAC2 internal loopback measurement ===")
    loop = LiveAudioLoopback(
        cdc_path=args.cdc,
        card_hint=args.card_hint,
        tone_duration_s=1.0,
        settle_s=1.0,
    )
    result = loop.run_loopback()
    print(
        f"loopback result: ok={result.ok} "
        f"correlation={result.correlation:.3f} "
        f"snr_db={result.snr_db:.1f} "
        f"dropouts={result.dropout_count} "
        f"dropout_fraction={result.dropout_fraction:.4f} "
        f"latency_s={result.latency_s:.4f}"
    )

    if not result.ok:
        print("\n=== HIL audio loopback gate: FAILED ===", file=sys.stderr)
        return 1

    print("\n=== HIL audio loopback gate: PASS ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
