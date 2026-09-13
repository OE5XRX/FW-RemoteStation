#!/usr/bin/env python3
"""Read the STM32U5 96-bit unique device ID over SWD via pyocd, or format words.

format_uid() is pure and unit-tested; read_uid_from_target() needs hardware.
"""
from __future__ import annotations

STM32U5_UID_BASE = 0x0BFA0700  # RM0456 — confirm on real silicon at the bench


def format_uid(words: list[int]) -> str:
    """Three 32-bit words (read at ascending addresses) -> canonical hex string.

    High word first (big-endian), zero-padded to 24 hex chars.
    """
    if len(words) != 3:
        raise ValueError(f"expected 3 words, got {len(words)}")
    return "".join(f"{w & 0xFFFFFFFF:08X}" for w in reversed(words))


def read_uid_from_target(target: str = "stm32u575citx") -> str:  # pragma: no cover
    """Connect via pyocd and read the UID. Requires an attached probe + board."""
    from pyocd.core.helpers import ConnectHelper

    with ConnectHelper.session_with_chosen_probe(target_override=target) as session:
        t = session.board.target
        words = [t.read32(STM32U5_UID_BASE + i * 4) for i in range(3)]
    return format_uid(words)


if __name__ == "__main__":  # pragma: no cover
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="stm32u575citx")
    print(read_uid_from_target(ap.parse_args().target))
