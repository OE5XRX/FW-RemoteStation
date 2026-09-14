/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "device_uid.h"

#include <stdio.h>
#include <string.h>

void mod_format_uid(const uint32_t words[3], char out[25]) {
  // High word first (reversed), each as 8 upper-hex — mirrors read_uid.py.
  snprintf(out, 25, "%08X%08X%08X", (unsigned)words[2], (unsigned)words[1], (unsigned)words[0]);
}

#if defined(CONFIG_BOARD_NATIVE_SIM) || !defined(__ZEPHYR__)

#include <zephyr/random/random.h>

// native_sim: persist a once-generated synthetic UID to a host file so it is
// stable across restarts (A/B/C sim tests rely on a stable identity). The file
// lives in the process CWD (the twister/runner working directory).
static const char *SIM_UID_PATH = "oe5xrx_sim_uid.txt";

static char s_uid[25];
static const char *s_uid_source = "synthetic";
static bool s_uid_ready;

static void load_or_make_synthetic(void) {
  FILE *f = fopen(SIM_UID_PATH, "r");
  if (f != NULL) {
    if (fgets(s_uid, sizeof(s_uid), f) != NULL && strlen(s_uid) >= 24) {
      s_uid[24] = '\0';
      fclose(f);
      return;
    }
    fclose(f);
  }
  uint32_t w[3];
  sys_rand_get(w, sizeof(w));
  mod_format_uid(w, s_uid);
  f = fopen(SIM_UID_PATH, "w");
  if (f != NULL) {
    fputs(s_uid, f);
    fclose(f);
  }
}

const char *mod_device_uid(void) {
  if (!s_uid_ready) {
    load_or_make_synthetic();
    s_uid_ready = true;
  }
  return s_uid;
}

const char *mod_uid_source(void) {
  (void)mod_device_uid(); // ensure s_uid is populated
  return s_uid_source;
}

#else

#include <sys/types.h>
#include <zephyr/drivers/hwinfo.h>

static char s_uid[25];
static const char *s_uid_source = "stm32_uid";
static bool s_uid_ready;

// Real HW: read the STM32 96-bit UID and format identically to read_uid.py.
// hwinfo returns raw bytes; interpret as 3 little-endian words at offsets 0/4/8
// (same as read_uid.py's read32 at ascending addresses), then mod_format_uid
// reverses to high-word-first.
// NOTE: exact byte/word order is confirmed on real silicon at the bench (HIL).
static void load_hwinfo_uid(void) {
  uint8_t buf[12] = {0};
  ssize_t n = hwinfo_get_device_id(buf, sizeof(buf));
  if (n < (ssize_t)sizeof(buf)) {
    // Fail-safe: no crash, definitively-invalid all-zero UID + log via caller.
    strcpy(s_uid, "000000000000000000000000");
    return;
  }
  uint32_t w[3];
  for (int i = 0; i < 3; i++) {
    w[i] = (uint32_t)buf[i * 4] | ((uint32_t)buf[i * 4 + 1] << 8) | ((uint32_t)buf[i * 4 + 2] << 16) | ((uint32_t)buf[i * 4 + 3] << 24);
  }
  mod_format_uid(w, s_uid);
}

const char *mod_device_uid(void) {
  if (!s_uid_ready) {
    load_hwinfo_uid();
    s_uid_ready = true;
  }
  return s_uid;
}

const char *mod_uid_source(void) {
  (void)mod_device_uid(); // ensure s_uid is populated
  return s_uid_source;
}

#endif
