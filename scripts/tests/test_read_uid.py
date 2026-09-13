import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from read_uid import format_uid

def test_format_uid_big_endian_high_word_first():
    # words read at 0x..0700, 0x..0704, 0x..0708 (ascending) -> high word first
    assert format_uid([0x11223344, 0x55667788, 0x99AABBCC]) == "99AABBCC5566778811223344"

def test_format_uid_zero_padding():
    assert format_uid([0x1, 0x0, 0x0]) == "000000000000000000000001"

def test_format_uid_requires_three_words():
    try:
        format_uid([0x1, 0x2])
    except ValueError:
        return
    assert False, "expected ValueError for != 3 words"
