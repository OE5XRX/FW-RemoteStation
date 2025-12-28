# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
SA818 Hardware-in-the-Loop Integration Tests

Tests that connect the SA818 simulator directly to the Zephyr UART.
These tests verify the complete AT command flow:
  Zephyr Driver -> UART -> Simulator -> Response -> UART -> Zephyr Driver
"""

import pytest
import time

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
    shell.exec_command("sa818 power on")
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


@pytest.mark.sa818_sim
def test_zephyr_at_set_group(sa818_sim, shell):
    """
    Test AT+DMOSETGROUP command via Zephyr firmware.
    
    Verifies the complete flow:
    1. Firmware sends AT+DMOSETGROUP command via UART
    2. Simulator receives and parses parameters
    3. Simulator responds with success
    4. Firmware receives response
    5. Simulator state reflects new configuration
    
    Tests frequency, CTCSS, squelch, and bandwidth settings.
    """
    # Power on device first
    shell.exec_command("sa818 power on")
    time.sleep(0.1)
    
    # Test parameters
    bandwidth = 0  # 12.5 kHz
    freq_tx = 145.500
    freq_rx = 145.500
    ctcss_tx = 0
    squelch = 5
    ctcss_rx = 0
    
    out = shell.exec_command(f"sa818 at_group {bandwidth} {freq_tx} {freq_rx} {ctcss_tx} {squelch} {ctcss_rx}")
    text = _as_text(out)
    
    print(f"Shell output: {text}")
    
    # Give time for AT transaction
    time.sleep(0.3)
    
    # Check simulator state
    state = sa818_sim.get_state()
    
    print(f"Simulator state - BW:{state.bandwidth} TX:{state.freq_tx} RX:{state.freq_rx} "
          f"TXCSS:{state.ctcss_tx} SQ:{state.squelch} RXCSS:{state.ctcss_rx}")
    
    # Verify state was updated correctly
    assert state.bandwidth == bandwidth, f"Expected bandwidth {bandwidth}, got {state.bandwidth}"
    assert abs(state.freq_tx - freq_tx) < 0.001, f"Expected TX freq {freq_tx}, got {state.freq_tx}"
    assert abs(state.freq_rx - freq_rx) < 0.001, f"Expected RX freq {freq_rx}, got {state.freq_rx}"
    assert state.ctcss_tx == ctcss_tx, f"Expected TX CTCSS {ctcss_tx}, got {state.ctcss_tx}"
    assert state.squelch == squelch, f"Expected squelch {squelch}, got {state.squelch}"
    assert state.ctcss_rx == ctcss_rx, f"Expected RX CTCSS {ctcss_rx}, got {state.ctcss_rx}"
    
    # Verify no error in command
    assert "err" not in text.lower(), f"AT command failed: {text}"
    
    print("✓ AT+DMOSETGROUP complete: Frequency and CTCSS configured")


@pytest.mark.sa818_sim
def test_zephyr_at_set_group_with_ctcss(sa818_sim, shell):
    """
    Test AT+DMOSETGROUP with CTCSS tones enabled.
    
    Configures radio with different TX/RX frequencies and CTCSS codes.
    """
    # Power on device first
    shell.exec_command("sa818 power on")
    time.sleep(0.1)
    
    # Test with different frequencies and CTCSS
    bandwidth = 1  # 25 kHz
    freq_tx = 146.850  # Repeater output
    freq_rx = 146.250  # Repeater input
    ctcss_tx = 88     # 88.5 Hz tone
    squelch = 3
    ctcss_rx = 88     # Same tone for RX
    
    out = shell.exec_command(f"sa818 at_group {bandwidth} {freq_tx} {freq_rx} {ctcss_tx} {squelch} {ctcss_rx}")
    text = _as_text(out)
    
    print(f"Shell output: {text}")
    
    # Give time for AT transaction
    time.sleep(0.3)
    
    # Check simulator state
    state = sa818_sim.get_state()
    
    print(f"Simulator state - BW:{state.bandwidth} TX:{state.freq_tx} RX:{state.freq_rx} "
          f"TXCSS:{state.ctcss_tx} SQ:{state.squelch} RXCSS:{state.ctcss_rx}")
    
    # Verify repeater configuration
    assert state.bandwidth == bandwidth, f"Expected bandwidth {bandwidth}, got {state.bandwidth}"
    assert abs(state.freq_tx - freq_tx) < 0.001, f"Expected TX freq {freq_tx}, got {state.freq_tx}"
    assert abs(state.freq_rx - freq_rx) < 0.001, f"Expected RX freq {freq_rx}, got {state.freq_rx}"
    assert state.ctcss_tx == ctcss_tx, f"Expected TX CTCSS {ctcss_tx}, got {state.ctcss_tx}"
    assert state.squelch == squelch, f"Expected squelch {squelch}, got {state.squelch}"
    assert state.ctcss_rx == ctcss_rx, f"Expected RX CTCSS {ctcss_rx}, got {state.ctcss_rx}"
    
    # Verify no error
    assert "err" not in text.lower(), f"AT command failed: {text}"
    
    print("✓ AT+DMOSETGROUP with CTCSS: Repeater configuration successful")


@pytest.mark.sa818_sim
def test_zephyr_at_set_filters_all_enabled(sa818_sim, shell):
    """
    Test AT+SETFILTER command with all filters enabled.
    
    Verifies the complete flow:
    1. Firmware sends AT+SETFILTER command via UART
    2. Simulator receives and parses filter settings
    3. Simulator responds with success
    4. Firmware receives response
    5. Simulator state reflects new filter configuration
    """
    # Power on device first
    shell.exec_command("sa818 power on")
    time.sleep(0.1)
    
    # Enable all filters
    pre = 1
    hpf = 1
    lpf = 1
    
    # Send filter configuration command
    out = shell.exec_command(f"sa818 at_filters {pre} {hpf} {lpf}")
    text = _as_text(out)
    
    print(f"Shell output: {text}")
    
    # Give time for AT transaction
    time.sleep(0.3)
    
    # Check simulator state
    state = sa818_sim.get_state()
    
    print(f"Simulator filter state - PRE:{state.pre_emphasis} HPF:{state.high_pass} LPF:{state.low_pass}")
    
    # Verify all filters are enabled
    assert state.pre_emphasis == True, f"Expected pre-emphasis enabled, got {state.pre_emphasis}"
    assert state.high_pass == True, f"Expected high-pass enabled, got {state.high_pass}"
    assert state.low_pass == True, f"Expected low-pass enabled, got {state.low_pass}"
    
    # Verify no error in command
    assert "err" not in text.lower(), f"AT command failed: {text}"
    
    print("✓ AT+SETFILTER complete: All filters enabled")


@pytest.mark.sa818_sim
def test_zephyr_at_set_filters_selective(sa818_sim, shell):
    """
    Test AT+SETFILTER with selective filter configuration.
    
    Tests different combinations of filter settings:
    - Pre-emphasis disabled
    - High-pass enabled
    - Low-pass disabled
    """
    # Power on device first
    shell.exec_command("sa818 power on")
    time.sleep(0.1)
    
    # Selective filter configuration
    pre = 0  # Disabled
    hpf = 1  # Enabled
    lpf = 0  # Disabled
    
    out = shell.exec_command(f"sa818 at_filters {pre} {hpf} {lpf}")
    text = _as_text(out)
    
    print(f"Shell output: {text}")
    
    # Give time for AT transaction
    time.sleep(0.3)
    
    # Check simulator state
    state = sa818_sim.get_state()
    
    print(f"Simulator filter state - PRE:{state.pre_emphasis} HPF:{state.high_pass} LPF:{state.low_pass}")
    
    # Verify selective filter configuration
    assert state.pre_emphasis == False, f"Expected pre-emphasis disabled, got {state.pre_emphasis}"
    assert state.high_pass == True, f"Expected high-pass enabled, got {state.high_pass}"
    assert state.low_pass == False, f"Expected low-pass disabled, got {state.low_pass}"
    
    # Verify no error
    assert "err" not in text.lower(), f"AT command failed: {text}"
    
    print("✓ AT+SETFILTER with selective configuration: PRE=off, HPF=on, LPF=off")


@pytest.mark.sa818_sim
def test_zephyr_at_set_filters_all_disabled(sa818_sim, shell):
    """
    Test AT+SETFILTER with all filters disabled.
    
    Verifies that all audio filters can be turned off.
    """
    # Power on device first
    shell.exec_command("sa818 power on")
    time.sleep(0.1)
    
    # Disable all filters
    pre = 0
    hpf = 0
    lpf = 0
    
    out = shell.exec_command(f"sa818 at_filters {pre} {hpf} {lpf}")
    text = _as_text(out)
    
    print(f"Shell output: {text}")
    
    # Give time for AT transaction
    time.sleep(0.3)
    
    # Check simulator state
    state = sa818_sim.get_state()
    
    print(f"Simulator filter state - PRE:{state.pre_emphasis} HPF:{state.high_pass} LPF:{state.low_pass}")
    
    # Verify all filters are disabled
    assert state.pre_emphasis == False, f"Expected pre-emphasis disabled, got {state.pre_emphasis}"
    assert state.high_pass == False, f"Expected high-pass disabled, got {state.high_pass}"
    assert state.low_pass == False, f"Expected low-pass disabled, got {state.low_pass}"
    
    # Verify no error
    assert "err" not in text.lower(), f"AT command failed: {text}"
    
    print("✓ AT+SETFILTER complete: All filters disabled")


@pytest.mark.sa818_sim
def test_zephyr_at_read_rssi_default(sa818_sim, shell):
    """
    Test RSSI? command via Zephyr firmware.
    
    Verifies the complete flow:
    1. Firmware sends RSSI? query via UART
    2. Simulator receives query
    3. Simulator responds with RSSI value
    4. Firmware receives and parses response
    
    Tests with default RSSI value.
    """
    # Power on device first
    shell.exec_command("sa818 power on")
    time.sleep(0.1)
    
    # Get default RSSI from simulator
    default_rssi = sa818_sim.get_state().rssi
    
    # Read RSSI via shell command
    out = shell.exec_command("sa818 at_rssi")
    text = _as_text(out)
    
    print(f"Shell output: {text}")
    
    # Give time for AT transaction
    time.sleep(0.3)
    
    # Parse RSSI value from output
    # Expected format: "RSSI: 120"
    import re
    match = re.search(r'RSSI:\s*(\d+)', text)
    assert match, f"Could not parse RSSI from output: {text}"
    
    rssi_value = int(match.group(1))
    print(f"Parsed RSSI: {rssi_value}, Expected: {default_rssi}")
    
    # Verify RSSI matches simulator state
    assert rssi_value == default_rssi, f"Expected RSSI {default_rssi}, got {rssi_value}"
    
    # Verify no error in command
    assert "err" not in text.lower(), f"AT command failed: {text}"
    
    print(f"✓ RSSI? command complete: Read RSSI={rssi_value}")


@pytest.mark.sa818_sim
def test_zephyr_at_read_rssi_strong_signal(sa818_sim, shell):
    """
    Test RSSI reading with strong signal (high RSSI value).
    
    Sets simulator to simulate strong signal and verifies
    the RSSI reading reflects this value.
    """
    # Power on device first
    shell.exec_command("sa818 power on")
    time.sleep(0.1)
    
    # Set strong signal (high RSSI)
    strong_rssi = 200
    sa818_sim.set_rssi(strong_rssi)
    
    # Read RSSI
    out = shell.exec_command("sa818 at_rssi")
    text = _as_text(out)
    
    print(f"Shell output: {text}")
    
    # Give time for AT transaction
    time.sleep(0.3)
    
    # Parse RSSI value
    import re
    match = re.search(r'RSSI:\s*(\d+)', text)
    assert match, f"Could not parse RSSI from output: {text}"
    
    rssi_value = int(match.group(1))
    print(f"Parsed RSSI: {rssi_value}, Expected: {strong_rssi}")
    
    # Verify strong signal RSSI
    assert rssi_value == strong_rssi, f"Expected RSSI {strong_rssi}, got {rssi_value}"
    
    # Verify no error
    assert "err" not in text.lower(), f"AT command failed: {text}"
    
    print(f"✓ RSSI? with strong signal: RSSI={rssi_value}")


@pytest.mark.sa818_sim
def test_zephyr_at_read_rssi_weak_signal(sa818_sim, shell):
    """
    Test RSSI reading with weak signal (low RSSI value).
    
    Sets simulator to simulate weak signal and verifies
    the RSSI reading is correct.
    """
    # Power on device first
    shell.exec_command("sa818 power on")
    time.sleep(0.1)
    
    # Set weak signal (low RSSI)
    weak_rssi = 50
    sa818_sim.set_rssi(weak_rssi)
    
    # Read RSSI
    out = shell.exec_command("sa818 at_rssi")
    text = _as_text(out)
    
    print(f"Shell output: {text}")
    
    # Give time for AT transaction
    time.sleep(0.3)
    
    # Parse RSSI value
    import re
    match = re.search(r'RSSI:\s*(\d+)', text)
    assert match, f"Could not parse RSSI from output: {text}"
    
    rssi_value = int(match.group(1))
    print(f"Parsed RSSI: {rssi_value}, Expected: {weak_rssi}")
    
    # Verify weak signal RSSI
    assert rssi_value == weak_rssi, f"Expected RSSI {weak_rssi}, got {rssi_value}"
    
    # Verify no error
    assert "err" not in text.lower(), f"AT command failed: {text}"
    
    print(f"✓ RSSI? with weak signal: RSSI={rssi_value}")


@pytest.mark.sa818_sim
def test_zephyr_at_read_rssi_varying_levels(sa818_sim, shell):
    """
    Test RSSI reading with multiple different signal levels.
    
    Verifies that RSSI readings correctly track changing signal strengths.
    """
    # Power on device first
    shell.exec_command("sa818 power on")
    time.sleep(0.1)
    
    # Test multiple RSSI levels
    test_levels = [30, 100, 150, 255]
    
    for expected_rssi in test_levels:
        # Set RSSI level
        sa818_sim.set_rssi(expected_rssi)
        
        # Read RSSI
        out = shell.exec_command("sa818 at_rssi")
        text = _as_text(out)
        
        # Give time for AT transaction
        time.sleep(0.2)
        
        # Parse RSSI value
        import re
        match = re.search(r'RSSI:\s*(\d+)', text)
        assert match, f"Could not parse RSSI from output: {text}"
        
        rssi_value = int(match.group(1))
        print(f"Level {expected_rssi}: Got RSSI={rssi_value}")
        
        # Verify RSSI matches expected level
        assert rssi_value == expected_rssi, f"Expected RSSI {expected_rssi}, got {rssi_value}"
    
    print(f"✓ RSSI? with varying levels: All {len(test_levels)} levels verified")
