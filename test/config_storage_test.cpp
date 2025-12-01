#include <CppUTest/TestHarness.h>

#include "app_config.h"
#include "config_storage.h"
#include "hal/system_manager.h"

#include <cstring>

TEST_GROUP(ConfigStorageTest){void setup(){SystemManager::init();
SystemManager::getEeprom().reset();
}
void teardown() {
}
}
;

TEST(ConfigStorageTest, LoadInitiallyNoConfig) {
  // genügend Platz für Header + Config
  ConfigStorage cs(CAT24C325::TOTAL_SIZE / 2, 0, CAT24C325::TOTAL_SIZE / 2, 1);
  AppConfig     cfg;
  uint32_t      sequence;
  CHECK_FALSE(cs.load(cfg, sequence));
}

TEST(ConfigStorageTest, SaveAndLoadRoundtrip) {
  ConfigStorage cs(CAT24C325::TOTAL_SIZE / 2, 0, CAT24C325::TOTAL_SIZE / 2, 1);

  AppConfig out;
  // mit Pattern füllen, damit CRC/Bytes überprüfbar sind
  // Byteweises Füllen vermeidet undefined behaviour bei Klassen mit Floats
  {
    auto p = reinterpret_cast<unsigned char *>(&out);
    for (std::size_t i = 0; i < sizeof(AppConfig); ++i)
      p[i] = i & 0xFF;
  }

  CHECK_TRUE(cs.save(out));

  AppConfig in;
  uint32_t  sequence;
  CHECK_TRUE(cs.load(in, sequence));

  MEMCMP_EQUAL(&out, &in, sizeof(AppConfig));
}
