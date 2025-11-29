#ifndef CAT24C32_H_
#define CAT24C32_H_

#include <cstddef>
#include <cstdint>

#include "i2c_device.h"

class CAT24C325 : public II2cDevice {
public:
  static constexpr std::size_t SIZE      = 4096;
  static constexpr std::size_t PAGE_SIZE = 32;
};

#endif
