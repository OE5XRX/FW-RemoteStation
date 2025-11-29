#ifndef HAL_I2C_DEVICE_H_
#define HAL_I2C_DEVICE_H_

#include <cstddef>
#include <cstdint>

class II2cDevice {
public:
  virtual ~II2cDevice() = default;

  virtual std::size_t write(std::uint16_t addr, const std::uint8_t *data, std::size_t len) = 0;
  virtual std::size_t read(std::uint16_t addr, std::uint8_t *buf, std::size_t len)         = 0;
};

#endif
