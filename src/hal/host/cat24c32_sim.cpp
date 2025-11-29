#include "cat24c32_sim.h"

#include <algorithm>
#include <fstream>

#include <FreeRTOS/Task.hpp>

CAT24C325_Sim::CAT24C325_Sim(unsigned writeCycleMs) : _writeCycleMs(writeCycleMs) {
  reset();
}

CAT24C325_Sim::~CAT24C325_Sim() = default;

std::size_t CAT24C325_Sim::write(std::uint16_t addr, const std::uint8_t *data, std::size_t len) {
  if (!data || len == 0) {
    return 0;
  }

  // clamp length to device size
  if (addr >= SIZE) {
    return 0;
  }
  std::size_t maxlen  = SIZE - addr;
  std::size_t towrite = std::min(len, maxlen);

  // emulate page writes: wrap within page boundary
  std::size_t written = 0;
  while (written < towrite) {
    std::size_t pageOffset = (addr + written) % PAGE_SIZE;
    std::size_t pageRemain = PAGE_SIZE - pageOffset;
    std::size_t chunk      = std::min(pageRemain, towrite - written);

    std::copy_n(data + written, chunk, _mem.begin() + (addr + written));

    written += chunk;

    // emulate write cycle delay per page if configured
    if (_writeCycleMs > 0) {
      vTaskDelay(pdMS_TO_TICKS(_writeCycleMs));
    }
  }

  return written;
}

std::size_t CAT24C325_Sim::read(std::uint16_t addr, std::uint8_t *buf, std::size_t len) {
  if (!buf || len == 0) {
    return 0;
  }
  if (addr >= SIZE) {
    return 0;
  }
  std::size_t maxlen = SIZE - addr;
  std::size_t toread = std::min(len, maxlen);
  std::copy_n(_mem.begin() + addr, toread, buf);
  return toread;
}

void CAT24C325_Sim::loadFromFile(const std::string &path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) {
    throw std::runtime_error("open failed");
  }
  ifs.read(reinterpret_cast<char *>(_mem.data()), SIZE);
}

void CAT24C325_Sim::dumpToFile(const std::string &path) const {
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  if (!ofs) {
    throw std::runtime_error("open failed");
  }
  ofs.write(reinterpret_cast<const char *>(_mem.data()), SIZE);
}

void CAT24C325_Sim::reset(uint8_t fill) {
  _mem.fill(fill);
}
