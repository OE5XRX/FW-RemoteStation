#ifndef FIXED_STRING_H_
#define FIXED_STRING_H_

#include <cstddef>
#include <cstring>

template <size_t N> class FixedString {
public:
  FixedString() {
    _data[0] = '\0';
  }

  FixedString(char const *const data) {
    _data[0] = '\0';
    append(data);
  }

  FixedString(const FixedString &data) {
    _data[0] = '\0';
    append(data);
  }

  // Zuweisungsoperatoren
  FixedString &operator=(const FixedString &other) {
    if (this != &other) {
      _data[0] = '\0';
      append(other);
    }
    return *this;
  }

  FixedString &operator=(char const *const data) {
    _data[0] = '\0';
    append(data);
    return *this;
  }

  // return a new FixedString which is concatenation of lhs and rhs
  FixedString operator+(const FixedString &data) const {
    FixedString tmp(*this);
    tmp.append(data);
    return tmp;
  }

  FixedString &operator+=(const FixedString &data) {
    append(data);
    return *this;
  }

  bool compare(const char *data) const {
    if (data == nullptr)
      return false;
    return std::strcmp(this->_data, data) == 0;
  }

  void append(char c) {
    size_t len = length();
    if (len < N) {
      _data[len]     = c;
      _data[len + 1] = '\0';
    }
  }

  void append(const FixedString &data) {
    append(data._data);
  }

  void append(char const *const data) {
    if (data == nullptr)
      return;
    size_t i = 0;
    while (data[i]) {
      if (length() >= N)
        break;
      append(data[i]);
      i++;
    }
  }

  size_t length() const {
    for (size_t i = 0; i <= N; i++) {
      if (_data[i] == '\0') {
        return i;
      }
    }
    return N;
  }

  char get(const size_t pos) const {
    if (pos >= length()) {
      return 0;
    }
    return _data[pos];
  }

  char const *data() const {
    return _data;
  }

  void clear() {
    _data[0] = '\0';
  }

  void copy_from(const FixedString &s) {
    _data[0] = '\0';
    append(s);
  }

  void pop() {
    size_t len = length();
    if (len > 0) {
      _data[len - 1] = '\0';
    }
  }

  bool operator==(const FixedString &other) const {
    return compare(other._data);
  }

private:
  char _data[N + 1];
};

#endif
