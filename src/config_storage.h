#include <cstdint>

#include "app_config.h"

static constexpr uint32_t CONFIG_MAGIC = 0x31474643u; // 'CFG1' in little-endian

struct ConfigHeader {
  uint32_t magic    : 32; // Erkennung: z.B. 'C','F','G','1'
  uint16_t version  : 16; // Schema-Version
  uint16_t length   : 16; // Länge des Payloads in Bytes
  uint32_t crc32    : 32; // CRC über den Payload
  uint32_t sequence : 32; // monoton steigende Nummer, um "neueste" Konfig zu erkennen
};
static_assert(sizeof(ConfigHeader) == 16, "Unexpected padding!");

class ConfigStorage {
public:
  ConfigStorage(uint16_t slotSize, uint16_t slotAOffset, uint16_t slotBOffset, uint16_t configVersion);

  bool load(AppConfig &cfg, uint32_t &sequence);
  bool save(const AppConfig &cfg);

#ifndef UNITTEST_BUILD
private:
#endif
  const uint16_t _slotSize;
  const uint16_t _slotA;
  const uint16_t _slotB;
  const uint16_t _version;

  struct SlotInfo {
    bool     valid;
    uint32_t sequence;
    uint16_t offset;
  };

  bool readHeader(uint16_t base, ConfigHeader &hdr);
  bool readConfig(uint16_t base, const ConfigHeader &hdr, AppConfig &cfg);
  bool validateSlot(uint16_t base, SlotInfo &out, AppConfig &cfg);
  bool loadFromSlots(AppConfig &cfg, uint32_t &sequence);
  bool writeHeaderAndConfig(uint16_t base, uint32_t sequence, const AppConfig &cfg);
  bool saveToSlots(const AppConfig &cfg);
};
