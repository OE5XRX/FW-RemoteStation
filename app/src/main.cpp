#include <sa818/sa818.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void) {
  printk("FM Board booted\n");
  printk("===========================================\n");
  printk("Initializing SA818 driver...\n");

  const struct device *sa = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sa818));
  if (sa == NULL) {
    printk("ERROR: SA818 DT node not found / not enabled\n");
    printk("===========================================\n");
    return -1;
  }
  printk("OK: SA818 Device Tree Node found\n");

  if (!device_is_ready(sa)) {
    printk("ERROR: SA818 Device not ready\n");
    printk("===========================================\n");
    return -1;
  }
  printk("OK: SA818 Device is ready\n");
  printk("OK: SA818 driver successfully loaded\n");
  printk("===========================================\n");

  printk("Powering on SA818...\n");
  enum sa818_result result = sa818_set_power(sa, SA818_DEVICE_ON);
  if (result != SA818_OK) {
    printk("ERROR: Could not turn on SA818\n");
    return -1;
  }

  printk("Setting transmit power to HIGH...\n");
  result = sa818_set_power_level(sa, SA818_POWER_HIGH);
  if (result != SA818_OK) {
    printk("ERROR: Could not set SA818 power\n");
    return -1;
  }

  printk("SA818 initialization complete\n");

  return 0;
}
