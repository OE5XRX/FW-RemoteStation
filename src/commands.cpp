
#include "commands.h"
#include "shell/log.h"
#include "shell/shell.h"

HelpCommand::HelpCommand() : CommandBase("help", "Show help information") {
}

void HelpCommand::handle(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  shell.printHelp();
}

LogCommand::LogCommand() : CommandBase("log", "Print Log Text") {
}

void LogCommand::handle(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  logger.debug("I am Debugging");
  logger.info("I am an Info");
  logger.warning("oooopppsss");
  logger.error("And I am a error");
}

TopCommand::TopCommand() : CommandBase("top", "Show processes") {
}

void TopCommand::handle(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;

#if (configUSE_STATS_FORMATTING_FUNCTIONS > 0)
  constexpr size_t BUF_SZ = 2048;
  char             buf[BUF_SZ];

  extern void cli_write(const char *str);

  vTaskList(buf);
  cli_write("Name          State  Priority  Stack   Num\r\n");
  cli_write("******************************************\r\n");
  cli_write(buf);
  cli_write("\r\n");
  cli_write("State: 'B' - Blocked, 'R' - Ready, 'D' - Deleted (waiting clean up), 'S' - Suspended, or Blocked without a timeout");

  cli_write("\r\n");
  cli_write("\r\n");

  vTaskGetRunTimeStats(buf);
  cli_write("Task            Abs Time      % Time\r\n");
  cli_write("************************************\r\n");
  cli_write(buf);
#else
  logger.info("Task listing not available: vTaskList not enabled in FreeRTOS config");
#endif
}

ExitCommand::ExitCommand() : CommandBase("exit", "stop execution") {
}

void ExitCommand::handle(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  shell.exit();
}
