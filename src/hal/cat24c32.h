#ifndef CAT24C32_H_
#define CAT24C32_H_

#include <cstddef>
#include <cstdint>

class CAT24C325 {
public:
  static constexpr std::size_t TOTAL_SIZE = 4096;
  static constexpr std::size_t PAGE_SIZE  = 32;

  virtual std::size_t write(std::uint16_t addr, const std::uint8_t *data, std::size_t len) = 0;
  virtual std::size_t read(std::uint16_t addr, std::uint8_t *buf, std::size_t len)         = 0;
  virtual void        reset(uint8_t fill = 0xFF)                                           = 0;

protected:
  bool rangeOk(uint32_t addr, std::size_t len) const {
    if (addr >= TOTAL_SIZE)
      return false;
    if (addr + len > TOTAL_SIZE)
      return false;
    return true;
  }
};

#endif
