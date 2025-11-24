#include "cdc_host.h"

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

#include "../../shell/log.h"
#include "../../shell/shell.h"

using namespace std;

// TerminalRaw implementation
TerminalRaw::TerminalRaw() : valid(false), _restored(false) {
  if (tcgetattr(STDIN_FILENO, &_orig) == 0) {
    struct termios raw = _orig;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    valid     = true;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
  }
}

void TerminalRaw::restore() {
  if (valid && !_restored) {
    tcsetattr(STDIN_FILENO, TCSANOW, &_orig);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
    _restored = true;
  }
}

TerminalRaw::~TerminalRaw() {
  if (valid && !_restored) {
    tcsetattr(STDIN_FILENO, TCSANOW, &_orig);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
  }
}

CdcTask::CdcTask() : CdcBase(tskIDLE_PRIORITY + 1, "cdc_host"), _term(nullptr) {
}

void CdcTask::terminalInit() {
  if (!_term) {
    _term = new TerminalRaw();
  }
}

void CdcTask::terminalRestore() {
  if (_term) {
    _term->restore();
    delete _term;
    _term = nullptr;
  }
}

ssize_t CdcTask::readBytes(uint8_t *buf, size_t maxlen, uint32_t timeout_ms) {
  struct pollfd pfd;
  pfd.fd     = STDIN_FILENO;
  pfd.events = POLLIN;
  int rc     = poll(&pfd, 1, static_cast<int>(timeout_ms));
  if (rc <= 0)
    return 0;
  if (!(pfd.revents & POLLIN))
    return 0;
  ssize_t r = ::read(STDIN_FILENO, buf, maxlen);
  return r;
}

void CdcTask::flushOutput() {
  cout << flush;
}

CdcTask cdcTask;
