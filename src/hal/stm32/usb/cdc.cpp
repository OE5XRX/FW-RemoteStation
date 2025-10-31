
#include <cstdint>
#include <tusb_config.h>

#include <tusb.h>

#include "FreeRTOS/Queue.hpp"

#include "../../../shell/log.h"
#include "cdc.h"

FreeRTOS::StaticQueue<LINE_STRING, 5> logQueue;
Shell                                 shell(&logQueue);
Log                                   logger(&logQueue);

void cli_write(const char *str) {
  tud_cdc_write_str(str);
  tud_cdc_write_flush();
}

void cli_write_char(char c) {
  tud_cdc_write_char(c);
  tud_cdc_write_flush();
}

static enum class EscapeState {
  None,
  Escape,
  CSI
} escState = EscapeState::None;

CdcTask::CdcTask() : FreeRTOS::StaticTask<CDC_STACK_SIZE>(configMAX_PRIORITIES - 2, "cdc") {
}

void CdcTask::taskFunction() {
  while (true) {
    if (tud_cdc_connected()) {
      if (tud_cdc_available() > 0) {
        int32_t c = tud_cdc_read_char();
        switch (escState) {
        case EscapeState::None:
          if (c == '\x1b') {
            escState = EscapeState::Escape;
          } else {
            shell.inputChar(c);
          }
          break;
        case EscapeState::Escape:
          if (c == '[') {
            escState = EscapeState::CSI;
          } else {
            escState = EscapeState::None;
          }
          break;
        case EscapeState::CSI:
          if (c == 'A') { // Up arrow
            shell.navigateHistory(1);
          } else if (c == 'B') { // Down arrow
            shell.navigateHistory(-1);
          }
          escState = EscapeState::None;
          break;
        }
      }
      shell.checkLog();
    }
    tud_cdc_write_flush();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

CdcTask cdcTask;
