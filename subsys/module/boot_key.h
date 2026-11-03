/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * MCUboot boot-key identity: the first 8 bytes (16 lowercase hex chars) of the
 * KEYHASH TLV (SHA-256 of the signing public key) of an MCUboot image. Lets the
 * server detect a bootloader/release key mismatch before it flashes.
 *
 * Pure and device-independent: the image is accessed only through a read
 * callback (flash area on target, a byte buffer in tests). No heap, no Zephyr.
 */
#pragma once
#include <stddef.h>

namespace mod {

/** Read @p len bytes at @p off into @p dst; return 0 on success, <0 on error. */
using ImageReadFn = int (*)(void *ctx, size_t off, void *dst, size_t len);

inline constexpr size_t kBootKeyHexLen = 16;

/** Fill @p out (kBootKeyHexLen + 1 bytes) with the lowercase hex prefix of the KEYHASH
 *  TLV of the MCUboot image readable via @p read (image starts at offset 0, @p size bytes
 *  readable). Returns true on success; on any failure returns false and sets out[0] = '\0'. */
[[nodiscard]] bool boot_key_from_image(ImageReadFn read, void *ctx, size_t size, char *out);

} // namespace mod
