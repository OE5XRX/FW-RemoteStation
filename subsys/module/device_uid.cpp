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

bool mod_uid_is_valid_hex24(const char *s) {
  if (s == NULL) {
    return false;
  }
  for (int i = 0; i < 24; i++) {
    char c = s[i];
    bool hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
    if (!hex) {
      return false;
    }
  }
  return s[24] == '\0';
}

#if defined(CONFIG_BOARD_NATIVE_SIM) || !defined(__ZEPHYR__)

// native_sim: persist a once-generated synthetic UID to a host file so it is
// stable across restarts (A/B/C sim tests rely on a stable identity). The file
// lives in the process CWD (the twister/runner working directory). Entropy for
// the initial value comes from the host /dev/urandom -- native_sim runs on the
// host, so this needs no Zephyr random subsystem (which would otherwise drag the
// STM32 RNG into the fm_board build, where the hwinfo path is used instead).
// Host I/O uses raw POSIX open/read/write (no heap-backed <stdio.h> FILE, per the
// no-dynamic-allocation rule); native_sim maps these to the host libc.
#include <fcntl.h>
#include <unistd.h>
#include <zephyr/sys/printk.h>

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
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    uint8_t b[12];
    ssize_t got = read(fd, b, sizeof(b));
    close(fd);
    if (got == (ssize_t)sizeof(b)) {
      for (int i = 0; i < 3; i++) {
        w[i] = (uint32_t)b[i * 4] | ((uint32_t)b[i * 4 + 1] << 8) | ((uint32_t)b[i * 4 + 2] << 16) | ((uint32_t)b[i * 4 + 3] << 24);
      }
    }
  }
}

void mod_uid_sim_load_or_make(const char *path, char out[25]) {
  int fd = open(path, O_RDONLY);
  if (fd >= 0) {
    char tmp[25] = {0};
    ssize_t got = read(fd, tmp, 24);
    close(fd);
    if (got == 24) {
      tmp[24] = '\0';
      if (mod_uid_is_valid_hex24(tmp)) {
        memcpy(out, tmp, 25);
        return; // valid persisted UID -> reuse (stable across restarts)
      }
    }
    // Fall through to regenerate: file was short, unreadable, or corrupt.
  }
  uint32_t w[3];
  make_synthetic_words(w);
  mod_format_uid(w, out);
  int wfd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (wfd < 0) {
    // Read-only CWD (or similar): do not fail. The UID stays stable within this
    // boot; only cross-restart stability is lost. Make that explicit, not silent.
    printk("device_uid: cannot persist synthetic UID to %s; stable per-boot only\n", path);
    return;
  }
  ssize_t wr = write(wfd, out, 24);
  close(wfd);
  if (wr != 24) {
    printk("device_uid: short write persisting synthetic UID to %s (%d/24)\n", path, (int)wr);
  }
}

static void load_or_make_synthetic(void) {
  mod_uid_sim_load_or_make(SIM_UID_PATH, s_uid);
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
#include <zephyr/sys/printk.h>

static char s_uid[25];
static const char *s_uid_source = "stm32_uid";
static bool s_uid_ready;

// Real HW: read the STM32 96-bit UID and format identically to read_uid.py.
// hwinfo_get_device_id() returns the UID as big-endian words, high word first
// (buf = be32(Word2), be32(Word1), be32(Word0)) -- already the canonical byte
// sequence -- so mod_format_uid_bytes emits it verbatim. This matches the bench
// provisioning UID (read_uid.py) so station-manager can match module<->heartbeat.
// ASSUMPTION: the byte/word order is taken from the Zephyr STM32 hwinfo driver
// source; it is NOT yet confirmed on real silicon -- bench/HIL confirmation is
// deferred (native_sim CI cannot exercise the hwinfo path).
static void load_hwinfo_uid(void) {
  uint8_t buf[12] = {0};
  ssize_t n = hwinfo_get_device_id(buf, sizeof(buf));
  if (n < (ssize_t)sizeof(buf)) {
    // No usable UID. Publish an explicitly EMPTY uid (never an all-zero string,
    // which every failed board would share and a consumer could merge into one
    // device / mistake for a real identity). uid_source stays "stm32_uid" -- the
    // path that was attempted -- so the advertised {stm32_uid, synthetic} enum is
    // unchanged; the empty uid is the unambiguous "no identity" signal.
    s_uid[0] = '\0';
    printk("device_uid: hwinfo_get_device_id failed (%d); reporting empty UID\n", (int)n);
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
