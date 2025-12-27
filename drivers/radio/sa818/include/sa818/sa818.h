#ifndef ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_H_
#define ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_H_

#include <stdbool.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

int sa818_power_on(const struct device *dev);
int sa818_power_off(const struct device *dev);

int sa818_set_ptt(const struct device *dev, bool enable);
int sa818_set_high_power(const struct device *dev, bool high);

bool sa818_is_squelch_open(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_H_ */
