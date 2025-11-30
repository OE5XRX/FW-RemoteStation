#ifndef CAT24C32_SIM_H_
#define CAT24C32_SIM_H_

#include <array>
#include <string>

#include "../cat24c32.h"

class CAT24C325_Sim : public CAT24C325 {
public:
  explicit CAT24C325_Sim(unsigned writeCycleMs = 5);
  ~CAT24C325_Sim();

  std::size_t write(std::uint16_t addr, const std::uint8_t *data, std::size_t len) final;
  std::size_t read(std::uint16_t addr, std::uint8_t *buf, std::size_t len) final;

  void loadFromFile(const std::string &path);
  void dumpToFile(const std::string &path) const;

private:
  std::array<std::uint8_t, TOTAL_SIZE> _mem;
  unsigned                             _writeCycleMs;

  void reset(uint8_t fill = 0xFF);
};

#endif
