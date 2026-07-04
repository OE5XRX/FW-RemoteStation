# Copyright (c) 2026 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Runtime `version` command format guard (native_sim).

version_shell.cpp prints the stamped app/VERSION zero-padded as
`APP-VERSION YY.MM.DD-NN`. This test build carries its own tests/sim_shell/VERSION
(12.34.56-78), so asserting that exact string proves the full chain
app/VERSION -> app_version.h -> `version` command — catching a renamed command,
a changed format string, a wrong macro, or a broken app_version.h include.
"""
import re

_ANSI = re.compile(r"\x1b\[[0-9;]*m")
# Format guard (locale/value-independent) and the exact value from
# tests/sim_shell/VERSION, which proves app_version.h is actually consumed.
_FORMAT = re.compile(r"APP-VERSION [0-9]{2}\.[0-9]{2}\.[0-9]{2}-[0-9]{2}")
_EXPECTED = "APP-VERSION 12.34.56-78"


def test_version_command_format(shell):
    out = shell.exec_command("version")
    lines = out if isinstance(out, list) else str(out).splitlines()
    text = "\n".join(_ANSI.sub("", line) for line in lines)
    assert _FORMAT.search(text), f"no `APP-VERSION YY.MM.DD-NN` line in output: {text}"
    assert _EXPECTED in text, f"expected {_EXPECTED!r} (from tests/sim_shell/VERSION) in: {text}"
