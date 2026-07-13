# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
Pytest configuration for USB Audio Bridge tests

These tests require actual hardware (fm_board) with USB connection.
"""

# The `dut` fixture (a twister_harness DeviceAdapter, with readlines()/write())
# is provided automatically by the pytest-twister-harness plugin when the tests
# run under Twister. Do NOT redefine it here — a local fixture would shadow the
# plugin's and break access to the real device. The single boot-sequence test
# waits for the boot log via dut.readlines_until(), so no separate enumeration
# fixture is needed.
