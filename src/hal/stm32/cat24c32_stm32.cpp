#include "cat24c32_stm32.h"

#include <algorithm>
#include <cstring>

#include <FreeRTOS/Task.hpp>

CAT24C325_stm32::CAT24C325_stm32(I2C_HandleTypeDef *i2c, uint8_t devAddr7, unsigned writeCycleMs, uint32_t timeoutMs) : _i2c(i2c), _devAddr7(devAddr7), _writeCycleMs(writeCycleMs), _timeoutMs(timeoutMs) {
}

std::size_t CAT24C325_stm32::writePage(std::uint16_t addr, const std::uint8_t *data, std::size_t len) {
  // write up to single page (caller ensures addr within bounds and len <= page remainder)
  if (!_i2c || !data || len == 0) {
    return 0;
  }
  uint16_t devAddr8 = static_cast<uint16_t>(_devAddr7 << 1);
  // Mem address is 16-bit (two bytes), use I2C_MEMADD_SIZE_16BIT
  HAL_StatusTypeDef rc = HAL_I2C_Mem_Write(_i2c, devAddr8, addr, I2C_MEMADD_SIZE_16BIT, const_cast<uint8_t *>(data), static_cast<uint16_t>(len), _timeoutMs);
  if (rc != HAL_OK) {
    return 0;
  }
  if (_writeCycleMs > 0) {
    vTaskDelay(pdMS_TO_TICKS(_writeCycleMs));
  }
  return len;
}

std::size_t CAT24C325_stm32::write(std::uint16_t addr, const std::uint8_t *data, std::size_t len) {
  if (!data || len == 0) {
    return 0;
  }
  if (addr >= SIZE) {
    return 0;
  }
  std::size_t maxlen  = SIZE - addr;
  std::size_t towrite = std::min(len, maxlen);

  std::size_t written = 0;
  while (written < towrite) {
    std::size_t pageOffset = (addr + written) % PAGE_SIZE;
    std::size_t pageRemain = PAGE_SIZE - pageOffset;
    std::size_t chunk      = std::min(pageRemain, towrite - written);

    std::size_t w = writePage(static_cast<std::uint16_t>(addr + written), data + written, chunk);
    if (w == 0) {
      break; // hardware error
    }
    written += w;
    // next chunk will handle page boundary and delay
  }
  return written;
}

std::size_t CAT24C325_stm32::read(std::uint16_t addr, std::uint8_t *buf, std::size_t len) {
  if (!buf || len == 0) {
    return 0;
  }
  if (addr >= SIZE) {
    return 0;
  }
  std::size_t maxlen = SIZE - addr;
  std::size_t toread = std::min(len, maxlen);

  uint16_t          devAddr8 = static_cast<uint16_t>(_devAddr7 << 1);
  HAL_StatusTypeDef rc       = HAL_I2C_Mem_Read(_i2c, devAddr8, addr, I2C_MEMADD_SIZE_16BIT, buf, static_cast<uint16_t>(toread), _timeoutMs);
  if (rc != HAL_OK) {
    return 0;
  }
  return toread;
}
