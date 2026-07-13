# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
USB Audio Bridge Integration Test (hardware)

Boots the fm_board USB-audio firmware on real hardware and verifies, from the
CDC-ACM console log, that the full init sequence runs: the USB device enables,
the SA818 is ready, the UAC2 audio bridge starts, and the system reaches its
ready state -- with no boot errors.

This is a single test on purpose. The twister_harness ``dut`` fixture is
function-scoped, so one test per assertion would re-flash the board for every
test; on this board the console IS the USB device, so each re-flash re-enumerates
the CDC port and races the ``/dev/serial/by-id`` symlink. One test = one launch =
one stable console capture.
"""


def test_usb_audio_boot_sequence(dut):
    # Read the boot banner up to (and including) the "System ready" line. The
    # Zephyr deferred log buffers the boot messages until the host opens the CDC
    # console, so they are all present in this first capture.
    lines = dut.readlines_until(regex="System ready", timeout=20.0)
    text = "\n".join(lines)

    assert "USB device enabled" in text, f"USB device did not enable.\nLog:\n{text}"
    assert "SA818 device ready" in text, f"SA818 device not ready.\nLog:\n{text}"
    # Assert on the specific success message, not just "USB Audio Bridge" (which
    # would also match an error like "USB Audio Bridge start failed").
    assert "USB Audio Bridge enabled" in text, f"USB Audio Bridge did not start.\nLog:\n{text}"
    assert "System ready" in text, f"System did not reach ready state.\nLog:\n{text}"

    low = text.lower()
    # "failed to" matches Zephyr's error phrasing without tripping on unrelated
    # text; the <err> level check below is the primary error guard.
    assert "failed to" not in low, f"Boot log reports a failure.\nLog:\n{text}"
    assert "<err>" not in low, f"Boot log contains an error-level message.\nLog:\n{text}"
