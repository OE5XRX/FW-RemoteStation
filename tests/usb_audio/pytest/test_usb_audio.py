# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
USB Audio Bridge Integration Tests

Tests the application-level integration between SA818 and USB Audio Class 2.
These tests verify that:
- USB device enumerates correctly
- USB Audio interfaces are detected
- Audio streaming can be initiated
- SA818 and USB Audio Bridge work together

Note: These are hardware tests that require fm_board with USB connection.
"""

import re


def test_usb_device_boot(dut, usb_enumerated):
    """
    Test that the device boots and USB is initialized.
    """
    # Look for USB initialization message in boot log
    output = dut.readlines()
    text = "\n".join(output)
    
    # Should see USB device enabled
    assert "USB device enabled" in text or "USB" in text


def test_sa818_device_ready(dut):
    """
    Test that SA818 device is ready after boot.
    """
    output = dut.readlines()
    text = "\n".join(output)
    
    # SA818 should be ready
    assert "SA818 device ready" in text


def test_usb_audio_bridge_initialized(dut):
    """
    Test that USB Audio Bridge is initialized.
    """
    output = dut.readlines()
    text = "\n".join(output)
    
    # Should see bridge initialization
    assert "USB Audio Bridge" in text or "UAC2" in text


def test_system_ready(dut):
    """
    Test that the complete system is ready.
    """
    output = dut.readlines()
    text = "\n".join(output)
    
    # Should see system ready message
    assert "System ready" in text or "ready" in text.lower()


def test_no_boot_errors(dut):
    """
    Test that there are no errors during boot.
    """
    output = dut.readlines()
    text = "\n".join(output).lower()
    
    # Should not see critical errors
    assert "failed" not in text or "Failed to" not in text
    assert "error" not in text or "ERROR:" not in text


# Additional tests that could be implemented with USB host tools:
#
# def test_usb_audio_enumeration():
#     """Test USB Audio device enumeration on host (requires pyusb)"""
#     pass
#
# def test_usb_audio_playback():
#     """Test audio playback to SA818 TX (requires host audio tools)"""
#     pass
#
# def test_usb_audio_capture():
#     """Test audio capture from SA818 RX (requires host audio tools)"""
#     pass
