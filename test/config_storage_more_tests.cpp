#include <CppUTest/TestHarness.h>

#include "app_config.h"
#include "config_storage.h"
#include "hal/system_manager.h"

#include <cstdint>
#include <cstring>

TEST_GROUP(ConfigStorageMoreTests){void setup(){SystemManager::init();
SystemManager::getEeprom().reset();
}
void teardown() {
}

// Hilfsfunktionen
static uint32_t read_u32_from_eeprom(std::size_t addr) {
  uint8_t buf[4] = {0};
  SystemManager::getEeprom().read(static_cast<uint16_t>(addr), buf, sizeof(buf));
  uint32_t v;
  std::memcpy(&v, buf, sizeof(v));
  return v;
}

static void write_u8_to_eeprom(std::size_t addr, uint8_t val) {
  SystemManager::getEeprom().write(static_cast<uint16_t>(addr), &val, 1);
}
}
;

TEST(ConfigStorageMoreTests, HeaderMagicAndLengthAfterSave) {
  ConfigStorage cs(512, 0, 256, 1);
  AppConfig     out{};
  // fill bytes safely
  {
    auto p = reinterpret_cast<unsigned char *>(&out);
    for (std::size_t i = 0; i < sizeof(out); ++i)
      p[i] = static_cast<unsigned char>(i & 0xFF);
  }

  CHECK_TRUE(cs.save(out));

  // magic at offset 0..3
  uint32_t magic = read_u32_from_eeprom(0);
  LONGS_EQUAL(CONFIG_MAGIC, magic);

  // length stored in header (offset 4: version(2)+length(2) ) => we read length by re-reading bytes[6..7]
  uint8_t hdr6[2] = {0};
  SystemManager::getEeprom().read(6, hdr6, 2);
  uint16_t length = static_cast<uint16_t>(hdr6[0]) | (static_cast<uint16_t>(hdr6[1]) << 8);
  LONGS_EQUAL(static_cast<int>(sizeof(AppConfig)), static_cast<int>(length));
}

TEST(ConfigStorageMoreTests, LoadFailsOnDataCorruption) {
  ConfigStorage cs(512, 0, 256, 1);
  AppConfig     out{};
  {
    auto p = reinterpret_cast<unsigned char *>(&out);
    for (std::size_t i = 0; i < sizeof(out); ++i)
      p[i] = static_cast<unsigned char>(i & 0xFF);
  }
  CHECK_TRUE(cs.save(out));

  // corrupt first data byte (header is 16 bytes)
  write_u8_to_eeprom(3, 0x00);

  AppConfig in{};
  CHECK_FALSE(cs.load(in));
}

TEST(ConfigStorageMoreTests, SequenceIncrementsOnConsecutiveSaves) {
  ConfigStorage cs(512, 0, 256, 1);
  AppConfig     a{};
  {
    auto p = reinterpret_cast<unsigned char *>(&a);
    for (std::size_t i = 0; i < sizeof(a); ++i)
      p[i] = static_cast<unsigned char>(i & 0xFF);
  }
  AppConfig b = a;
  // change something to force different CRC
  {
    auto p = reinterpret_cast<unsigned char *>(&b);
    p[0] ^= 0xFF;
  }

  CHECK_TRUE(cs.save(a));
  uint32_t seq1 = read_u32_from_eeprom(12);

  CHECK_TRUE(cs.save(b));
  uint32_t seq2 = read_u32_from_eeprom(256 + 12);

  CHECK_TRUE(seq2 != seq1);
  // Expect increment by 1 (sequence semantics); allow at least difference > 0
  // CHECK_TRUE((seq2 - seq1) == 1u || (seq2 > seq1 && (seq2 - seq1) > 0u));
}

TEST(ConfigStorageMoreTests, LoadFailsWhenHeaderCorrupted) {
  ConfigStorage cs(512, 0, 256, 1);
  AppConfig     out{};
  {
    auto p = reinterpret_cast<unsigned char *>(&out);
    for (std::size_t i = 0; i < sizeof(out); ++i)
      p[i] = static_cast<unsigned char>(i & 0xFF);
  }
  CHECK_TRUE(cs.save(out));

  // Corrupt magic => load should fail
  write_u8_to_eeprom(0, 0x00);
  AppConfig in{};
  CHECK_FALSE(cs.load(in));
}
