#ifndef SRC_CONFIG_H_
#define SRC_CONFIG_H_

#include "config_storage.h"

class Config {
public:
  static void init();

  static AppConfig       &current();
  static const AppConfig &currentConst();
  static const uint32_t  &currentSequence();

  static bool load();
  static bool save();

  static void resetToDefaults();

private:
  static ConfigStorage &storage();

  static AppConfig s_config;
  static uint32_t  s_sequence;
};

#endif
