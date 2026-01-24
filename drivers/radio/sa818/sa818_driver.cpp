
#include <sa818/sa818.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sa818, LOG_LEVEL_INF);

#define SA818_NODE DT_ALIAS(sa818)

static int sa818_init_device(void) {
  const struct device *sa818 = DEVICE_DT_GET(SA818_NODE);
  if (!device_is_ready(sa818)) {
    LOG_ERR("SA818 device not ready");
    return -ENODEV;
  }

  if (sa818_set_ptt(sa818, SA818_PTT_OFF) != SA818_OK) {
    LOG_ERR("Failed to disable PTT during init");
    return -EIO;
  }

  if (sa818_set_power(sa818, SA818_DEVICE_OFF) != SA818_OK) {
    LOG_ERR("Failed to power off SA818 during init");
    return -EIO;
  }
  LOG_INF("SA818 device ready");

  return 0;
}

SYS_INIT(sa818_init_device, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
