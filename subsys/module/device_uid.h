/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Device UID: canonical formatting (mirrors scripts/read_uid.py::format_uid)
 * plus a runtime provider (STM32 hwinfo on real HW, persisted-synthetic on
 * native_sim). The formatted string MUST match the bench provisioning UID.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 3 words (ascending addresses) -> 24 upper-hex chars + NUL, high word first. */
void mod_format_uid(const uint32_t words[3], char out[25]);

/**
 * Zephyr STM32 hwinfo device-id bytes -> 24 upper-hex chars + NUL.
 *
 * hwinfo_get_device_id() already returns the 96-bit UID as big-endian words in
 * high-word-first order (buf = be32(Word2), be32(Word1), be32(Word0) on the
 * 3-word STM32 families), which is exactly the canonical byte sequence, so the
 * bytes are emitted verbatim as hex. The result matches read_uid.py::format_uid.
 */
void mod_format_uid_bytes(const uint8_t buf[12], char out[25]);

/** Runtime device UID string (24 hex real / synthetic sim). Stable per boot. */
const char *mod_device_uid(void);

/** "stm32_uid" (real), "synthetic" (native_sim), or "unavailable" (hwinfo fail). */
const char *mod_uid_source(void);

/** True iff @p s is exactly 24 upper-hex chars followed by a NUL. */
bool mod_uid_is_valid_hex24(const char *s);

/**
 * native_sim only: load the persisted synthetic UID from @p path if it holds a
 * valid 24-upper-hex string, else generate one and persist it to @p path.
 * Exposed (with an explicit path) so the persist/reload/validate logic is unit-
 * testable across a simulated restart. Not defined for real-HW builds.
 */
void mod_uid_sim_load_or_make(const char *path, char out[25]);

#ifdef __cplusplus
}
#endif
