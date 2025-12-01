#include "config.h"
#include "hal/system_manager.h"

AppConfig Config::s_config;
uint32_t  Config::s_sequence;

void Config::init() {
  if (!storage().load(s_config, s_sequence)) {
    s_config.applyDefaults();
    // storage().save(s_config);
  }
}

AppConfig &Config::current() {
  return s_config;
}

const AppConfig &Config::currentConst() {
  return s_config;
}

const uint32_t &Config::currentSequence() {
  return s_sequence;
}

bool Config::load() {
  return storage().load(s_config, s_sequence);
}

bool Config::save() {
  return storage().save(s_config);
}

void Config::resetToDefaults() {
  s_config.applyDefaults();
}

ConfigStorage &Config::storage() {
  static ConfigStorage s{
      /*slotSize=*/CAT24C325::TOTAL_SIZE / 2,
      /*slotAOffset=*/0,
      /*slotBOffset=*/CAT24C325::TOTAL_SIZE / 2,
      /*configVersion=*/1,
  };
  return s;
}
