#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Measure the fm_board UAC2 capture sample rate against the host clock.

The board's audio clocks (TIM6 ADC trigger, TIM7 DAC trigger) divide SYSCLK
down to 8 kHz. On the uncalibrated MSIS the UAC2 IN stream delivered
~8052 samples/s (+0.65 %); from the 8 MHz HSE crystal it must be 8000 within
crystal tolerance. This script records raw PCM from the board's ALSA device
and fits frames-received against CLOCK_MONOTONIC.

    python3 scripts/hil_rate.py                       # hw:CARD=Board,DEV=0, 60 s
    python3 scripts/hil_rate.py --seconds 120 --tolerance-hz 0.5

Use the raw hw: device, not plughw: — plughw may resample to whatever rate it
was asked for and hide the device's true rate. The device must be idle (hw: is
exclusive). CLOCK_MONOTONIC is NTP-frequency-disciplined, so the host should
be NTP-synchronised (checked via timedatectl, warning only).

fit_rate() / evaluate() are pure and unit-tested (scripts/tests); recording
needs the bench.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time

BYTES_PER_FRAME = 2  # S16_LE, mono


def fit_rate(points: list[tuple[float, int]]) -> float:
    """Least-squares slope of cumulative frames over time, in frames/s.

    Each point is (t_seconds, frames_received_so_far), taken when a read
    returns. Reads arrive in bursts (one ALSA period at a time), so a two-point
    (first/last) estimate carries up to one period of error at each end; the
    regression over every read averages that jitter out.
    """
    if len(points) < 2:
        raise ValueError(f"need at least 2 points, got {len(points)}")
    n = len(points)
    mean_t = sum(t for t, _ in points) / n
    mean_f = sum(f for _, f in points) / n
    var_t = sum((t - mean_t) ** 2 for t, _ in points)
    if var_t == 0:
        raise ValueError("all points share the same timestamp")
    cov = sum((t - mean_t) * (f - mean_f) for t, f in points)
    return cov / var_t


def evaluate(rate: float, nominal: float, tolerance_hz: float) -> tuple[bool, str]:
    """Return (within tolerance, human-readable summary line)."""
    dev = rate - nominal
    ppm = dev / nominal * 1e6
    ok = abs(dev) <= tolerance_hz
    verdict = "PASS" if ok else "FAIL"
    return ok, (
        f"{verdict}: measured {rate:.3f} frames/s, nominal {nominal:.0f} "
        f"-> {dev:+.3f} Hz ({ppm:+.1f} ppm, {dev / nominal * 100:+.4f} %), tolerance ±{tolerance_hz} Hz"
    )


def ntp_synchronised() -> bool | None:  # pragma: no cover - host dependent
    try:
        out = subprocess.run(
            ["timedatectl", "show", "-p", "NTPSynchronized", "--value"],
            capture_output=True, text=True, timeout=5, check=False,
        ).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return None
    return out == "yes" if out else None


def record(device: str, rate: int, seconds: float, warmup: float) -> list[tuple[float, int]]:  # pragma: no cover - needs HW
    """Run arecord and return (t, frames) points for the measurement window."""
    cmd = [
        "arecord", "-q", "-D", device, "-t", "raw", "-f", "S16_LE", "-c", "1", "-r", str(rate),
        # 10 ms periods: many small reads -> dense points for the fit.
        "--period-time=10000", "--buffer-time=200000",
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert proc.stdout is not None
    points: list[tuple[float, int]] = []
    total = 0
    t_start = time.monotonic()
    try:
        while True:
            chunk = proc.stdout.read1(4096)
            now = time.monotonic()
            if not chunk:
                err = proc.stderr.read().decode(errors="replace").strip() if proc.stderr else ""
                raise RuntimeError(f"arecord ended early (rc={proc.poll()}): {err}")
            total += len(chunk)
            elapsed = now - t_start
            if elapsed < warmup:
                continue  # skip stream start-up (USB alt-setting, first packets)
            points.append((now, total // BYTES_PER_FRAME))
            if elapsed >= warmup + seconds:
                return points
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def main() -> int:  # pragma: no cover - needs HW
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--device", default="hw:CARD=Board,DEV=0", help="ALSA capture device (raw hw:, not plughw:)")
    ap.add_argument("--rate", type=int, default=8000, help="nominal sample rate (Hz)")
    ap.add_argument("--seconds", type=float, default=60.0, help="measurement window (s)")
    ap.add_argument("--warmup", type=float, default=3.0, help="discarded start-up time (s)")
    ap.add_argument("--tolerance-hz", type=float, default=0.5, help="PASS band around --rate (Hz)")
    args = ap.parse_args()

    if args.device.startswith("plughw"):
        print("WARNING: plughw: may resample and hide the device rate; use hw:", file=sys.stderr)
    ntp = ntp_synchronised()
    if ntp is not True:
        print(f"WARNING: host NTPSynchronized={ntp!r}; the result is only as good as the host clock", file=sys.stderr)

    print(f"Recording {args.device} @ {args.rate} Hz for {args.seconds:.0f} s (+{args.warmup:.0f} s warm-up)...")
    try:
        points = record(args.device, args.rate, args.seconds, args.warmup)
    except (OSError, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    rate = fit_rate(points)
    ok, line = evaluate(rate, args.rate, args.tolerance_hz)
    span = points[-1][0] - points[0][0]
    print(f"{len(points)} reads over {span:.1f} s, {points[-1][1] - points[0][1]} frames")
    print(line)
    return 0 if ok else 1


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
