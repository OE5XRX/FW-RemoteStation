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
    
    # Prefer DUT-provided mechanisms for detecting enumeration, if available.
    timeout = 10.0
    poll_interval = 0.1

    wait_method = getattr(dut, "wait_for_usb_enumeration", None)
    if callable(wait_method):
        # Let the DUT handle waiting; assume it raises or errors on failure.
        wait_method(timeout=timeout)
        return True

    is_enumerated = getattr(dut, "is_usb_enumerated", None)
    if callable(is_enumerated):
        start = time.time()
        while time.time() - start < timeout:
            if is_enumerated():
                return True
            time.sleep(poll_interval)
        pytest.fail("USB device failed to enumerate within the expected timeout")

    # Fallback: retain original behavior when no status APIs are exposed by the DUT.
    # This keeps existing setups working but is less robust than the above paths.
    time.sleep(2)
    return True
