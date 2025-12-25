#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void) {
  printk("Hello from Zephyr native_sim!\n");
  return 0;
}
