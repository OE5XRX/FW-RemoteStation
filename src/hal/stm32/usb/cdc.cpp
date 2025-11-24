#include <cstdint>
#include <tusb_config.h>

#include <tusb.h>

#include "FreeRTOS/Queue.hpp"

#include "../../../shell/log.h"
#include "cdc.h"

void cli_write(const char *str) {
  tud_cdc_write_str(str);
  tud_cdc_write_flush();
}

void cli_write_char(char c) {
  tud_cdc_write_char(c);
  tud_cdc_write_flush();
}

CdcTask::CdcTask() : CdcBase(configMAX_PRIORITIES - 2, "cdc") {
}

ssize_t CdcTask::readBytes(uint8_t *buf, size_t maxlen, uint32_t /*timeout_ms*/) {
  if (!tud_cdc_connected())
    return 0;
  int32_t avail = tud_cdc_available();
  if (avail <= 0)
    return 0;
  size_t toread = static_cast<size_t>(avail);
  if (toread > maxlen)
    toread = maxlen;
  for (size_t i = 0; i < toread; ++i) {
    int32_t c = tud_cdc_read_char();
    if (c < 0)
      return static_cast<ssize_t>(i);
    buf[i] = static_cast<uint8_t>(c);
  }
  return static_cast<ssize_t>(toread);
}

void CdcTask::flushOutput() {
  tud_cdc_write_flush();
}

CdcTask cdcTask;
