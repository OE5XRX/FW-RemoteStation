#ifndef TEST_STUB_STREAM_H_
#define TEST_STUB_STREAM_H_

#include "hal/istream.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Test helper: stub stream that records writes and allows injecting received chars.
class StubStream : public IStream {
public:
  StubStream() noexcept : _rxCb(nullptr), _rxCtx(nullptr) {
  }

  void write(const char *s) noexcept override {
    if (!s)
      return;
    const std::size_t l = std::strlen(s);
    for (std::size_t i = 0; i < l; ++i)
      _out.push_back(static_cast<uint8_t>(s[i]));
  }

  void write(const uint8_t *data, std::size_t len) noexcept override {
    if (!data || len == 0)
      return;
    _out.insert(_out.end(), data, data + len);
  }

  void setReceiveCallback(ReceiveCb cb, void *ctx) noexcept override {
    _rxCb  = cb;
    _rxCtx = ctx;
  }

  // Test helper: inject a received char (simulate IRQ/driver)
  void injectChar(char c) noexcept {
    if (_rxCb)
      _rxCb(_rxCtx, c);
  }

  const std::vector<uint8_t> &out() const noexcept {
    return _out;
  }
  void clearOut() noexcept {
    _out.clear();
  }

private:
  ReceiveCb            _rxCb;
  void                *_rxCtx;
  std::vector<uint8_t> _out;
};

#endif
