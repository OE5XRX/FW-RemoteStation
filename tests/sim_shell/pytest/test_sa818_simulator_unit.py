# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""Pure-Python unit tests for the SA818 AT-command simulator.

These need NO native_sim / Zephyr / twister — they drive SA818Simulator
directly over a local pty pair, so they run under plain `pytest` and pin the
AT-protocol contract deterministically.

Topology mirrors the real setup: native_sim owns the pty MASTER and the
simulator attaches to the SLAVE. Here the test plays native_sim (master) and
the simulator attaches to the slave, exactly as it does against the firmware.

The framing tests are the important ones: they reproduce, without native_sim,
the back-to-back / char-by-char command streams that the live telemetry poll
creates — the conditions under which the current simulator desynced in
production (a set-group read got the RSSI reply and vice-versa). The contract
here is: exactly ONE correct response line per received command, in order, with
no stray, duplicated, or late bytes left in the pipe.
"""

import os
import select
import termios
import time
import tty

import pytest

from sa818_simulator import SA818Simulator, SA818State


# ---------------------------------------------------------------------------
# Pure command-processing contract (no pty) — matches drivers/radio/sa818.
# ---------------------------------------------------------------------------
@pytest.fixture
def sim():
    """A simulator instance whose _process_command can be called directly
    (no thread / pty started)."""
    return SA818Simulator(pty_path="/dev/null")


def test_dmosetgroup_ack(sim):
    r = sim._process_command("AT+DMOSETGROUP=0,145.5000,145.5000,0000,4,0000")
    assert r == "+DMOSETGROUP:0"
    assert sim.state.freq_tx == 145.5
    assert sim.state.freq_rx == 145.5
    assert sim.state.squelch == 4


def test_rssi_query(sim):
    sim.state.rssi = 137
    assert sim._process_command("RSSI?") == "RSSI=137"


def test_setvolume_ack_and_bounds(sim):
    assert sim._process_command("AT+DMOSETVOLUME=7") == "+DMOSETVOLUME:0"
    assert sim.state.volume == 7
    assert sim._process_command("AT+DMOSETVOLUME=9") == "+DMOSETVOLUME:1"  # out of range


def test_setfilter_ack(sim):
    assert sim._process_command("AT+SETFILTER=1,1,1") == "+DMOSETFILTER:0"


def test_dmoconnect_and_at(sim):
    assert sim._process_command("AT+DMOCONNECT") == "+DMOCONNECT:0"
    assert sim._process_command("AT") == "+DMOCONNECT:0"


def test_unknown_command_is_error(sim):
    assert sim._process_command("AT+BOGUS") == "ERROR"


# ---------------------------------------------------------------------------
# Framing over a real pty — the desync-relevant behaviour.
# ---------------------------------------------------------------------------
class Bridge:
    """Owns a pty pair; the simulator attaches to the slave, the test writes /
    reads on the master (playing the role of native_sim's SA818 UART)."""

    def __init__(self, raw=True):
        self.master_fd, slave_fd = os.openpty()
        slave_path = os.ttyname(slave_fd)
        # native_sim's UART is a RAW byte pipe — no line-discipline echo, no
        # canonical mode, no CR/NL translation. A default pty is cooked (echoes
        # input back to the master), which would make the master read its own
        # command instead of the simulator's reply.
        #
        # raw=True: the test itself forces raw (baseline: prove the emulator's
        #   framing is correct on a clean UART).
        # raw=False: the test LEAVES the pty cooked and relies on the simulator
        #   to force raw mode on its own fd when it attaches — the defensive
        #   guarantee that echo can never corrupt the response stream.
        if raw:
            for fd in (self.master_fd, slave_fd):
                tty.setraw(fd)
        self.sim = SA818Simulator(pty_path=slave_path)
        self.sim.start()
        # The simulator opened the slave by path; drop our handle so only the
        # simulator holds it.
        os.close(slave_fd)
        time.sleep(0.05)  # let the reader thread come up

    def write(self, data: bytes):
        os.write(self.master_fd, data)

    def send_cmd(self, cmd: str, char_by_char: bool = False):
        """Send an AT command exactly like the firmware does: chars then \\r\\n."""
        payload = cmd.encode() + b"\r\n"
        if char_by_char:
            for b in payload:
                os.write(self.master_fd, bytes([b]))
                time.sleep(0.001)
        else:
            os.write(self.master_fd, payload)

    def read_line(self, timeout: float = 1.0) -> str:
        """Read one CRLF-terminated response line from the master."""
        deadline = time.time() + timeout
        buf = b""
        while time.time() < deadline:
            r, _, _ = select.select([self.master_fd], [], [], max(0, deadline - time.time()))
            if not r:
                continue
            buf += os.read(self.master_fd, 256)
            if b"\n" in buf:
                line, _, _ = buf.partition(b"\n")
                return line.strip(b"\r").decode(errors="replace")
        raise AssertionError(f"no response line within {timeout}s (got {buf!r})")

    def drain(self, settle: float = 0.15) -> bytes:
        """Return any bytes still pending on the master after a settle delay —
        used to assert there are NO stray/late/duplicate responses."""
        time.sleep(settle)
        out = b""
        while True:
            r, _, _ = select.select([self.master_fd], [], [], 0)
            if not r:
                break
            chunk = os.read(self.master_fd, 256)
            if not chunk:
                break
            out += chunk
        return out

    def close(self):
        self.sim.stop()
        if self.master_fd >= 0:
            os.close(self.master_fd)
            self.master_fd = -1


@pytest.fixture
def bridge():
    b = Bridge()
    yield b
    b.close()


@pytest.fixture
def cooked_bridge():
    """A bridge whose pty is left in the default (cooked, echoing) state — the
    simulator itself must force raw mode so echo can't corrupt responses."""
    b = Bridge(raw=False)
    yield b
    b.close()


def test_simulator_forces_raw_mode_no_echo(cooked_bridge):
    """Defensive guarantee: even if the pty is handed over in cooked mode, the
    simulator must disable line-discipline echo on attach, so the master reads
    the simulator's reply — not the echoed command. Without the raw-mode
    hardening in start(), the master would read back 'RSSI?' (the echo)."""
    cooked_bridge.send_cmd("RSSI?")
    assert cooked_bridge.read_line() == "RSSI=120"
    cooked_bridge.send_cmd("AT+DMOSETGROUP=0,145.5000,145.5000,0000,4,0000")
    assert cooked_bridge.read_line() == "+DMOSETGROUP:0"
    assert cooked_bridge.drain() == b""


def test_single_command_one_response(bridge):
    bridge.send_cmd("RSSI?")
    assert bridge.read_line() == "RSSI=120"
    # No stray/late/duplicate bytes must remain.
    assert bridge.drain() == b"", "simulator emitted extra bytes after a single command"


def test_char_by_char_one_response(bridge):
    # The firmware writes the command one byte at a time (uart_poll_out).
    bridge.send_cmd("AT+DMOSETGROUP=0,145.5000,145.5000,0000,4,0000", char_by_char=True)
    assert bridge.read_line() == "+DMOSETGROUP:0"
    assert bridge.drain() == b""


def test_back_to_back_rssi_then_setgroup_stay_aligned(bridge):
    """The production desync: a fast RSSI poll immediately followed by a
    set-group. Each read MUST get its OWN response, in order — this is the
    regression guard for 'set-group got RSSI=120 / RSSI got +DMOSETGROUP:0'."""
    bridge.send_cmd("RSSI?")
    assert bridge.read_line() == "RSSI=120"
    bridge.send_cmd("AT+DMOSETGROUP=0,145.5000,145.5000,0000,4,0000")
    assert bridge.read_line() == "+DMOSETGROUP:0"
    assert bridge.drain() == b"", "responses drifted out of alignment"


def test_reader_stops_on_pty_eof(bridge):
    """When the pty peer (native_sim) closes, os.read() returns b'' (EOF). The
    reader must STOP, not `continue` on a perpetually-readable closed fd (which
    pegs the CPU). Closing the master here simulates native_sim exiting."""
    os.close(bridge.master_fd)
    bridge.master_fd = -1  # prevent double-close in teardown
    deadline = time.time() + 2.0
    while time.time() < deadline and bridge.sim.thread.is_alive():
        time.sleep(0.02)
    assert not bridge.sim.thread.is_alive(), "reader thread did not stop on pty EOF"


def test_rapid_interleaved_stream_stays_aligned(bridge):
    """Sustained RSSI polling interleaved with set-groups — the live load.
    Every response must line up 1:1 with its command over many iterations."""
    for i in range(25):
        bridge.send_cmd("RSSI?")
        assert bridge.read_line() == "RSSI=120", f"RSSI desync at iter {i}"
        bridge.send_cmd("AT+DMOSETGROUP=0,145.5000,145.5000,0000,4,0000")
        assert bridge.read_line() == "+DMOSETGROUP:0", f"set-group desync at iter {i}"
    assert bridge.drain() == b"", "residual bytes after sustained stream"
