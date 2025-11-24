#ifndef CDC_HOST_H_
#define CDC_HOST_H_

#include <FreeRTOS/Queue.hpp>
#include <FreeRTOS/Task.hpp>
#include <cstdint>
#include <termios.h>

#include "../cdc_base.h"

class TerminalRaw {
public:
  TerminalRaw();
  ~TerminalRaw();

  void restore();
  bool valid;

private:
  termios _orig;
  bool    _restored;
};

class CdcTask : public CdcBase {
public:
  CdcTask();

private:
  ssize_t readBytes(uint8_t *buf, size_t maxlen, uint32_t timeout_ms) override;
  void    flushOutput() override;

  void terminalInit() override;
  void terminalRestore() override;

  TerminalRaw *_term = nullptr;
};

extern CdcTask cdcTask;

#include "../../shell/shell.h"
extern Shell shell;

#endif
