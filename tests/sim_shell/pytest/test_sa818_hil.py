# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
SA818 Hardware-in-the-Loop Integration Tests

Tests that connect the SA818 simulator directly to the Zephyr UART.
These tests verify the complete AT command flow:
  Zephyr Driver -> UART -> Simulator -> Response -> UART -> Zephyr Driver
"""

import pytest
import re
import time
import os

from test_sa818 import _parse_sa818_status


def _as_text(out) -> str:
    """Convert shell output to text."""
    if isinstance(out, list):
        return "\n".join(str(line) for line in out)
    return str(out)

@pytest.mark.sa818_sim  
def test_zephyr_at_volume_command_simple(sa818_sim, shell):
    """
    Test volume change via Zephyr shell command WITH firmware.
    
    This test verifies the complete flow:
    1. Shell command → Firmware
    2. Firmware sends AT command via UART
    3. Simulator receives on same PTY
    4. Simulator responds
    5. Firmware receives response
    6. Simulator state updated
    
    This is now simple because simulator uses native_sim's UART PTY directly!
    """
    # Power on device first
    out = shell.exec_command("sa818 power on")
    time.sleep(0.1)

    volume_level = 7
    
    # Send AT volume command via shell
    out = shell.exec_command(f"sa818 at_volume {volume_level}")
    text = _as_text(out)
    
    print(f"Shell output: {text}")
    
    # Give time for AT transaction
    time.sleep(0.3)
    
    # Check simulator state
    state = sa818_sim.get_state()
    
    print(f"Simulator volume: {state.volume}")
    
    # Verify state was updated
    assert state.volume == volume_level, f"Expected volume {volume_level}, got {state.volume}"
    
    # Verify no timeout error
    assert "err" not in text.lower(), f"AT command failed: {text}"

    out = shell.exec_command("sa818 status")
    status = _parse_sa818_status(out)

    print(status)
    assert status["volume"] == volume_level, f"Expected volume {volume_level} in status, got {status['volume']}"
    
    print("✓ Complete HIL flow working: Shell → Firmware → UART → Simulator")
