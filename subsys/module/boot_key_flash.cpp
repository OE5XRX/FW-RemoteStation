/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Flash-map glue for identity.boot_key (MCUboot builds only): runs the pure
 * KEYHASH parser over slot0_partition, where MCUboot keeps the running image
 * header at offset 0. Every failure degrades to "absent" -- no assert, no panic:
 * a missing boot key must never affect boot or the boot-confirm health gate.
 */
#include "boot_key.h"

#include <sys/types.h>
#include <zephyr/storage/flash_map.h>

namespace {

int fa_read(void *ctx, size_t off, void *dst, size_t len) {
  return flash_area_read(static_cast<const struct flash_area *>(ctx), static_cast<off_t>(off), dst, len);
}

} // namespace

void mod_boot_key_init(char *out) {
  out[0] = '\0';
  const struct flash_area *fa = nullptr;
  if (flash_area_open(PARTITION_ID(slot0_partition), &fa) != 0 || fa == nullptr) {
    return;
  }
  // The parser clears out[0] itself on any failure; nothing else to do here.
  (void)mod::boot_key_from_image(fa_read, const_cast<struct flash_area *>(fa), fa->fa_size, out);
  flash_area_close(fa);
}
