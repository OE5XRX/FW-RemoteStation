/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "device_uid.h"

#include <stdio.h>

void mod_format_uid(const uint32_t words[3], char out[25]) {
  // High word first (reversed), each as 8 upper-hex — mirrors read_uid.py.
  snprintf(out, 25, "%08X%08X%08X", (unsigned)words[2], (unsigned)words[1], (unsigned)words[0]);
}
