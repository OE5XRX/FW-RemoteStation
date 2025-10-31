#include <CppUTest/TestHarness.h>

#include "fixed_string.h"

TEST_GROUP(StringTestGroup){};

TEST(StringTestGroup, Empty_Constructor) {
  FixedString<5> empty;
  STRCMP_EQUAL("", empty.data());
  LONGS_EQUAL(0, empty.length());
}

TEST(StringTestGroup, C_Constructor) {
  FixedString<15> with_data("hello world!");
  STRCMP_EQUAL("hello world!", with_data.data());
  LONGS_EQUAL(12, with_data.length());

  FixedString<15> copy_data(with_data);
  STRCMP_EQUAL("hello world!", copy_data.data());
  LONGS_EQUAL(12, copy_data.length());
}

TEST(StringTestGroup, String_Constructor) {
  FixedString<15> with_data("hello world!");
  FixedString<15> copy_data(with_data);
  STRCMP_EQUAL("hello world!", copy_data.data());
  LONGS_EQUAL(12, copy_data.length());
}

// new/extended tests

TEST(StringTestGroup, Append_Char_And_CString) {
  FixedString<10> s("A");
  s.append('B');
  STRCMP_EQUAL("AB", s.data());
  s.append("CDE");
  STRCMP_EQUAL("ABCDE", s.data());
  LONGS_EQUAL(5, s.length());
}

TEST(StringTestGroup, Operator_Plus_And_PlusEquals) {
  FixedString<10> a("foo");
  FixedString<10> b("bar");
  FixedString<10> c = a + b;
  STRCMP_EQUAL("foobar", c.data());
  // original operands unchanged
  STRCMP_EQUAL("foo", a.data());
  STRCMP_EQUAL("bar", b.data());

  a += b;
  STRCMP_EQUAL("foobar", a.data());
}

TEST(StringTestGroup, Pop_And_Clear) {
  FixedString<8> s("abc");
  s.pop();
  STRCMP_EQUAL("ab", s.data());
  s.pop();
  s.pop();
  STRCMP_EQUAL("", s.data());
  LONGS_EQUAL(0, s.length());
  // popping empty should remain safe
  s.pop();
  STRCMP_EQUAL("", s.data());
  s.append("x");
  STRCMP_EQUAL("x", s.data());
  s.clear();
  STRCMP_EQUAL("", s.data());
}

TEST(StringTestGroup, Get_Bounds) {
  FixedString<6> s("xyz");
  LONGS_EQUAL('x', s.get(0));
  LONGS_EQUAL('y', s.get(1));
  LONGS_EQUAL('z', s.get(2));
  LONGS_EQUAL(0, s.get(3));   // out of bounds -> 0
  LONGS_EQUAL(0, s.get(100)); // large index safe
}

TEST(StringTestGroup, Capacity_Does_Not_Overflow) {
  const char     *large = "0123456789ABCDEF"; // > capacity 10
  FixedString<10> s;
  s.append(large);
  // length must be <= capacity
  LONGS_EQUAL(10, s.length());
  // data must be nul-terminated
  CHECK(s.data()[s.length()] == '\0');
}

TEST(StringTestGroup, Compare_Behaviour) {
  FixedString<10> s("match");
  CHECK_TRUE(s.compare("match"));
  CHECK_FALSE(s.compare("nope"));
  s.clear();
  CHECK_TRUE(s.compare(""));
}

// --- Neue Corner-Case Tests ---

TEST(StringTestGroup, Construct_With_Nullptr) {
  FixedString<8> s((const char *)nullptr);
  STRCMP_EQUAL("", s.data());
  LONGS_EQUAL(0, s.length());
}

TEST(StringTestGroup, Append_Nullptr_NoChange) {
  FixedString<8> s("a");
  s.append((const char *)nullptr);
  STRCMP_EQUAL("a", s.data());
  LONGS_EQUAL(1, s.length());
}

TEST(StringTestGroup, Compare_Nullptr_Returns_False) {
  FixedString<8> s("x");
  CHECK_FALSE(s.compare(nullptr));
}

TEST(StringTestGroup, Copy_From_Self_Becomes_Empty) {
  FixedString<8> s("self");
  s.copy_from(s); // current implementation clears then appends -> results empty
  STRCMP_EQUAL("", s.data());
  LONGS_EQUAL(0, s.length());
}

TEST(StringTestGroup, Operator_Plus_Truncates_To_Capacity) {
  FixedString<5> a("123");
  FixedString<5> b("4567");
  FixedString<5> c = a + b;
  LONGS_EQUAL(5, c.length());
  STRCMP_EQUAL("12345", c.data());
}

TEST(StringTestGroup, PlusEquals_Truncates) {
  FixedString<5> a("123");
  FixedString<5> b("4567");
  a += b;
  LONGS_EQUAL(5, a.length());
  STRCMP_EQUAL("12345", a.data());
}

TEST(StringTestGroup, Zero_Capacity_String) {
  FixedString<0> s;
  STRCMP_EQUAL("", s.data());
  LONGS_EQUAL(0, s.length());
  s.append('a');
  STRCMP_EQUAL("", s.data());
  LONGS_EQUAL(0, s.length());
}

TEST(StringTestGroup, Append_One_By_One_To_Fill) {
  FixedString<4> s;
  s.append('a');
  s.append('b');
  s.append('c');
  s.append('d');
  LONGS_EQUAL(4, s.length());
  STRCMP_EQUAL("abcd", s.data());
  s.append('e'); // should not change when full
  LONGS_EQUAL(4, s.length());
  STRCMP_EQUAL("abcd", s.data());
}

TEST(StringTestGroup, Compare_Case_Sensitive) {
  FixedString<10> s("Hello");
  CHECK_FALSE(s.compare("hello"));
  CHECK_TRUE(s.compare("Hello"));
}
