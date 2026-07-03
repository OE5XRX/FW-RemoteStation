# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
SA818 Test Tone Generator Tests

Pytest-based tests for SA818 test tone generation feature.
"""
import re
import time


_ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")


def _normalize_out(out) -> list[str]:
    """
    Normalize shell output to a list of clean text lines (no ANSI).
    """
    if isinstance(out, list):
        lines = out
    else:
        lines = str(out).splitlines()

    # strip ANSI + whitespace
    clean = [_ANSI_RE.sub("", line).rstrip("\r\n") for line in lines]
    # drop empty lines
    return [l for l in clean if l.strip()]


def _as_text(out) -> str:
    return "\n".join(_normalize_out(out))


def test_sa818_test_tone_continuous(shell):
    """Test continuous test tone generation."""
    # Power on and enable PTT
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")
    
    # Start continuous tone
    out = shell.exec_command("sa818 test tone 1000")
    text = _as_text(out)
    assert "error" not in text.lower()
    assert "test tone started" in text.lower() or "continuous" in text.lower()
    
    # Let it run briefly
    time.sleep(0.5)
    
    # Stop tone
    out = shell.exec_command("sa818 test tone_stop")
    text = _as_text(out)
    assert "error" not in text.lower()
    assert "stopped" in text.lower()
    
    # Clean up
    shell.exec_command("sa818 ptt off")


def test_sa818_test_tone_timed(shell):
    """Test timed test tone generation."""
    # Power on and enable PTT
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")
    
    # Start 1 second tone
    out = shell.exec_command("sa818 test tone 1000 1000")
    text = _as_text(out)
    assert "error" not in text.lower()
    assert "test tone started" in text.lower()
    
    # Wait for tone to complete
    time.sleep(1.5)
    
    # Clean up
    shell.exec_command("sa818 ptt off")


def test_sa818_test_tone_with_amplitude(shell):
    """Test test tone with custom amplitude."""
    # Power on and enable PTT
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")
    
    # Start tone with custom amplitude
    out = shell.exec_command("sa818 test tone 1750 2000 200")
    text = _as_text(out)
    assert "error" not in text.lower()
    assert "test tone started" in text.lower()
    
    # Let it run briefly
    time.sleep(0.5)
    
    # Stop tone
    out = shell.exec_command("sa818 test tone_stop")
    text = _as_text(out)
    assert "error" not in text.lower()
    
    # Clean up
    shell.exec_command("sa818 ptt off")


def test_sa818_test_tone_invalid_frequency(shell):
    """Test that invalid frequencies are rejected."""
    # Power on and enable PTT
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")
    
    # Try frequency too low
    out = shell.exec_command("sa818 test tone 50")
    text = _as_text(out)
    assert "error" in text.lower() or "invalid" in text.lower()
    
    # Try frequency too high
    out = shell.exec_command("sa818 test tone 5000")
    text = _as_text(out)
    assert "error" in text.lower() or "invalid" in text.lower()
    
    # Clean up
    shell.exec_command("sa818 ptt off")


def test_sa818_test_tone_invalid_amplitude(shell):
    """Test that invalid amplitudes are rejected."""
    # Power on and enable PTT
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")
    
    # Try amplitude too high
    out = shell.exec_command("sa818 test tone 1000 1000 300")
    text = _as_text(out)
    assert "error" in text.lower() or "invalid" in text.lower()
    
    # Clean up
    shell.exec_command("sa818 ptt off")


def test_sa818_test_tone_stop_when_not_active(shell):
    """Test stopping tone when no tone is active."""
    # Power on
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")
    
    # Try to stop when no tone is active
    out = shell.exec_command("sa818 test tone_stop")
    text = _as_text(out)
    # Should succeed (idempotent operation)
    assert "error" not in text.lower()
    
    # Clean up
    shell.exec_command("sa818 ptt off")


def test_sa818_test_tone_frequency_range(shell):
    """Test various valid frequencies."""
    # Power on and enable PTT
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")
    
    # Test low end
    out = shell.exec_command("sa818 test tone 100 500")
    text = _as_text(out)
    assert "error" not in text.lower()
    time.sleep(0.3)
    
    # Test mid range
    out = shell.exec_command("sa818 test tone 1500 500")
    text = _as_text(out)
    assert "error" not in text.lower()
    time.sleep(0.3)
    
    # Test high end
    out = shell.exec_command("sa818 test tone 3000 500")
    text = _as_text(out)
    assert "error" not in text.lower()
    time.sleep(0.3)
    
    # Stop tone
    shell.exec_command("sa818 test tone_stop")
    
    # Clean up
    shell.exec_command("sa818 ptt off")
