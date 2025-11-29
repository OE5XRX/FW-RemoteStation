#ifndef HAL_STM32_CAT24C32_H_
#define HAL_STM32_CAT24C32_H_

#include <cstddef>
#include <cstdint>

#include "../cat24c32.h"

#include <stm32f3xx_hal.h>

class CAT24C325_stm32 : public CAT24C325 {
public:
  static constexpr uint8_t CAT24C32_DEFAULT_ADDR = 0x50;

  // i2c: Pointer auf HAL I2C Handle
  // devAddr7: 7-bit I2C device address (e.g. 0x50 typical for CAT24C32)
  // writeCycleMs: delay after page write to allow internal EEPROM write (0 = no delay)
  CAT24C325_stm32(I2C_HandleTypeDef *i2c, uint8_t devAddr7 = CAT24C32_DEFAULT_ADDR, unsigned writeCycleMs = 5, uint32_t timeoutMs = 100);
  ~CAT24C325_stm32() override = default;

  // CAT24C325 interface
  std::size_t write(std::uint16_t addr, const std::uint8_t *data, std::size_t len) override;
  std::size_t read(std::uint16_t addr, std::uint8_t *buf, std::size_t len) override;

private:
  I2C_HandleTypeDef *_i2c;
  uint8_t            _devAddr7;
  unsigned           _writeCycleMs;
  uint32_t           _timeoutMs;

  // internal helpers
  std::size_t writePage(std::uint16_t addr, const std::uint8_t *data, std::size_t len);
};

#endif
