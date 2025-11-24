#ifndef RING_BUFFER_H_
#define RING_BUFFER_H_

#include "FreeRTOS.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <task.h>

struct NoCrit {
  static inline void enter() {
  }
  static inline void exit() {
  }
};

#if !(defined(UNITTEST_BUILD) || defined(HOST_BUILD))
struct RtosCrit {
  static inline void enter() {
    taskENTER_CRITICAL();
  }
  static inline void exit() {
    taskEXIT_CRITICAL();
  }
};

struct IsrCrit {
  static inline void enter() {
    __disable_irq();
  }
  static inline void exit() {
    __enable_irq();
  }
};
#endif

template <typename T, std::size_t N, class Crit = NoCrit> class RingBuffer {
  static_assert(N && ((N & (N - 1)) == 0), "N must be power of two for fast mod");

public:
  bool try_push(const T &v) {
    Crit::enter();
    auto n = (head_ + 1) & mask_;
    if (n == tail_) {
      Crit::exit();
      return false;
    }
    buf_[head_] = v;
    head_       = n;
    Crit::exit();
    return true;
  }

  std::optional<T> try_pop() {
    Crit::enter();
    if (head_ == tail_) {
      Crit::exit();
      return std::nullopt;
    }
    auto v = buf_[tail_];
    tail_  = (tail_ + 1) & mask_;
    Crit::exit();
    return v;
  }

  std::size_t available() const {
    return (head_ - tail_) & mask_;
  }

  std::size_t free_space() const {
    return (mask_)-available();
  }

  bool empty() const {
    return head_ == tail_;
  }

  bool full() const {
    return ((head_ + 1) & mask_) == tail_;
  }

private:
  static constexpr std::size_t mask_ = N - 1;
  std::array<T, N>             buf_{};
  volatile std::size_t         head_ = 0; // Producer schreibt
  volatile std::size_t         tail_ = 0; // Consumer schreibt
};

#endif
