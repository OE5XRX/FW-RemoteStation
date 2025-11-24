#include "cdc_base.h"
#include "../shell/shell.h"

#include <cstdlib>

CdcBase::CdcBase(UBaseType_t priority, const char *name) : StaticTask<CDC_STACK_SIZE>(priority, name), _escState(EscapeState::None) {
}

void CdcBase::terminalInit() {
}

void CdcBase::terminalRestore() {
}

void CdcBase::taskFunction() {
  terminalInit();

  constexpr size_t BUF_SZ = 128;
  uint8_t          buf[BUF_SZ];

  const uint32_t loop_delay_ms = 10;

  while (true) {
    if (shell.shutdownRequested()) {
      terminalRestore();

      logger.info("CdcTask: shutdown requested, ending scheduler.");
      FreeRTOS::Kernel::endScheduler();
      std::exit(0);
    }

    ssize_t r = readBytes(buf, BUF_SZ, loop_delay_ms);
    if (r > 0) {
      for (ssize_t i = 0; i < r; ++i) {
        unsigned char c = buf[i];

        if (c == 0x03) { // Ctrl-C
          shell.exit();
          continue;
        }

        switch (_escState) {
        case EscapeState::None:
          if (c == 0x1B) {
            _escState = EscapeState::Escape;
          } else {
            shell.inputChar(c);
          }
          break;
        case EscapeState::Escape:
          if (c == '[') {
            _escState = EscapeState::CSI;
          } else {
            _escState = EscapeState::None;
          }
          break;
        case EscapeState::CSI:
          if (c == 'A') { // Up arrow
            shell.navigateHistory(1);
          } else if (c == 'B') { // Down arrow
            shell.navigateHistory(-1);
          }
          _escState = EscapeState::None;
          break;
        }
      }
    }

    shell.checkLog();
    flushOutput();

    vTaskDelay(pdMS_TO_TICKS(loop_delay_ms));
  }

  terminalRestore();
}
