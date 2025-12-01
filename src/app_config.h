#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

#include <cstdint>

#include "hal/cat24c32.h"

class AppConfig {
public:
  AppConfig() : volume(0), frequency(0.0f), txEnable(false), dummy(0) {
  }

  AppConfig(uint8_t vol, float freq, bool txEn, uint32_t dmy) : volume(vol), frequency(freq), txEnable(txEn), dummy(dmy) {
  }

  void applyDefaults() {
    volume    = 50;
    frequency = 145.500f;
    txEnable  = false;
  }

  uint8_t  volume;
  float    frequency;
  bool     txEnable;
  uint32_t dummy;
};

static_assert(sizeof(AppConfig) <= CAT24C325::TOTAL_SIZE / 2, "Unexpected padding!");

#endif
