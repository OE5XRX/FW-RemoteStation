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

void mod_format_uid_bytes(const uint8_t buf[12], char out[25]) {
  // hwinfo already delivers the canonical byte sequence (high word first, each
  // word big-endian); emit it verbatim as upper-hex. See header for the mapping.
  for (int i = 0; i < 12; i++) {
    snprintf(out + i * 2, 3, "%02X", (unsigned)buf[i]);
  }
}

#if defined(CONFIG_BOARD_NATIVE_SIM) || !defined(__ZEPHYR__)

// native_sim: persist a once-generated synthetic UID to a host file so it is
// stable across restarts (A/B/C sim tests rely on a stable identity). The file
// lives in the process CWD (the twister/runner working directory). Entropy for
// the initial value comes from the host /dev/urandom -- native_sim runs on the
// host, so this needs no Zephyr random subsystem (which would otherwise drag the
// STM32 RNG into the fm_board build, where the hwinfo path is used instead).
static const char *SIM_UID_PATH = "oe5xrx_sim_uid.txt";

static char s_uid[25];
static const char *s_uid_source = "synthetic";
static bool s_uid_ready;

static void make_synthetic_words(uint32_t w[3]) {
  // Deterministic, obviously-synthetic fallback if /dev/urandom is unavailable
  // or a short read occurs.
  w[0] = 0x5A5A0001u;
  w[1] = 0x5A5A0002u;
  w[2] = 0x5A5A0003u;
  FILE *r = fopen("/dev/urandom", "rb");
  if (r != NULL) {
    size_t got = fread(w, sizeof(w[0]), 3, r);
    if (got != 3) {
      w[0] = 0x5A5A0001u;
      w[1] = 0x5A5A0002u;
      w[2] = 0x5A5A0003u;
    }
    fclose(r);
  }
}

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
  make_synthetic_words(w);
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
// hwinfo_get_device_id() returns the UID as big-endian words, high word first
// (buf = be32(Word2), be32(Word1), be32(Word0)) -- already the canonical byte
// sequence -- so mod_format_uid_bytes emits it verbatim. This matches the bench
// provisioning UID (read_uid.py) so station-manager can match module<->heartbeat.
// The byte/word order is per the Zephyr STM32 hwinfo driver; confirmed against
// real silicon at the bench (HIL).
static void load_hwinfo_uid(void) {
  uint8_t buf[12] = {0};
  ssize_t n = hwinfo_get_device_id(buf, sizeof(buf));
  if (n < (ssize_t)sizeof(buf)) {
    // Fail-safe: no crash, definitively-invalid all-zero UID + log via caller.
    strcpy(s_uid, "000000000000000000000000");
    return;
  }
  mod_format_uid_bytes(buf, s_uid);
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
