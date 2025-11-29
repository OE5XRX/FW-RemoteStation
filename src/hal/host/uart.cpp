#include "uart.h"
#include "../../shell/log.h"

HostUart::HostUart(FreeRTOS::QueueBase<LINE_STRING> *inputQueue, FreeRTOS::QueueBase<LINE_STRING> *outputQueue) : iUart(inputQueue, outputQueue) {
  if (!openPty()) {
    logger.info("HostUart: failed to open pty");
  } else {
    logger.info("HostUart slave is:");
    logger.info(_slavePath.c_str());
  }

  // Registriere Instanz für gemeinsame Callbacks falls benötigt
  iUart::registerInstance(this);
  // Die Base-StaticTask sollte den Task starten; taskFunction wird aufgerufen.
}

HostUart::~HostUart() {
  closePty();
  iUart::unregisterInstance(this);
}

bool HostUart::openPty() {
  char slaveNameBuf[128] = {0};

  int master = posix_openpt(O_RDWR | O_NOCTTY);
  if (master < 0)
    return false;
  if (grantpt(master) != 0) {
    close(master);
    return false;
  }
  if (unlockpt(master) != 0) {
    close(master);
    return false;
  }
  if (ptsname_r(master, slaveNameBuf, sizeof(slaveNameBuf)) != 0) {
    close(master);
    return false;
  }

  // configure slave termios minimal (raw)
  int slave = open(slaveNameBuf, O_RDWR | O_NOCTTY);
  if (slave >= 0) {
    struct termios tio;
    if (tcgetattr(slave, &tio) == 0) {
      cfmakeraw(&tio);
      cfsetispeed(&tio, B115200);
      cfsetospeed(&tio, B115200);
      tcsetattr(slave, TCSANOW, &tio);
    }
    close(slave);
  }

  // make master non-blocking
  int flags = fcntl(master, F_GETFL, 0);
  fcntl(master, F_SETFL, flags | O_NONBLOCK);

  _masterFd  = master;
  _slavePath = slaveNameBuf;
  return true;
}

void HostUart::closePty() {
  if (_masterFd >= 0) {
    close(_masterFd);
    _masterFd = -1;
  }
}

ssize_t HostUart::writeAll(const uint8_t *buf, size_t len) {
  if (_masterFd < 0)
    return -1;
  size_t written = 0;
  while (written < len) {
    ssize_t ret = ::write(_masterFd, buf + written, len - written);
    if (ret > 0) {
      written += static_cast<size_t>(ret);
      continue;
    }
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // kurze Wartezeit in Task
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    return -1;
  }
  return static_cast<ssize_t>(written);
}

void HostUart::taskFunction() {
  const int     poll_timeout_ms = 50;
  struct pollfd pfd;
  pfd.fd     = _masterFd;
  pfd.events = POLLIN;

  while (true) {
    // 1) Read from PTY master if available
    if (_masterFd >= 0) {
      pfd.revents = 0;
      int rc      = poll(&pfd, 1, poll_timeout_ms);
      if (rc > 0 && (pfd.revents & POLLIN)) {
        uint8_t buf[128];
        ssize_t r = ::read(_masterFd, buf, sizeof(buf));
        if (r > 0) {
          for (ssize_t i = 0; i < r; ++i) {
            uint8_t b = buf[i];
            if (b == '\n' || b == '\r') {
              if (_rxLine.size() > 0) {
                _inputQueue->sendToBack(_rxLine);
                _rxLine.clear();
              } else {
                LINE_STRING empty;
                _inputQueue->sendToBack(empty);
              }
            } else {
              if (_rxLine.size() < CLI_MAX_LINE_LENGTH) {
                _rxLine.push_back(static_cast<char>(b));
              } else {
                // Zeile voll -> abschicken und neu beginnen
                _inputQueue->sendToBack(_rxLine);
                _rxLine.clear();
                _rxLine.push_back(static_cast<char>(b));
              }
            }
          }
        }
      }
    } else {
      // kein PTY offen -> warten
      vTaskDelay(pdMS_TO_TICKS(poll_timeout_ms));
    }

    // 2) Output Queue -> PTY master
    if (_masterFd >= 0 && _outputQueue->isValid()) {
      auto opt = _outputQueue->receive(0);
      if (opt.has_value()) {
        LINE_STRING line = *opt;
        // Schreibe Zeile, optional Newline anhängen
        if (line.size() > 0) {
          writeAll(reinterpret_cast<const uint8_t *>(line.c_str()), line.size());
        }
        // Newline senden
        const char nl = '\n';
        writeAll(reinterpret_cast<const uint8_t *>(&nl), 1);
      }
    }

    // kurze Pause, um CPU zu schonen
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
