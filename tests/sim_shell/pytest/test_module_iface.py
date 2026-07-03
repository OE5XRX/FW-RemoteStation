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
    out = shell.exec_command("module fm describe")
    d = _payload(out, "MODULE-DESCRIBE")

    assert d["schema"] == 1
    assert d["module"] == "fm"
    assert d["identity"]["type"] == "fm_transceiver"
    assert d["identity"]["model"] == "SA818-V"
    assert d["identity"]["version"] == "vhf"

    caps = {c["name"]: c for c in d["capabilities"]}
    assert set(caps) == {"frequency", "tx_frequency", "rx_frequency", "ptt", "power_level", "rssi", "volume", "bandwidth", "squelch", "tx_tone", "rx_tone", "band"}

    assert caps["frequency"]["kind"] == "setting"
    assert caps["frequency"]["type"] == "float"
    assert caps["frequency"]["unit"] == "MHz"
    assert caps["frequency"]["ranges"] == [{"name": "vhf", "min": 134.0, "max": 174.0}]

    assert caps["ptt"]["kind"] == "action"
    assert caps["ptt"]["type"] == "bool"

    assert caps["power_level"]["kind"] == "setting"
    assert caps["power_level"]["type"] == "enum"
    assert caps["power_level"]["values"] == ["low", "high"]

    assert caps["rssi"]["kind"] == "telemetry"
    assert caps["rssi"]["type"] == "int"
    assert caps["rssi"]["unit"] == "raw"
    assert caps["rssi"]["readonly"] is True

    assert caps["volume"]["type"] == "int"
    assert caps["volume"]["ranges"] == [{"min": 1, "max": 8}]

    assert caps["bandwidth"]["type"] == "enum"
    assert caps["bandwidth"]["values"] == ["12.5", "25"]

    assert caps["band"]["kind"] == "telemetry"
    assert caps["band"]["type"] == "string"


def test_module_set_frequency_e2e(sa818_sim, shell):
    """The §8 datapoint: typed set flows through the real driver to the module."""
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set frequency 145.500")
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
    shell.exec_command("module fm set frequency 145.500")
    out = shell.exec_command("module fm set frequency 200.0")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False
    assert r["error"] == "out_of_range"
    # Rejected value did not reach the module.
    assert sa818_sim.get_state().freq_tx == 145.5


def test_module_set_frequency_bad_value(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set frequency abc")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False
    assert r["error"] == "bad_value"


def _status(shell):
    out = shell.exec_command("sa818 status")
    text = "\n".join(_lines(out))
    m = re.search(r"powered=(\d+)\s+ptt=(\d+)\s+high_power=(\d+)\s+squelch=(\d+)\s+volume=(\d+)", text)
    assert m, f"could not parse sa818 status: {text}"
    return {"powered": int(m[1]), "ptt": int(m[2]), "high_power": int(m[3]),
            "squelch": int(m[4]), "volume": int(m[5])}


def test_module_do_ptt(shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm do ptt on")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True and r["value"] is True
    assert _status(shell)["ptt"] == 1

    out = shell.exec_command("module fm do ptt off")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True and r["value"] is False
    assert _status(shell)["ptt"] == 0


def test_module_do_ptt_bad_value(shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm do ptt maybe")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "bad_value"


def test_module_set_power_level(shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set power_level high")
    assert _payload(out, "MODULE-RESULT")["ok"] is True
    assert _status(shell)["high_power"] == 1
    out = shell.exec_command("module fm set power_level low")
    assert _payload(out, "MODULE-RESULT")["ok"] is True
    assert _status(shell)["high_power"] == 0


def test_module_set_power_level_bad_value(shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set power_level medium")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "bad_value"


def test_module_set_volume(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set volume 7")
    assert _payload(out, "MODULE-RESULT")["ok"] is True
    assert sa818_sim.get_state().volume == 7


def test_module_set_volume_out_of_range(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set volume 9")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "out_of_range"


def test_module_set_bandwidth(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set bandwidth 25")
    assert _payload(out, "MODULE-RESULT")["ok"] is True
    assert sa818_sim.get_state().bandwidth == 1
    out = shell.exec_command("module fm set bandwidth 12.5")
    assert _payload(out, "MODULE-RESULT")["ok"] is True
    assert sa818_sim.get_state().bandwidth == 0


def test_module_get_rssi(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm get rssi")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True
    assert r["cap"] == "rssi" and r["op"] == "get"
    assert isinstance(r["value"], int)


def test_module_get_frequency_reads_shadow(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    shell.exec_command("module fm set frequency 146.000")
    out = shell.exec_command("module fm get frequency")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True and r["value"] == 146.0


def test_module_set_rssi_read_only(shell):
    out = shell.exec_command("module fm set rssi 5")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "read_only"


def test_module_unknown_capability(shell):
    out = shell.exec_command("module fm set banana 1")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "unknown_capability"


def test_module_get_unknown_capability(shell):
    out = shell.exec_command("module fm get banana")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "unknown_capability"


def test_module_set_frequency_nan(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set frequency nan")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "bad_value"


def test_module_set_action_is_wrong_op(shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set ptt on")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "wrong_op"


def test_module_do_setting_is_wrong_op(shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm do frequency 145.5")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "wrong_op"


def test_module_frequency_serializes_as_float(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set frequency 146.000")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True and r["value"] == 146.0
    out = shell.exec_command("module fm get frequency")
    r = _payload(out, "MODULE-RESULT")
    assert r["value"] == 146.0


def test_module_list(shell):
    out = shell.exec_command("module list")
    for l in _lines(out):
        i = l.find("MODULE-LIST ")
        if i != -1:
            d = json.loads(l[i + len("MODULE-LIST "):])
            assert d == {"modules": ["fm"]}
            return
    raise AssertionError(f"no MODULE-LIST line: {_lines(out)}")


def test_module_unknown_module(shell):
    out = shell.exec_command("module nope describe")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "unknown_module"
    assert r["op"] == "describe"  # the attempted op, not a placeholder "get"


def test_module_result_carries_module_id(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set frequency 145.500")
    r = _payload(out, "MODULE-RESULT")
    assert r["module"] == "fm" and r["cap"] == "frequency"


def test_module_repeater_split(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    assert _payload(shell.exec_command("module fm set rx_frequency 145.600"), "MODULE-RESULT")["ok"]
    assert _payload(shell.exec_command("module fm set tx_frequency 145.000"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().freq_rx == 145.6
    assert sa818_sim.get_state().freq_tx == 145.0
    # frequency (simplex) sets both equal
    assert _payload(shell.exec_command("module fm set frequency 144.800"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().freq_tx == 144.8
    assert sa818_sim.get_state().freq_rx == 144.8


def test_module_get_tx_rx_frequency(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    shell.exec_command("module fm set tx_frequency 145.000")
    shell.exec_command("module fm set rx_frequency 145.600")
    assert _payload(shell.exec_command("module fm get tx_frequency"), "MODULE-RESULT")["value"] == 145.0
    assert _payload(shell.exec_command("module fm get rx_frequency"), "MODULE-RESULT")["value"] == 145.6


def test_module_squelch(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    assert _payload(shell.exec_command("module fm set squelch 3"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().squelch == 3
    assert _payload(shell.exec_command("module fm get squelch"), "MODULE-RESULT")["value"] == 3


def test_module_squelch_out_of_range(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    r = _payload(shell.exec_command("module fm set squelch 9"), "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "out_of_range"


def test_module_tones(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    assert _payload(shell.exec_command("module fm set tx_tone 67.0"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().ctcss_tx == 1       # CTCSS 67.0 Hz -> code 1
    assert _payload(shell.exec_command("module fm get tx_tone"), "MODULE-RESULT")["value"] == "67.0"
    assert _payload(shell.exec_command("module fm set rx_tone none"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().ctcss_rx == 0
    assert _payload(shell.exec_command("module fm get rx_tone"), "MODULE-RESULT")["value"] == "none"


def test_module_tone_dcs(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    assert _payload(shell.exec_command("module fm set tx_tone 023"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().ctcss_tx == 39      # DCS 023 -> code 39
    assert _payload(shell.exec_command("module fm get tx_tone"), "MODULE-RESULT")["value"] == "023"


def test_module_tone_invalid_is_bad_value(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    # garbage / out-of-range codes must be rejected, not silently cleared to "none"
    r = _payload(shell.exec_command("module fm set tx_tone garbage"), "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "bad_value"
    r = _payload(shell.exec_command("module fm set rx_tone 999"), "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "bad_value"
    # partial float parse must be rejected too (not accepted as CTCSS 67.0)
    r = _payload(shell.exec_command("module fm set tx_tone 67.0junk"), "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "bad_value"
    # "none" and "off" remain valid clear requests
    assert _payload(shell.exec_command("module fm set tx_tone none"), "MODULE-RESULT")["ok"] is True
    assert _payload(shell.exec_command("module fm set rx_tone off"), "MODULE-RESULT")["ok"] is True
    assert sa818_sim.get_state().ctcss_tx == 0
    assert sa818_sim.get_state().ctcss_rx == 0


def test_module_band_telemetry(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    shell.exec_command("module fm set rx_frequency 145.500")
    r = _payload(shell.exec_command("module fm get band"), "MODULE-RESULT")
    assert r["ok"] is True and r["value"] == "vhf"


def test_module_band_string_arm(sa818_sim, shell):
    """`band` returns the named-range string via Result::okStr (the okStr arm C1 changes)."""
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm get band")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True
    assert r["cap"] == "band"
    assert r["op"] == "get"
    # Default power-on freq (145.5 VHF / 435.0 UHF) is inside the named band range,
    # so okStr returns the band name; this pins the okStr -> etl::string arm exactly.
    assert r["value"] == "vhf"


def test_module_set_parse_edges(sa818_sim, shell):
    """Pin the accept/reject behavior of numeric parsing across the to_arithmetic swap."""
    shell.exec_command("sa818 power on")

    def result(cmd):
        return _payload(shell.exec_command(cmd), "MODULE-RESULT")

    # Trailing garbage -> rejected as bad_value (whole-string parse).
    assert result("module fm set frequency 145.5x")["error"] == "bad_value"
    # Empty-ish / non-numeric -> bad_value.
    assert result("module fm set volume abc")["error"] == "bad_value"
    # Valid integer within range -> ok.
    r = result("module fm set volume 5")
    assert r["ok"] is True and r["value"] == 5
    # Valid float within range -> ok.
    r = result("module fm set frequency 145.500")
    assert r["ok"] is True and r["value"] == 145.5
    # Out-of-range numeric still parses then range-rejects (not bad_value).
    assert result("module fm set volume 99")["error"] == "out_of_range"
