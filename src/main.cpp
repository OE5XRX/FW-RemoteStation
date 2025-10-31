#include <FreeRTOS/Kernel.hpp>

#ifndef UNITTEST
#include "hal/stm32/board.h"
#include "hal/stm32/usb/cdc.h"
#endif

#include "shell/log.h"
#include "shell/shell.h"

class HelpCommand : public CommandBase {
public:
  HelpCommand() : CommandBase("help", "Show help information") {
  }

  void handle(int argc, const char *argv[]) override {
    (void)argc;
    (void)argv;
    // shell.printHelp();
  }
};

class LogCommand : public CommandBase {
public:
  LogCommand() : CommandBase("log", "Print Log Text") {
  }

  void handle(int argc, const char *argv[]) override {
    (void)argc;
    (void)argv;
    logger.debug("I am Debugging");
    logger.info("I am an Info");
    logger.warning("oooopppsss");
    logger.error("And I am a error");
  }
};

#ifndef UNITTEST
int main(void) {
  board_init();

  static HelpCommand helpCmd;
  shell.registerCommand(&helpCmd);
  static LogCommand logCmd;
  shell.registerCommand(&logCmd);

  FreeRTOS::Kernel::startScheduler();

  return 0;
}
#endif
