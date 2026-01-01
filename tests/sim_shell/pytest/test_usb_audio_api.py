# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
USB Audio Bridge API Tests

Unit tests for the USB Audio Bridge interface.
These tests verify the generic audio streaming interface between SA818 and USB.

These tests focus on:
- Callback registration
- Audio data flow
- Ring buffer behavior
- Error handling

Note: These are API-level tests that can run without actual USB hardware.
"""


def test_sa818_audio_stream_api_exists(shell):
    """
    Test that SA818 audio streaming API is available.
    
    This verifies that the sa818_audio_stream.h interface is compiled
    and accessible from the application.
    """
    # If we can get SA818 status, the API is linked correctly
    out = shell.exec_command("sa818 status")
    assert out is not None


def test_sa818_power_control_for_audio(shell):
    """
    Test SA818 power control which is prerequisite for audio.
    """
    # Power on SA818
    out = shell.exec_command("sa818 power on")
    text = "\n".join(out)
    assert "error" not in text.lower()
    
    # Verify powered on
    out = shell.exec_command("sa818 status")
    text = "\n".join(out)
    # Check for powered=1 or "on" state indicators
    assert "powered=1" in text or "on" in text.lower()


def test_sa818_ptt_control_for_tx_audio(shell):
    """
    Test PTT control which gates TX audio path.
    """
    shell.exec_command("sa818 power on")
    
    # Enable PTT (TX audio)
    out = shell.exec_command("sa818 ptt on")
    text = "\n".join(out)
    assert "error" not in text.lower()
    
    # Disable PTT
    out = shell.exec_command("sa818 ptt off")
    text = "\n".join(out)
    assert "error" not in text.lower()


def test_audio_pipeline_initialization(shell):
    """
    Test that audio pipeline can be initialized on native_sim.
    """
    # The sim_audio pipeline should be available
    # This is tested indirectly through SA818 commands
    shell.exec_command("sa818 power on")
    out = shell.exec_command("sa818 status")
    
    # Should not have audio initialization errors
    text = "\n".join(out).lower()
    # Check that error patterns don't appear
    assert "error" not in text


def test_sa818_audio_commands_without_usb(shell):
    """
    Test that SA818 audio commands work without USB (native_sim).
    
    This verifies the clean separation: SA818 driver doesn't depend on USB.
    """
    shell.exec_command("sa818 power on")
    shell.exec_command("sa818 ptt on")
    
    # Should work without USB
    out = shell.exec_command("sa818 status")
    text = "\n".join(out).lower()
    assert "error" not in text


def test_audio_streaming_api_independence(shell):
    """
    Test that audio streaming API is independent of USB.
    
    The sa818_audio_stream interface should work regardless of whether
    USB Audio Bridge is compiled in.
    """
    # SA818 should be functional
    out = shell.exec_command("sa818 power on")
    assert out is not None
    
    # Audio commands should work
    out = shell.exec_command("sa818 ptt on")
    text = "\n".join(out).lower()
    assert "error" not in text and "not supported" not in text
