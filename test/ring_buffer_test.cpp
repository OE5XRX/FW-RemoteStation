#include <CppUTest/TestHarness.h>

#include "ring_buffer.h"

TEST_GROUP(RingBufferTest) {
  RingBuffer<int, 4> buffer; // Puffer der Größe 4 für einfache Tests
};

TEST(RingBufferTest, InitiallyEmpty) {
  CHECK_TRUE(buffer.empty());
  CHECK_FALSE(buffer.full());
  CHECK_EQUAL(0, buffer.available());
  CHECK_EQUAL(3, buffer.free_space()); // N-1 nutzbare Elemente
}

TEST(RingBufferTest, SingleElement) {
  CHECK_TRUE(buffer.try_push(42));
  CHECK_FALSE(buffer.empty());
  CHECK_EQUAL(1, buffer.available());

  auto val = buffer.try_pop();
  CHECK_TRUE(val.has_value());
  CHECK_EQUAL(42, *val);
  CHECK_TRUE(buffer.empty());
}

TEST(RingBufferTest, FullBuffer) {
  CHECK_TRUE(buffer.try_push(1));
  CHECK_TRUE(buffer.try_push(2));
  CHECK_TRUE(buffer.try_push(3));
  CHECK_TRUE(buffer.full());
  CHECK_FALSE(buffer.try_push(4)); // Sollte fehlschlagen
}

TEST(RingBufferTest, FifoOrder) {
  buffer.try_push(1);
  buffer.try_push(2);
  buffer.try_push(3);

  auto val1 = buffer.try_pop();
  auto val2 = buffer.try_pop();
  auto val3 = buffer.try_pop();

  CHECK_EQUAL(1, *val1);
  CHECK_EQUAL(2, *val2);
  CHECK_EQUAL(3, *val3);
}

TEST(RingBufferTest, WrapAround) {
  // Fülle Buffer
  buffer.try_push(1);
  buffer.try_push(2);
  buffer.try_push(3);

  // Entferne zwei Elemente
  buffer.try_pop();
  buffer.try_pop();

  // Füge neue Elemente hinzu (wrap-around)
  CHECK_TRUE(buffer.try_push(4));
  CHECK_TRUE(buffer.try_push(5));

  auto val1 = buffer.try_pop();
  auto val2 = buffer.try_pop();
  auto val3 = buffer.try_pop();

  CHECK_EQUAL(3, *val1);
  CHECK_EQUAL(4, *val2);
  CHECK_EQUAL(5, *val3);
}

TEST(RingBufferTest, SizeCalculations) {
  buffer.try_push(1);
  buffer.try_push(2);

  CHECK_EQUAL(2, buffer.available());
  CHECK_EQUAL(1, buffer.free_space());

  buffer.try_pop();

  CHECK_EQUAL(1, buffer.available());
  CHECK_EQUAL(2, buffer.free_space());
}

TEST(RingBufferTest, PopEmpty) {
  auto val = buffer.try_pop();
  CHECK_FALSE(val.has_value());
}
