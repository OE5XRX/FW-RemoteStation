#include <sa818/sa818.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void) {
  printk("FM Board booted\n");
  printk("===========================================\n");
  printk("Initialisiere SA818 Treiber...\n");

  const struct device *sa = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sa818));
  if (sa == NULL) {
    printk("FEHLER: SA818 DT node nicht gefunden / nicht aktiviert\n");
    printk("===========================================\n");
    return -1;
  }
  printk("OK: SA818 Device-Tree Node gefunden\n");

  if (!device_is_ready(sa)) {
    printk("FEHLER: SA818 Device nicht bereit\n");
    printk("===========================================\n");
    return -1;
  }
  printk("OK: SA818 Device ist bereit\n");
  printk("OK: SA818 Treiber erfolgreich geladen\n");
  printk("===========================================\n");

  printk("Schalte SA818 ein...\n");
  sa818_power_on(sa);

  printk("Setze Sendeleistung auf HIGH...\n");
  sa818_set_high_power(sa, true);

  printk("SA818 Initialisierung abgeschlossen\n");

  return 0;
}
