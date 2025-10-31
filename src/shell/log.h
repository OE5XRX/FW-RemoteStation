#ifndef LOG_H_
#define LOG_H_

#include <FreeRTOS/Queue.hpp>

#include "constant.h"

class Log {
public:
  Log(FreeRTOS::QueueBase<LINE_STRING> *logQueue);

  void debug(LINE_STRING text);
  void info(LINE_STRING text);
  void warning(LINE_STRING text);
  void error(LINE_STRING text);

  void assert(bool check, LINE_STRING text);

private:
  FreeRTOS::QueueBase<LINE_STRING> *_logQueue;
};

extern Log logger;

#endif
