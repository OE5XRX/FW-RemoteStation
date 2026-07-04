# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
SA818 frequency-sweep tests.

Drives the `sa818 test sweep` shell command on native_sim and checks that a
sweep starts, runs, stops, and rejects invalid parameters.
"""
import re
import time


_ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")


def _as_text(out) -> str:
    lines = out if isinstance(out, list) else str(out).splitlines()
    clean = [_ANSI_RE.sub("", line).rstrip("\r\n") for line in lines]
    return "\n".join(l for l in clean if l.strip())


def test_sa818_sweep_start_stop(shell):
    """A valid sweep starts, then stops via tone_stop."""
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")

    out = shell.exec_command("sa818 test sweep 300 2000 2000 128")
    text = _as_text(out)
    assert "error" not in text.lower()
    assert "sweep started" in text.lower()

    # Let a few sweep cycles run.
    time.sleep(0.3)

    out = shell.exec_command("sa818 test tone_stop")
    text = _as_text(out)
    assert "error" not in text.lower()
    assert "stopped" in text.lower()

    shell.exec_command("sa818 ptt off")


def test_sa818_sweep_defaults(shell):
    """Optional duration/amplitude may be omitted."""
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")

    out = shell.exec_command("sa818 test sweep 100 3000")
    text = _as_text(out)
    assert "error" not in text.lower()
    assert "sweep started" in text.lower()

    shell.exec_command("sa818 test tone_stop")
    shell.exec_command("sa818 ptt off")


def test_sa818_sweep_rejects_end_le_start(shell):
    """end_hz must be greater than start_hz (assert the specific message)."""
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")
    out = shell.exec_command("sa818 test sweep 2000 500")
    text = _as_text(out).lower()
    assert "greater than start" in text
    shell.exec_command("sa818 ptt off")


def test_sa818_sweep_rejects_out_of_range_freq(shell):
    """Frequencies outside 100-3000 Hz are rejected (assert the specific message)."""
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")
    out = shell.exec_command("sa818 test sweep 50 2000")
    text = _as_text(out).lower()
    assert "invalid start frequency" in text
    shell.exec_command("sa818 ptt off")


def test_sa818_sweep_rejects_bad_duration(shell):
    """Duration outside 1000-60000 ms is rejected (assert the specific message)."""
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")
    out = shell.exec_command("sa818 test sweep 300 2000 100")
    text = _as_text(out).lower()
    assert "invalid duration" in text
    shell.exec_command("sa818 ptt off")
