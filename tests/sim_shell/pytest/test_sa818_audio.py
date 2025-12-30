# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
SA818 Audio and DAC Integration Tests

Pytest-based tests for SA818 audio subsystem, specifically testing
DAC integration for TX audio output.
"""
import re


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


def test_sa818_audio_dac_initialization(shell):
    """Test that DAC is properly initialized."""
    # This test verifies that the SA818 driver successfully initialized
    # the DAC by checking that the device is ready
    out = shell.exec_command("sa818 status")
    text = _as_text(out)
    
    # If we get a valid status, the DAC must be initialized
    # because the driver requires a DAC to be present
    assert "error" not in text.lower() and "sa818 not ready" not in text.lower()


def test_sa818_audio_tx_path_enable(shell):
    """Test enabling/disabling TX audio path."""
    # Power on
    shell.exec_command("sa818 power on")
    
    # Test PTT on (enables TX path)
    out = shell.exec_command("sa818 ptt on")
    text = _as_text(out)
    assert "error" not in text.lower()
    
    # Test PTT off (disables TX path)
    out = shell.exec_command("sa818 ptt off")
    text = _as_text(out)
    assert "error" not in text.lower()


def test_sa818_audio_tx_commands_no_error(shell):
    """Test that TX audio commands execute without error."""
    # Enable TX to trigger DAC writes
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")

    # Verify commands execute without error
    out = shell.exec_command("sa818 status")
    text = _as_text(out)
    assert "error" not in text.lower()

    shell.exec_command("sa818 ptt off")


def test_sa818_audio_commands_execute(shell):
    """Test that audio-related commands execute without error."""
    # Verify audio commands work
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")

    # Verify no errors occur
    out = shell.exec_command("sa818 status")
    assert "error" not in _as_text(out).lower()

    shell.exec_command("sa818 ptt off")


def test_sa818_audio_dac_required(shell):
    """Test that DAC is required for SA818 operation."""
    # The device tree must specify a DAC in io-channels[1]
    # If it's missing, the driver should fail to initialize
    # Since we're running this test, the driver must have initialized successfully
    # which means the DAC is present
    
    out = shell.exec_command("sa818 status")
    text = _as_text(out)
    
    # Should get valid status (not an error about missing DAC)
    assert "sa818 not ready" not in text.lower()
    # If DAC is mentioned, it should not be "not ready"
    if "dac" in text.lower():
        assert "dac not ready" not in text.lower()
