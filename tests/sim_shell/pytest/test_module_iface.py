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
