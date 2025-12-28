# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
Pytest configuration and fixtures for SA818 driver tests.

Provides fixtures for:
- SA818 hardware simulator
- Shell interaction helpers
- Native_sim PTY bridging
"""

import pytest
import time
from sa818_simulator import SA818Simulator

from twister_harness.device.device_adapter import DeviceAdapter

@pytest.fixture(scope="function")
def sa818_sim(dut: DeviceAdapter):
    """
    Create SA818 simulator that connects to native_sim UART PTY.
    
    This is the key integration fixture:
    1. Native_sim starts (via twister/pytest)
    2. Read DUT output to find UART PTY path
    3. Start simulator on that PTY
    4. Simulator reads/writes directly on native_sim UART
    
    No socat needed!
    
    Usage:
        @pytest.mark.sa818_sim
        def test_with_uart(sa818_sim, shell):
            # Simulator is connected to firmware UART
            shell.exec_command("sa818 at_volume 7")
            assert sa818_sim.get_state().volume == 7
    """
    import re
    
    # Read DUT startup output to find UART PTY
    uart_pty = None
    
    print("Searching for native_sim UART PTY...")
    
    # Try to read initial output
    for attempt in range(100):
        try:
            line = dut.readline(timeout=0.1)
            if line:
                print(f"DUT: {line[:80]}")
                
                # Look for: "uart_1 connected to pseudotty: /dev/pts/X"
                if "uart_1" in line and "pseudotty" in line:
                    match = re.search(r'/dev/pts/\d+', line)
                    if match:
                        uart_pty = match.group(0)
                        print(f"✓ Found UART PTY: {uart_pty}")
                        break
        except Exception:
            pass
    
    assert uart_pty, "Could not find native_sim UART PTY in output"
    
    # Start simulator on this PTY
    print(f"Starting SA818 simulator on {uart_pty}...")
    sim = SA818Simulator(pty_path=uart_pty)
    sim.start()
    
    # Give simulator time to attach
    time.sleep(0.2)
    
    print("✓ SA818 simulator connected to firmware UART")
    
    yield sim
    
    # Cleanup
    print("Stopping SA818 simulator")
    sim.stop()


def pytest_configure(config):
    """Register custom markers."""
    config.addinivalue_line(
        "markers", "sa818_sim: tests that require SA818 hardware simulator"
    )
    config.addinivalue_line(
        "markers", "at_commands: tests that verify AT command protocol"
    )
