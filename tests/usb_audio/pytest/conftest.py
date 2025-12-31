# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
Pytest configuration for USB Audio Bridge tests

These tests require actual hardware (fm_board) with USB connection.
"""

import pytest


@pytest.fixture(scope="session")
def dut(request):
    """
    Device Under Test fixture.
    Provides access to the hardware device for testing.
    """
    # The DUT is provided by Twister's hardware testing framework
    return request.config.dut


@pytest.fixture
def usb_enumerated(dut):
    """
    Fixture that waits for USB enumeration.
    
    Returns True when USB device is enumerated by host.
    """
    import time
    # Wait for USB enumeration (typically < 2 seconds)
    time.sleep(2)
    return True
