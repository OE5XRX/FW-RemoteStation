#include <CppUTest/TestHarness.h>

#include "fixed_string.h"

TEST_GROUP(StringTestGroup){};

TEST(StringTestGroup, Empty_Constructor) {
  FixedString<5> empty;
  STRCMP_EQUAL("", empty.c_str());
  LONGS_EQUAL(0, empty.size());
  CHECK_TRUE(empty.empty());
}

TEST(StringTestGroup, CString_Constructor) {
  FixedString<15> with_data("hello world!");
  STRCMP_EQUAL("hello world!", with_data.c_str());
  LONGS_EQUAL(12, with_data.size());

  FixedString<15> copy_data(with_data);
  STRCMP_EQUAL("hello world!", copy_data.c_str());
  LONGS_EQUAL(12, copy_data.size());
}

TEST(StringTestGroup, Copy_Constructor) {
  FixedString<15> with_data("hello world!");
  FixedString<15> copy_data(with_data);
  STRCMP_EQUAL("hello world!", copy_data.c_str());
  LONGS_EQUAL(12, copy_data.size());
}

TEST(StringTestGroup, Move_Constructor) {
  FixedString<15> src("hello");
  FixedString<15> dst(std::move(src));
  STRCMP_EQUAL("hello", dst.c_str());
  LONGS_EQUAL(5, dst.size());
  // src should be empty after move
  STRCMP_EQUAL("", src.c_str());
  LONGS_EQUAL(0, src.size());
  CHECK_TRUE(src.empty());
}

TEST(StringTestGroup, Move_Assignment) {
  FixedString<15> src("moved");
  FixedString<15> dst("original");
  dst = std::move(src);
  STRCMP_EQUAL("moved", dst.c_str());
  LONGS_EQUAL(5, dst.size());
  // src should be empty after move
  STRCMP_EQUAL("", src.c_str());
  LONGS_EQUAL(0, src.size());
}

TEST(StringTestGroup, Append_Char_And_CString) {
  FixedString<10> s("A");
  s.push_back('B');
  STRCMP_EQUAL("AB", s.c_str());
  s.append("CDE");
  STRCMP_EQUAL("ABCDE", s.c_str());
  LONGS_EQUAL(5, s.size());
}

TEST(StringTestGroup, Operator_Plus_And_PlusEquals) {
  FixedString<10> a("foo");
  FixedString<10> b("bar");
  FixedString<10> c = a + b;
  STRCMP_EQUAL("foobar", c.c_str());
  // original operands unchanged
  STRCMP_EQUAL("foo", a.c_str());
  STRCMP_EQUAL("bar", b.c_str());

  a += b;
  STRCMP_EQUAL("foobar", a.c_str());
}

TEST(StringTestGroup, Pop_Back_And_Clear) {
  FixedString<8> s("abc");
  s.pop_back();
  STRCMP_EQUAL("ab", s.c_str());
  s.pop_back();
  s.pop_back();
  STRCMP_EQUAL("", s.c_str());
  LONGS_EQUAL(0, s.size());
  // popping empty should remain safe
  s.pop_back();
  STRCMP_EQUAL("", s.c_str());
  s.push_back('x');
  STRCMP_EQUAL("x", s.c_str());
  s.clear();
  STRCMP_EQUAL("", s.c_str());
}

TEST(StringTestGroup, At_Bounds) {
  FixedString<6> s("xyz");
  LONGS_EQUAL('x', s.at(0));
  LONGS_EQUAL('y', s.at(1));
  LONGS_EQUAL('z', s.at(2));
  LONGS_EQUAL(0, s.at(3));   // out of bounds -> 0
  LONGS_EQUAL(0, s.at(100)); // large index safe
}

TEST(StringTestGroup, Operator_Bracket_Access) {
  FixedString<8> s("test");
  LONGS_EQUAL('t', s[0]);
  LONGS_EQUAL('e', s[1]);
  LONGS_EQUAL('s', s[2]);
  LONGS_EQUAL('t', s[3]);
  LONGS_EQUAL(0, s[4]); // out of bounds -> 0
}

TEST(StringTestGroup, Find_Character) {
  FixedString<11> s("hello");
  // find returns position or CAPACITY+1 if not found
  LONGS_EQUAL(0, s.find('h'));
  LONGS_EQUAL(1, s.find('e'));
  LONGS_EQUAL(4, s.find('o'));
  CHECK(s.find('x') == 11); // CAPACITY+1 for not found (CAPACITY=10)

  FixedString<5> empty;
  CHECK(empty.find('a') == 5); // empty string returns CAPACITY+1
}

TEST(StringTestGroup, Empty_Check) {
  FixedString<8> s;
  CHECK_TRUE(s.empty());
  s.append("a");
  CHECK_FALSE(s.empty());
  s.clear();
  CHECK_TRUE(s.empty());
}

TEST(StringTestGroup, Capacity_Does_Not_Overflow) {
  const char     *large = "0123456789ABCDEF"; // > capacity 10
  FixedString<11> s;
  s.append(large);
  // size must be <= capacity
  LONGS_EQUAL(10, s.size());
  // data must be nul-terminated
  CHECK(s.c_str()[s.size()] == '\0');
}

TEST(StringTestGroup, Equals_Behavior) {
  FixedString<10> s("match");
  CHECK_TRUE(s.equals("match"));
  CHECK_FALSE(s.equals("nope"));
  s.clear();
  CHECK_TRUE(s.equals(""));
}

TEST(StringTestGroup, Construct_With_Nullptr) {
  FixedString<8> s((const char *)nullptr);
  STRCMP_EQUAL("", s.c_str());
  LONGS_EQUAL(0, s.size());
}

TEST(StringTestGroup, Append_Nullptr_NoChange) {
  FixedString<8> s("a");
  s.append((const char *)nullptr);
  STRCMP_EQUAL("a", s.c_str());
  LONGS_EQUAL(1, s.size());
}

TEST(StringTestGroup, Equals_Nullptr_Returns_False) {
  FixedString<8> s("x");
  CHECK_FALSE(s.equals(nullptr));
}

TEST(StringTestGroup, Assign_From_Self_Becomes_Empty) {
  FixedString<8> s("self");
  s.assignFrom(s); // clears then appends -> results empty
  STRCMP_EQUAL("", s.c_str());
  LONGS_EQUAL(0, s.size());
}

TEST(StringTestGroup, Operator_Plus_Truncates_To_Capacity) {
  FixedString<6> a("123");
  FixedString<6> b("4567");
  FixedString<6> c = a + b;
  LONGS_EQUAL(5, c.size());
  STRCMP_EQUAL("12345", c.c_str());
}

TEST(StringTestGroup, PlusEquals_Truncates) {
  FixedString<6> a("123");
  FixedString<6> b("4567");
  a += b;
  LONGS_EQUAL(5, a.size());
  STRCMP_EQUAL("12345", a.c_str());
}

TEST(StringTestGroup, Append_One_By_One_To_Fill) {
  FixedString<5> s;
  s.push_back('a');
  s.push_back('b');
  s.push_back('c');
  s.push_back('d');
  LONGS_EQUAL(4, s.size());
  STRCMP_EQUAL("abcd", s.c_str());
  s.push_back('e'); // should not change when full
  LONGS_EQUAL(4, s.size());
  STRCMP_EQUAL("abcd", s.c_str());
}

TEST(StringTestGroup, Equals_Case_Sensitive) {
  FixedString<10> s("Hello");
  CHECK_FALSE(s.equals("hello"));
  CHECK_TRUE(s.equals("Hello"));
}

TEST(StringTestGroup, Assign_From_FixedString_Copies_Value) {
  FixedString<16> src("original");
  FixedString<16> dst;
  dst = src;
  STRCMP_EQUAL("original", dst.c_str());
  LONGS_EQUAL(8, dst.size());

  // changes to src must not affect dst (deep copy semantics)
  src.push_back('X');
  CHECK(strcmp(src.c_str(), dst.c_str()) != 0);
}

TEST(StringTestGroup, Assignment_From_CString) {
  FixedString<16> s;
  s = "hello";
  STRCMP_EQUAL("hello", s.c_str());
  LONGS_EQUAL(5, s.size());
}

TEST(StringTestGroup, Self_Assignment_Is_Safe) {
  FixedString<16> s("self");
  s = s; // self assignment should not corrupt
  STRCMP_EQUAL("self", s.c_str());
  LONGS_EQUAL(4, s.size());
}

TEST(StringTestGroup, Assignment_Nullptr_Clears_String) {
  FixedString<8> s("keep");
  s = (const char *)nullptr;
  STRCMP_EQUAL("", s.c_str());
  LONGS_EQUAL(0, s.size());
}

TEST(StringTestGroup, Equality_Operator_True_False) {
  FixedString<12> a("match");
  FixedString<12> b("match");
  FixedString<12> c("different");

  CHECK_TRUE(a == b);
  CHECK_FALSE(a == c);

  // empty equality
  FixedString<4> e1;
  FixedString<4> e2;
  CHECK_TRUE(e1 == e2);
}

TEST(StringTestGroup, Assign_FixedString_Truncates_To_Capacity) {
  FixedString<6> a("123");
  FixedString<6> b("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  a = b; // assign long fixedstring -> truncated
  LONGS_EQUAL(5, a.size());
  STRCMP_EQUAL("ABCDE", a.c_str());
}

TEST(StringTestGroup, Assignment_CString_Truncates_To_Capacity) {
  FixedString<5> s;
  s = "overflow";
  LONGS_EQUAL(4, s.size());
  STRCMP_EQUAL("over", s.c_str());
}

TEST(StringTestGroup, Iterator_Support) {
  FixedString<10> s("abc");
  // test const_iterator begin/end
  int count = std::distance(s.cbegin(), s.cend());
  LONGS_EQUAL(3, count);

  // test range-based for loop
  count = 0;
  for (char c : s) {
    (void)c; // use variable to avoid warning
    count++;
  }
  LONGS_EQUAL(3, count);
}

TEST(StringTestGroup, Iterator_Empty_String) {
  FixedString<5> s;
  POINTERS_EQUAL(s.cbegin(), s.cend());
}

TEST(StringTestGroup, Append_FixedString_Argument) {
  FixedString<12> a("hello");
  FixedString<12> b(" world");
  a.append(b);
  STRCMP_EQUAL("hello world", a.c_str());
  LONGS_EQUAL(11, a.size());
}
