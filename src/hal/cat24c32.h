#ifndef CAT24C32_H_
#define CAT24C32_H_

#include <cstddef>
#include <cstdint>

class CAT24C325 {
public:
  static constexpr std::size_t SIZE      = 4096; // 32 Kbit = 4096 bytes
  static constexpr std::size_t PAGE_SIZE = 32;   // typical page size

  virtual ~CAT24C325() = default;

  virtual std::size_t write(std::uint16_t addr, const std::uint8_t *data, std::size_t len) = 0;
  virtual std::size_t read(std::uint16_t addr, std::uint8_t *buf, std::size_t len)         = 0;

  static CAT24C325 &instance();
};

#endif
