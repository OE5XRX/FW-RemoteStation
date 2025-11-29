#include <CppUTest/TestHarness.h>

#include "shell/history.h"

TEST_GROUP(HistoryTest){};

TEST(HistoryTest, EmptyGetReturnsEmpty) {
  History     h;
  LINE_STRING s = h.get(0);
  STRCMP_EQUAL("", s.c_str());
  LONGS_EQUAL(0, s.size());
}

TEST(HistoryTest, AddAndGetNewest) {
  History     h;
  LINE_STRING one("first");
  h.add(one);

  LINE_STRING latest = h.get(0);
  STRCMP_EQUAL("first", latest.c_str());
  LONGS_EQUAL(5, latest.size());
}

TEST(HistoryTest, NavigateHistoryBounds) {
  History h;
  h.add(LINE_STRING("one"));
  h.add(LINE_STRING("two"));
  h.add(LINE_STRING("three"));

  // newest
  STRCMP_EQUAL("three", h.get(0).c_str());
  // older
  STRCMP_EQUAL("two", h.get(1).c_str());
  STRCMP_EQUAL("one", h.get(1).c_str());
  // further older stays clamped to oldest available
  STRCMP_EQUAL("one", h.get(1).c_str());
  // go back towards newest
  STRCMP_EQUAL("two", h.get(-1).c_str());
  STRCMP_EQUAL("three", h.get(-1).c_str());
  // further newer stays clamped to newest
  STRCMP_EQUAL("three", h.get(-1).c_str());
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
  STRCMP_EQUAL("E12", h.get(0).c_str());

  // navigate to the last kept (oldest within depth)
  // index depth-1 should give the oldest retained entry: E( (depth+3)-depth ) == E3
  // call get(1) repeatedly to reach it
  for (size_t i = 1; i < depth; ++i) {
    h.get(1);
  }
  STRCMP_EQUAL("E3", h.get(0).c_str()); // after clamping, current index points to oldest retained
}

TEST(HistoryTest, AddEmptyLineAndDuplicates) {
  History h;
  h.add(LINE_STRING("dup"));
  h.add(LINE_STRING(""));
  h.add(LINE_STRING("dup"));

  // newest is last "dup"
  STRCMP_EQUAL("dup", h.get(0).c_str());
  // older is empty string
  STRCMP_EQUAL("", h.get(1).c_str());
  // older is first "dup"
  STRCMP_EQUAL("dup", h.get(1).c_str());
}

TEST(HistoryTest, ResetResetsIndex) {
  History h;
  h.add(LINE_STRING("a"));
  h.add(LINE_STRING("b"));

  // walk to older entry
  STRCMP_EQUAL("b", h.get(0).c_str());
  STRCMP_EQUAL("a", h.get(1).c_str());

  h.reset();
  // after reset, get(0) should return newest again
  STRCMP_EQUAL("b", h.get(0).c_str());
}
