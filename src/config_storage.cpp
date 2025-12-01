#include "config_storage.h"

#include "crc32.h"
#include "hal/system_manager.h"

ConfigStorage::ConfigStorage(uint16_t slotSize, uint16_t slotAOffset, uint16_t slotBOffset, uint16_t configVersion) : _slotSize(slotSize), _slotA(slotAOffset), _slotB(slotBOffset), _version(configVersion) {
}

bool ConfigStorage::load(AppConfig &cfg, uint32_t &sequence) {
  return loadFromSlots(cfg, sequence);
}

bool ConfigStorage::save(const AppConfig &cfg) {
  return saveToSlots(cfg);
}

bool ConfigStorage::readHeader(uint16_t base, ConfigHeader &hdr) {
  return SystemManager::getEeprom().read(base, reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr));
}

bool ConfigStorage::readConfig(uint16_t base, const ConfigHeader &hdr, AppConfig &cfg) {
  if (hdr.length != sizeof(AppConfig)) {
    return false;
  }
  return SystemManager::getEeprom().read(base + sizeof(ConfigHeader), reinterpret_cast<uint8_t *>(&cfg), sizeof(AppConfig));
}

bool ConfigStorage::validateSlot(uint16_t base, SlotInfo &out, AppConfig &cfg) {
  ConfigHeader hdr{};
  if (!readHeader(base, hdr)) {
    return false;
  }

  const uint32_t expectedMagic = CONFIG_MAGIC;

  if (hdr.magic != expectedMagic) {
    return false;
  }
  if (hdr.version != _version) {
    return false;
  }
  if (hdr.length != sizeof(AppConfig)) {
    return false;
  }
  if (sizeof(ConfigHeader) + hdr.length > _slotSize) {
    return false;
  }

  if (!readConfig(base, hdr, cfg)) {
    return false;
  }

  // CRC prüfen
  uint32_t calc = crc32(&cfg, sizeof(AppConfig));
  if (calc != hdr.crc32) {
    return false;
  }

  out.valid    = true;
  out.sequence = hdr.sequence;
  out.offset   = base;
  return true;
}

bool ConfigStorage::loadFromSlots(AppConfig &cfg, uint32_t &sequence) {
  SlotInfo a{false, 0, _slotA};
  SlotInfo b{false, 0, _slotB};

  AppConfig cfgA{};
  AppConfig cfgB{};

  bool validA = validateSlot(_slotA, a, cfgA);
  bool validB = validateSlot(_slotB, b, cfgB);

  if (!validA && !validB) {
    return false; // keine gültige Config → Caller lädt Defaults
  }

  if (validA && (!validB || a.sequence >= b.sequence)) {
    cfg      = cfgA;
    sequence = a.sequence;
  } else {
    cfg      = cfgB;
    sequence = b.sequence;
  }
  return true;
}

bool ConfigStorage::writeHeaderAndConfig(uint16_t base, uint32_t sequence, const AppConfig &cfg) {
  ConfigHeader hdr{};
  hdr.magic    = CONFIG_MAGIC;
  hdr.version  = _version;
  hdr.length   = sizeof(AppConfig);
  hdr.sequence = sequence;
  hdr.crc32    = crc32(&cfg, sizeof(AppConfig));

  // 1. Payload schreiben
  if (!SystemManager::getEeprom().write(base + sizeof(ConfigHeader), reinterpret_cast<const uint8_t *>(&cfg), sizeof(AppConfig))) {
    return false;
  }

  // 2. Header zum Schluss schreiben
  if (!SystemManager::getEeprom().write(base, reinterpret_cast<const uint8_t *>(&hdr), sizeof(hdr))) {
    return false;
  }

  return true;
}

bool ConfigStorage::saveToSlots(const AppConfig &cfg) {
  // zuerst aktuelle Sequenz ermitteln
  SlotInfo a{false, 0, _slotA};
  SlotInfo b{false, 0, _slotB};

  AppConfig dummy;
  validateSlot(_slotA, a, dummy);
  validateSlot(_slotB, b, dummy);

  uint32_t nextSeq = 0;
  if (a.valid || b.valid) {
    uint32_t cur = 0;
    if (a.valid) {
      cur = a.sequence;
    }
    if (b.valid && b.sequence > cur) {
      cur = b.sequence;
    }
    nextSeq = cur + 1;
  }

  // in den "anderen" Slot schreiben
  uint16_t targetBase = _slotA;
  if (a.valid && (!b.valid || a.sequence >= b.sequence)) {
    targetBase = _slotB;
  } else if (b.valid && (!a.valid || b.sequence > a.sequence)) {
    targetBase = _slotA;
  }

  return writeHeaderAndConfig(targetBase, nextSeq, cfg);
}
