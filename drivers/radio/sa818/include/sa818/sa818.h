#ifndef ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_H_
#define ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_H_

#include <stdbool.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

enum sa818_device_power {
  SA818_DEVICE_OFF = 0,
  SA818_DEVICE_ON = 1,
};

int sa818_set_power(const struct device *dev, sa818_device_power power_state);

enum sa818_ptt_state {
  SA818_PTT_OFF = 0,
  SA818_PTT_ON = 1,
};

int sa818_set_ptt(const struct device *dev, sa818_ptt_state ptt_state);

enum sa818_power_level {
  SA818_POWER_LOW = 0,
  SA818_POWER_HIGH = 1,
};

int sa818_set_power_level(const struct device *dev, sa818_power_level power_level);

bool sa818_is_squelch_open(const struct device *dev);

struct sa818_status {
  sa818_device_power device_power;
  sa818_ptt_state ptt_state;
  sa818_power_level power_level;
  bool squelch;
};

sa818_status sa818_get_status(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_H_ */
