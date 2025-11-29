#ifndef FIXED_STRING_H_
#define FIXED_STRING_H_

#include <array>
#include <cstddef>
#include <cstring>
#include <type_traits>

template <size_t N> class FixedString {
public:
  static_assert(N > 0, "FixedString template parameter N must be greater than 0");

  using storage_t      = std::array<char, N>;
  using iterator       = typename storage_t::iterator;
  using const_iterator = typename storage_t::const_iterator;

  static constexpr std::size_t STORAGE_SIZE = N;
  static constexpr std::size_t CAPACITY     = (N > 0 ? N - 1 : 0);

  FixedString() noexcept : _length(0) {
    storage_[0] = '\0';
  }

  FixedString(const char *str) noexcept : _length(0) {
    storage_[0] = '\0';
    append(str);
  }

  FixedString(const FixedString &other) noexcept : _length(0) {
    storage_[0] = '\0';
    append(other);
  }

  FixedString(FixedString &&other) noexcept : _length(other._length) {
    std::copy(other.storage_.begin(), other.storage_.end(), storage_.begin());
    other._length     = 0;
    other.storage_[0] = '\0';
  }

  FixedString &operator=(FixedString &&other) noexcept {
    if (this != &other) {
      _length = other._length;
      std::copy(other.storage_.begin(), other.storage_.end(), storage_.begin());
      other._length     = 0;
      other.storage_[0] = '\0';
    }
    return *this;
  }

  FixedString &operator=(const FixedString &other) noexcept {
    if (this != &other) {
      storage_[0] = '\0';
      _length     = 0;
      append(other);
    }
    return *this;
  }

  FixedString &operator=(const char *str) noexcept {
    storage_[0] = '\0';
    _length     = 0;
    append(str);
    return *this;
  }

  FixedString operator+(const FixedString &rhs) const noexcept {
    FixedString tmp(*this);
    tmp.append(rhs);
    return tmp;
  }

  FixedString &operator+=(const FixedString &rhs) noexcept {
    append(rhs);
    return *this;
  }

  std::size_t size() const noexcept {
    return _length;
  }

  bool empty() const noexcept {
    return _length == 0;
  }

  char const *c_str() const noexcept {
    return storage_.data();
  }

  bool equals(const char *str) const noexcept {
    if (str == nullptr) {
      return false;
    }
    return std::strcmp(c_str(), str) == 0;
  }

  std::size_t find(char c) const noexcept {
    for (std::size_t i = 0; i < _length; ++i) {
      if (storage_[i] == c) {
        return i;
      }
    }
    return CAPACITY + 1;
  }

  void push_back(char c) noexcept {
    if (_length < CAPACITY) {
      storage_[_length]   = c;
      storage_[++_length] = '\0';
    }
  }

  void append(const FixedString &other) noexcept {
    append(other.c_str());
  }

  void append(char const *str) noexcept {
    if (str == nullptr) {
      return;
    }
    std::size_t i = 0;
    while (str[i] && _length < CAPACITY) {
      storage_[_length++] = str[i++];
    }
    storage_[_length] = '\0';
  }

  char at(std::size_t pos) const noexcept {
    if (pos >= _length) {
      return 0;
    }
    return storage_[pos];
  }

  char operator[](std::size_t pos) const noexcept {
    return at(pos);
  }

  void pop_back() noexcept {
    if (_length > 0) {
      storage_[--_length] = '\0';
    }
  }

  void clear() noexcept {
    storage_[0] = '\0';
    _length     = 0;
  }

  void assignFrom(const FixedString &s) noexcept {
    storage_[0] = '\0';
    _length     = 0;
    append(s);
  }

  bool operator==(const FixedString &other) const noexcept {
    return equals(other.c_str());
  }

  const_iterator begin() const noexcept {
    return storage_.begin();
  }

  const_iterator end() const noexcept {
    return storage_.begin() + _length;
  }

  const_iterator cbegin() const noexcept {
    return storage_.cbegin();
  }

  const_iterator cend() const noexcept {
    return storage_.cbegin() + _length;
  }

private:
  storage_t   storage_{};
  std::size_t _length = 0;
};

#endif
