#include <CppUTest/TestHarness.h>

#include "hal/host/cat24c32_sim.h"

#include <cstdio>
#include <vector>

TEST_GROUP(Cat24c32SimGroup){};

TEST(Cat24c32SimGroup, DefaultMemoryIsFF) {
  CAT24C325_Sim sim(0); // no write delay for tests

  std::vector<uint8_t> buf(CAT24C325::SIZE);
  std::size_t          r = sim.read(0, buf.data(), buf.size());
  LONGS_EQUAL(CAT24C325::SIZE, r);

  for (std::size_t i = 0; i < r; ++i) {
    LONGS_EQUAL(0xFF, buf[i]);
  }
}

TEST(Cat24c32SimGroup, WriteAndReadBack) {
  CAT24C325_Sim sim(0);

  const std::size_t    len = 64;
  std::vector<uint8_t> src(len);
  for (std::size_t i = 0; i < len; ++i) {
    src[i] = static_cast<uint8_t>(i & 0xFF);
  }

  std::size_t written = sim.write(0x10, src.data(), src.size());
  LONGS_EQUAL(len, written);

  std::vector<uint8_t> dst(len);
  std::size_t          read = sim.read(0x10, dst.data(), dst.size());
  LONGS_EQUAL(len, read);

  MEMCMP_EQUAL(src.data(), dst.data(), len);
}

TEST(Cat24c32SimGroup, PageBoundaryWrite) {
  CAT24C325_Sim sim(0);

  // choose address near page end to force crossing a page boundary
  std::uint16_t        addr = static_cast<std::uint16_t>(CAT24C325::PAGE_SIZE - 4);
  const std::size_t    len  = 16;
  std::vector<uint8_t> src(len);
  for (std::size_t i = 0; i < len; ++i) {
    src[i] = static_cast<uint8_t>(0xA0 + i);
  }

  std::size_t written = sim.write(addr, src.data(), src.size());
  LONGS_EQUAL(len, written);

  std::vector<uint8_t> dst(len);
  std::size_t          read = sim.read(addr, dst.data(), dst.size());
  LONGS_EQUAL(len, read);
  MEMCMP_EQUAL(src.data(), dst.data(), len);
}

TEST(Cat24c32SimGroup, WriteClampsAtDeviceEnd) {
  CAT24C325_Sim sim(0);

  std::uint16_t        addr = static_cast<std::uint16_t>(CAT24C325::SIZE - 10);
  const std::size_t    want = 20;
  std::vector<uint8_t> src(want, 0x55);

  std::size_t written = sim.write(addr, src.data(), src.size());
  // should only write up to device end (10 bytes)
  LONGS_EQUAL(10, written);

  std::vector<uint8_t> dst(20, 0);
  std::size_t          read = sim.read(addr, dst.data(), dst.size());
  LONGS_EQUAL(10, read);
  for (std::size_t i = 0; i < read; ++i) {
    LONGS_EQUAL(0x55, dst[i]);
  }
}

TEST(Cat24c32SimGroup, DumpAndLoadFile) {
  CAT24C325_Sim sim1(0);

  // write a small pattern
  std::vector<uint8_t> src(128);
  for (std::size_t i = 0; i < src.size(); ++i) {
    src[i] = static_cast<uint8_t>(i ^ 0x5A);
  }
  sim1.write(100, src.data(), src.size());

  const char *tmpfile = "/tmp/cat24c32_sim_test.bin";
  // ensure no leftover file
  std::remove(tmpfile);

  sim1.dumpToFile(tmpfile);

  CAT24C325_Sim sim2(0);
  sim2.loadFromFile(tmpfile);

  std::vector<uint8_t> dst(src.size());
  std::size_t          read = sim2.read(100, dst.data(), dst.size());
  LONGS_EQUAL(src.size(), read);
  MEMCMP_EQUAL(src.data(), dst.data(), src.size());

  std::remove(tmpfile);
}
