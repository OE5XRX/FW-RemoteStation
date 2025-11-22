#ifndef HOST_UART_H_
#define HOST_UART_H_

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "../interface_uart.h"

class HostUart : public iUart {
public:
  HostUart(FreeRTOS::QueueBase<LINE_STRING> *inputQueue, FreeRTOS::QueueBase<LINE_STRING> *outputQueue);
  virtual ~HostUart();

private:
  // iUart::taskFunction wird als FreeRTOS-Taskbody genutzt
  void taskFunction() final;

  bool    openPty();
  void    closePty();
  ssize_t writeAll(const uint8_t *buf, size_t len);

  int         _masterFd = -1;
  std::string _slavePath;

  // Zeilenpuffer für Empfang
  LINE_STRING _rxLine;

  // Nicht kopierbar
  HostUart(const HostUart &)            = delete;
  HostUart &operator=(const HostUart &) = delete;
};

#endif
