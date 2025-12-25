#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void) {
  printk("FM Board booted\n");
  return 0;
}
