/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Device UID: canonical formatting (mirrors scripts/read_uid.py::format_uid)
 * plus a runtime provider (STM32 hwinfo on real HW, persisted-synthetic on
 * native_sim). The formatted string MUST match the bench provisioning UID.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 3 words (ascending addresses) -> 24 upper-hex chars + NUL, high word first. */
void mod_format_uid(const uint32_t words[3], char out[25]);

/** Runtime device UID string (24 hex real / synthetic sim). Stable per boot. */
const char *mod_device_uid(void);

/** "stm32_uid" (real hwinfo) or "synthetic" (native_sim). */
const char *mod_uid_source(void);

#ifdef __cplusplus
}
#endif
