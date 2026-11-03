# SPDX-License-Identifier: LGPL-3.0-or-later
"""Python KEYHASH parser vs. the imgtool fixtures shared with tests/unit_boot_key,
plus MODULE-DESCRIBE parsing and the HIL gate's boot_key comparison."""
import pathlib
import re
import struct
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from hil_bench_gate import check_boot_key
from mcuboot_keyhash import keyhash_prefix, keyhash_prefix_file, parse_describe

REPO = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = REPO / "tests" / "unit_boot_key" / "fixtures"
C_TEST = REPO / "tests" / "unit_boot_key" / "src" / "main.cpp"

DEV_PREFIX = "e30466f6b8470c1f"
OTHER_PREFIX = "271b30fab2c5517f"


def _fixture(name: str) -> bytes:
    return (FIXTURES / name).read_bytes()


def _le16(b, off):
    return struct.unpack_from("<H", b, off)[0]


def _tlv_info_off(img) -> int:
    hdr_size, protect, img_size = struct.unpack_from("<HHI", img, 8)
    return hdr_size + img_size + protect


def _keyhash_entry_off(img) -> int:
    """Independent walk (same as the ztest helper) to the KEYHASH entry header."""
    info = _tlv_info_off(img)
    assert _le16(img, info) == 0x6907
    end = info + _le16(img, info + 2)
    off = info + 4
    while off + 4 <= end:
        if _le16(img, off) == 0x0001:
            return off
        off += 4 + _le16(img, off + 2)
    raise AssertionError("fixture has no KEYHASH")


def _put16(img: bytearray, off: int, v: int) -> None:
    struct.pack_into("<H", img, off, v)


# ── Fixtures / cross-check with the C parser ─────────────────────────────────


def test_expected_constants_match_c_ztest():
    """The ztest (C parser) and this test assert the very same prefixes."""
    src = C_TEST.read_text()
    assert re.search(r'kDevPrefix\s*=\s*"([0-9a-f]{16})"', src).group(1) == DEV_PREFIX
    assert re.search(r'kOtherPrefix\s*=\s*"([0-9a-f]{16})"', src).group(1) == OTHER_PREFIX


@pytest.mark.parametrize(
    "name, expected",
    [("dev-signed.bin", DEV_PREFIX), ("other-signed.bin", OTHER_PREFIX), ("prot-signed.bin", DEV_PREFIX)],
)
def test_fixture_prefixes(name, expected):
    assert keyhash_prefix(_fixture(name)) == expected
    assert keyhash_prefix_file(str(FIXTURES / name)) == expected


def test_prot_fixture_has_protected_area():
    assert _le16(_fixture("prot-signed.bin"), 10) != 0


def test_image_inside_larger_partition():
    dev = _fixture("dev-signed.bin")
    assert keyhash_prefix(dev + b"\xff" * (8192 - len(dev))) == DEV_PREFIX


def test_missing_file_returns_none(tmp_path):
    assert keyhash_prefix_file(str(tmp_path / "nope.bin")) is None


# ── Malformed input -> None ──────────────────────────────────────────────────


def test_erased_header_returns_none():
    assert keyhash_prefix(b"\xff" * 512) is None


def test_empty_returns_none():
    assert keyhash_prefix(b"") is None


def test_truncated_returns_none():
    dev = _fixture("dev-signed.bin")
    for size in (0, 8, 600, _keyhash_entry_off(dev) + 8):
        assert keyhash_prefix(dev[:size]) is None, size
    info = _tlv_info_off(dev)
    assert keyhash_prefix(dev[: info + _le16(dev, info + 2) - 1]) is None


def test_wrong_magic_returns_none():
    img = bytearray(_fixture("dev-signed.bin"))
    img[0] ^= 0x01
    assert keyhash_prefix(bytes(img)) is None


def test_tlv_total_len_overflow_returns_none():
    img = bytearray(_fixture("dev-signed.bin"))
    _put16(img, _tlv_info_off(img) + 2, 0xFFFF)
    assert keyhash_prefix(bytes(img)) is None


def test_tlv_entry_len_overflow_returns_none():
    img = bytearray(_fixture("dev-signed.bin"))
    _put16(img, _tlv_info_off(img) + 4 + 2, 0xFFF0)
    assert keyhash_prefix(bytes(img)) is None


def test_img_size_overflow_returns_none():
    img = bytearray(_fixture("dev-signed.bin"))
    struct.pack_into("<I", img, 12, 0xFFFFFFFF)
    assert keyhash_prefix(bytes(img)) is None


def test_wrong_tlv_info_magic_returns_none():
    img = bytearray(_fixture("dev-signed.bin"))
    _put16(img, _tlv_info_off(img), 0x1234)
    assert keyhash_prefix(bytes(img)) is None


def test_keyhash_type_patched_returns_none():
    img = bytearray(_fixture("dev-signed.bin"))
    _put16(img, _keyhash_entry_off(img), 0x007F)
    assert keyhash_prefix(bytes(img)) is None


def test_keyhash_wrong_len_returns_none():
    img = bytearray(_fixture("dev-signed.bin"))
    _put16(img, _keyhash_entry_off(img) + 2, 0x0010)
    assert keyhash_prefix(bytes(img)) is None


def test_protected_size_mismatch_returns_none():
    # Header claims a protected area but the TLV start holds the unprotected info.
    img = bytearray(_fixture("dev-signed.bin"))
    _put16(img, 10, 0x000C)
    assert keyhash_prefix(bytes(img)) is None
    # Protected area present but the header size disagrees with its total_len.
    img = bytearray(_fixture("prot-signed.bin"))
    _put16(img, 10, _le16(img, 10) + 4)
    assert keyhash_prefix(bytes(img)) is None


def test_protected_info_without_header_size_returns_none():
    # Protected info at the TLV start although the header says "no protected area".
    img = bytearray(_fixture("prot-signed.bin"))
    _put16(img, 10, 0)
    assert keyhash_prefix(bytes(img)) is None


# ── parse_describe ───────────────────────────────────────────────────────────

_ID_WITH_KEY = (
    '{"schema":2,"module":"fm","identity":{"type":"fm_transceiver","model":"SA818-V",'
    '"version":"26.11.03-00","boot_key":"e30466f6b8470c1f"},"capabilities":[]}'
)
_ID_WITHOUT_KEY = '{"schema":2,"module":"fm","identity":{"type":"fm_transceiver","version":"26.11.03-00"}}'


def _console(payload: str) -> str:
    # CDC echo of the command, ANSI-coloured prompt, CRLF line endings.
    return (
        "\x1b[1;32muart:~$ \x1b[mmodule fm describe\r\n"
        f"MODULE-DESCRIBE {payload}\r\n"
        "\x1b[1;32muart:~$ \x1b[m"
    )


def test_parse_describe_with_boot_key():
    d = parse_describe(_console(_ID_WITH_KEY))
    assert d["module"] == "fm"
    assert d["identity"]["boot_key"] == DEV_PREFIX


def test_parse_describe_without_boot_key():
    d = parse_describe(_console(_ID_WITHOUT_KEY))
    assert d["identity"]["version"] == "26.11.03-00"
    assert "boot_key" not in d["identity"]


def test_parse_describe_cr_only_and_ansi_inside_line():
    text = f"module fm describe\r\x1b[mMODULE-DESCRIBE {_ID_WITH_KEY}\r\x1b[1;32muart:~$ "
    assert parse_describe(text)["identity"]["boot_key"] == DEV_PREFIX


def test_parse_describe_skips_truncated_line():
    text = f"MODULE-DESCRIBE {_ID_WITH_KEY[:40]}\r\nMODULE-DESCRIBE {_ID_WITHOUT_KEY}\r\n"
    assert parse_describe(text)["identity"]["type"] == "fm_transceiver"


def test_parse_describe_absent():
    assert parse_describe("") is None
    assert parse_describe("uart:~$ module fm describe\r\nmodule: command not found\r\n") is None
    assert parse_describe("MODULE-DESCRIBE [1, 2]\r\n") is None


# ── check_boot_key (HIL gate comparison) ─────────────────────────────────────


def test_check_boot_key_match():
    assert check_boot_key(parse_describe(_console(_ID_WITH_KEY)), DEV_PREFIX) is None


def test_check_boot_key_mismatch():
    err = check_boot_key(parse_describe(_console(_ID_WITH_KEY)), OTHER_PREFIX)
    assert err is not None and "!=" in err


def test_check_boot_key_missing():
    err = check_boot_key(parse_describe(_console(_ID_WITHOUT_KEY)), DEV_PREFIX)
    assert err is not None and "missing" in err


@pytest.mark.parametrize("bad", ["E30466F6B8470C1F", "e30466f6b8470c1", "e30466f6b8470c1f0", 1234, None])
def test_check_boot_key_malformed(bad):
    d = {"identity": {"boot_key": bad}}
    err = check_boot_key(d, DEV_PREFIX)
    assert err is not None and "malformed" in err


def test_check_boot_key_no_describe_or_identity():
    assert "MODULE-DESCRIBE" in check_boot_key(None, DEV_PREFIX)
    assert "identity" in check_boot_key({"schema": 2}, DEV_PREFIX)


@pytest.mark.parametrize("expected", [None, "", "zz"])
def test_check_boot_key_bad_expected(expected):
    err = check_boot_key({"identity": {"boot_key": DEV_PREFIX}}, expected)
    assert err is not None and "expected" in err
