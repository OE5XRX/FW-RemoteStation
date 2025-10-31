#include "log.h"

Log::Log(FreeRTOS::QueueBase<LINE_STRING> *logQueue) : _logQueue(logQueue) {
}

void Log::debug(LINE_STRING text) {
  LINE_STRING t("[DEBUG] ");
  t.append(text);
  _logQueue->sendToBack(t);
}

void Log::info(LINE_STRING text) {
  LINE_STRING t("[INFO] ");
  t.append(text);
  _logQueue->sendToBack(t);
}

void Log::warning(LINE_STRING text) {
  LINE_STRING t("[WARNING] ");
  t.append(text);
  _logQueue->sendToBack(t);
}

void Log::error(LINE_STRING text) {
  LINE_STRING t("[ERROR] ");
  t.append(text);
  _logQueue->sendToBack(t);
}

void Log::assert(bool check, LINE_STRING text) {
  if (!check) {
    LINE_STRING t("[ASSERT] ");
    t.append(text);
    _logQueue->sendToBack(t);
    while (true) {
    }
  }
}
