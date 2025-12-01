#ifndef VARIABLE_HELPERS_H_
#define VARIABLE_HELPERS_H_

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

template <typename T> bool parseFromString(T & /*ref*/, const char * /*str*/) {
  // Default: nicht unterstützt
  return false;
}

template <> inline bool parseFromString<int>(int &ref, const char *str) {
  ref = std::atoi(str);
  return true;
}

template <> inline bool parseFromString<float>(float &ref, const char *str) {
  ref = static_cast<float>(std::atof(str));
  return true;
}

template <> inline bool parseFromString<double>(double &ref, const char *str) {
  ref = std::atof(str);
  return true;
}

template <> inline bool parseFromString<bool>(bool &ref, const char *str) {
  ref = (std::strcmp(str, "1") == 0 || std::strcmp(str, "true") == 0);
  return true;
}

template <> inline bool parseFromString<std::uint8_t>(std::uint8_t &ref, const char *str) {
  ref = static_cast<std::uint8_t>(std::atoi(str));
  return true;
}

template <> inline bool parseFromString<std::uint32_t>(std::uint32_t &ref, const char *str) {
  ref = static_cast<std::uint32_t>(std::strtoul(str, nullptr, 0));
  return true;
}
template <typename T> bool stringifyToBuffer(const T & /*ref*/, char *buffer, std::size_t bufferSize) {
  if (bufferSize == 0)
    return false;
  std::snprintf(buffer, bufferSize, "<unsupported>");
  return false;
}

template <> inline bool stringifyToBuffer<int>(const int &ref, char *buffer, std::size_t bufferSize) {
  if (bufferSize == 0)
    return false;
  std::snprintf(buffer, bufferSize, "%d", ref);
  return true;
}

template <> inline bool stringifyToBuffer<float>(const float &ref, char *buffer, std::size_t bufferSize) {
  if (bufferSize == 0)
    return false;
  std::snprintf(buffer, bufferSize, "%.6f", static_cast<double>(ref));
  return true;
}

template <> inline bool stringifyToBuffer<double>(const double &ref, char *buffer, std::size_t bufferSize) {
  if (bufferSize == 0)
    return false;
  std::snprintf(buffer, bufferSize, "%.6f", ref);
  return true;
}

template <> inline bool stringifyToBuffer<bool>(const bool &ref, char *buffer, std::size_t bufferSize) {
  if (bufferSize == 0)
    return false;
  std::snprintf(buffer, bufferSize, "%s", ref ? "true" : "false");
  return true;
}

template <> inline bool stringifyToBuffer<std::uint8_t>(const std::uint8_t &ref, char *buffer, std::size_t bufferSize) {
  if (bufferSize == 0)
    return false;
  std::snprintf(buffer, bufferSize, "%u", static_cast<unsigned int>(ref));
  return true;
}

template <> inline bool stringifyToBuffer<std::uint32_t>(const std::uint32_t &ref, char *buffer, std::size_t bufferSize) {
  if (bufferSize == 0)
    return false;
  std::snprintf(buffer, bufferSize, "%lu", static_cast<unsigned long>(ref));
  return true;
}

#endif
