#ifndef SRC_CRC32_H_
#define SRC_CRC32_H_

#include <cstddef>
#include <cstdint>

/// Aktualisiert einen CRC32 (IEEE) über einen Datenblock.
/// - crc_in: Initialwert (für neuen CRC i.d.R. 0xFFFFFFFF)
/// - data:   Zeiger auf Daten
/// - len:    Länge in Bytes
/// Rückgabe: neuer CRC-Wert (noch NICHT final xor'ed)
inline std::uint32_t crc32_update(std::uint32_t crc_in, const void *data, std::size_t len) {
  const std::uint8_t *p   = static_cast<const std::uint8_t *>(data);
  std::uint32_t       crc = crc_in;

  while (len--) {
    crc ^= *p++;
    for (int i = 0; i < 8; ++i) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320u; // reflektiertes Polynom
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

/// Komfortfunktion: kompletten CRC32 über einen Buffer berechnen.
/// Entspricht CRC-32/IEEE:
/// - Init:   0xFFFFFFFF
/// - Poly:   0x04C11DB7 (reflektiert 0xEDB88320)
/// - XORout: 0xFFFFFFFF
inline std::uint32_t crc32(const void *data, std::size_t len) {
  std::uint32_t crc = 0xFFFFFFFFu;
  crc               = crc32_update(crc, data, len);
  crc ^= 0xFFFFFFFFu;
  return crc;
}

#endif
