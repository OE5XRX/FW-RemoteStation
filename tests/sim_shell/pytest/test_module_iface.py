# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Generic `module` interface tests (describe + typed command mapping) on native_sim."""
import json
import re

_ANSI = re.compile(r"\x1b\[[0-9;]*m")


def _lines(out):
    lines = out if isinstance(out, list) else str(out).splitlines()
    return [_ANSI.sub("", l).strip() for l in lines]


def _payload(out, token):
    """Extract and json.loads the compact-JSON body of the first `token ` line."""
    needle = token + " "
    for l in _lines(out):
        i = l.find(needle)
        if i != -1:
            return json.loads(l[i + len(needle):])
    raise AssertionError(f"no {token} line in output: {_lines(out)}")


def test_module_describe_valid_json(shell):
    out = shell.exec_command("module describe")
    d = _payload(out, "MODULE-DESCRIBE")

    assert d["schema"] == 1
    assert d["identity"] == {"type": "fm_transceiver", "model": "SA818-V", "version": "2m"}

    caps = {c["name"]: c for c in d["capabilities"]}
    assert set(caps) == {"frequency", "ptt", "power_level", "rssi", "volume", "bandwidth"}

    assert caps["frequency"]["kind"] == "setting"
    assert caps["frequency"]["type"] == "float"
    assert caps["frequency"]["unit"] == "MHz"
    assert caps["frequency"]["min"] == 144.0
    assert caps["frequency"]["max"] == 148.0

    assert caps["ptt"]["kind"] == "action"
    assert caps["ptt"]["type"] == "bool"

    assert caps["power_level"]["kind"] == "setting"
    assert caps["power_level"]["type"] == "enum"
    assert caps["power_level"]["values"] == ["low", "high"]

    assert caps["rssi"]["kind"] == "telemetry"
    assert caps["rssi"]["type"] == "int"
    assert caps["rssi"]["readonly"] is True

    assert caps["volume"]["type"] == "int"
    assert caps["volume"]["min"] == 1
    assert caps["volume"]["max"] == 8

    assert caps["bandwidth"]["type"] == "enum"
    assert caps["bandwidth"]["values"] == ["12.5", "25"]


def test_module_set_frequency_e2e(sa818_sim, shell):
    """The §8 datapoint: typed set flows through the real driver to the module."""
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module set frequency 145.500")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True
    assert r["cap"] == "frequency"
    assert r["op"] == "set"
    assert r["value"] == 145.5
    # Verify it actually drove the SA818 driver -> AT+DMOSETGROUP -> simulator state.
    assert sa818_sim.get_state().freq_tx == 145.5
    assert sa818_sim.get_state().freq_rx == 145.5


def test_module_set_frequency_out_of_range(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    shell.exec_command("module set frequency 145.500")
    out = shell.exec_command("module set frequency 150.0")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False
    assert r["error"] == "out_of_range"
    # Rejected value did not reach the module.
    assert sa818_sim.get_state().freq_tx == 145.5


def test_module_set_frequency_bad_value(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module set frequency abc")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False
    assert r["error"] == "bad_value"
