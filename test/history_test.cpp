#include <CppUTest/TestHarness.h>

#include "shell/history.h"

TEST_GROUP(HistoryTest){};

TEST(HistoryTest, EmptyGetReturnsEmpty) {
  History     h;
  LINE_STRING s = h.get(0);
  STRCMP_EQUAL("", s.data());
  LONGS_EQUAL(0, s.length());
}

TEST(HistoryTest, AddAndGetNewest) {
  History     h;
  LINE_STRING one("first");
  h.add(one);

  LINE_STRING latest = h.get(0);
  STRCMP_EQUAL("first", latest.data());
  LONGS_EQUAL(5, latest.length());
}

TEST(HistoryTest, NavigateHistoryBounds) {
  History h;
  h.add(LINE_STRING("one"));
  h.add(LINE_STRING("two"));
  h.add(LINE_STRING("three"));

  // newest
  STRCMP_EQUAL("three", h.get(0).data());
  // older
  STRCMP_EQUAL("two", h.get(1).data());
  STRCMP_EQUAL("one", h.get(1).data());
  // further older stays clamped to oldest available
  STRCMP_EQUAL("one", h.get(1).data());
  // go back towards newest
  STRCMP_EQUAL("two", h.get(-1).data());
  STRCMP_EQUAL("three", h.get(-1).data());
  // further newer stays clamped to newest
  STRCMP_EQUAL("three", h.get(-1).data());
}

TEST(HistoryTest, OverflowDropsOldest) {
  History      h;
  const size_t depth = CLI_HISTORY_DEPTH;

  // add depth + 3 entries
  for (size_t i = 0; i < depth + 3; ++i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "E%zu", i);
    h.add(LINE_STRING(buf));
  }

  // newest should be last added
  STRCMP_EQUAL("E12", h.get(0).data());

  // navigate to the last kept (oldest within depth)
  // index depth-1 should give the oldest retained entry: E( (depth+3)-depth ) == E3
  // call get(1) repeatedly to reach it
  for (size_t i = 1; i < depth; ++i) {
    h.get(1);
  }
  STRCMP_EQUAL("E3", h.get(0).data()); // after clamping, current index points to oldest retained
}

TEST(HistoryTest, AddEmptyLineAndDuplicates) {
  History h;
  h.add(LINE_STRING("dup"));
  h.add(LINE_STRING(""));
  h.add(LINE_STRING("dup"));

  // newest is last "dup"
  STRCMP_EQUAL("dup", h.get(0).data());
  // older is empty string
  STRCMP_EQUAL("", h.get(1).data());
  // older is first "dup"
  STRCMP_EQUAL("dup", h.get(1).data());
}

TEST(HistoryTest, ResetResetsIndex) {
  History h;
  h.add(LINE_STRING("a"));
  h.add(LINE_STRING("b"));

  // walk to older entry
  STRCMP_EQUAL("b", h.get(0).data());
  STRCMP_EQUAL("a", h.get(1).data());

  h.reset();
  // after reset, get(0) should return newest again
  STRCMP_EQUAL("b", h.get(0).data());
}
