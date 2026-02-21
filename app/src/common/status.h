/**
 * @file status.h
 * @brief Unified status codes for the application
 *
 * Provides consistent error handling across all components using
 * standard errno values where applicable.
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#pragma once

#include <errno.h>
#include <type_traits>

namespace oe5xrx {

/**
 * @brief Unified status codes for application-wide error handling
 *
 * Based on standard errno values for consistency with Zephyr APIs.
 * Uses C++20 enum class with explicit underlying type.
 */
enum class [[nodiscard]] Status : int {
  OK = 0,                   ///< Operation successful
  INVALID_PARAM = -EINVAL,  ///< Invalid parameter (-22)
  TIMEOUT = -ETIMEDOUT,     ///< Operation timed out (-110)
  NO_MEM = -ENOMEM,         ///< Out of memory (-12)
  IO_ERROR = -EIO,          ///< I/O error (-5)
  NOT_SUPPORTED = -ENOTSUP, ///< Operation not supported (-95)
  NO_DEVICE = -ENODEV,      ///< No such device (-19)
  BUSY = -EBUSY,            ///< Resource busy (-16)
  AGAIN = -EAGAIN,          ///< Try again (-11)
  PROTOCOL_ERROR = -EPROTO, ///< Protocol error (-71)
  BAD_MESSAGE = -EBADMSG,   ///< Bad message (-74)
  NOT_READY = -ENODATA,     ///< No data available / not ready (-61)
  OVERFLOW = -EOVERFLOW,    ///< Value too large (-75)
};

/**
 * @brief Convert Status to int (errno value)
 *
 * C++20 feature: using-enum for cleaner syntax
 */
[[nodiscard]] constexpr int to_errno(Status status) noexcept {
  return static_cast<int>(status);
}

/**
 * @brief Convert int (errno value) to Status
 */
[[nodiscard]] constexpr Status from_errno(int err) noexcept {
  return static_cast<Status>(err);
}

/**
 * @brief Check if status indicates success
 */
[[nodiscard]] constexpr bool is_ok(Status status) noexcept {
  return status == Status::OK;
}

/**
 * @brief Check if status indicates error
 */
[[nodiscard]] constexpr bool is_error(Status status) noexcept {
  return status != Status::OK;
}

} // namespace oe5xrx
