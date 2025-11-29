
#include "commands.h"
#include "VariableRegistry.h"
#include "shell/log.h"
#include "shell/shell.h"

extern void cli_write(const char *str);

HelpCommand::HelpCommand() : CommandBase("help", "Show help information") {
}

void HelpCommand::handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) {
  (void)argc;
  (void)argv;
  shell.printHelp();
}

LogCommand::LogCommand() : CommandBase("log", "Print Log Text") {
}

void LogCommand::handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) {
  (void)argc;
  (void)argv;
  logger.debug("I am Debugging");
  logger.info("I am an Info");
  logger.warning("oooopppsss");
  logger.error("And I am a error");
}

TopCommand::TopCommand() : CommandBase("top", "Show processes") {
}

void TopCommand::handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) {
  (void)argc;
  (void)argv;

#if (configUSE_STATS_FORMATTING_FUNCTIONS > 0)
  constexpr size_t BUF_SZ = 2048;
  char             buf[BUF_SZ];

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

void ExitCommand::handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) {
  (void)argc;
  (void)argv;
  shell.exit();
}

SetCommand::SetCommand() : CommandBase("set", "set <name> <value>  - set a registered variable") {
}

void SetCommand::handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) {
  if (argc < 3) {
    cli_write("Usage: set <name> <value>\r\n");
    return;
  }

  const char *varName  = argv[1];
  const char *varValue = argv[2];

  if (!variableRegistry.set(varName, varValue)) {
    cli_write("Error: unknown variable or invalid value: ");
    cli_write(varName);
    cli_write("\r\n");
    return;
  }

  cli_write("OK\r\n");
}

GetCommand::GetCommand() : CommandBase("get", "get <name>  - read a registered variable") {
}

void GetCommand::handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) {
  if (argc < 2) {
    cli_write("Usage: get <name>\r\n");
    return;
  }

  const char *varName = argv[1];
  char        buffer[32];

  if (!variableRegistry.get(varName, buffer, sizeof(buffer))) {
    cli_write("Error: unknown variable: ");
    cli_write(varName);
    cli_write("\r\n");
    return;
  }

  cli_write(varName);
  cli_write(" = ");
  cli_write(buffer);
  cli_write("\r\n");
}

ListCommand::ListCommand() : CommandBase("list", "list  - list all registered variables") {
}

void ListCommand::handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) {
  (void)argc;
  (void)argv;

  size_t      index   = 0;
  const char *varName = variableRegistry.getName(index);
  while (varName != nullptr) {
    char buffer[32];
    if (variableRegistry.get(varName, buffer, sizeof(buffer))) {
      cli_write(varName);
      cli_write(" = ");
      cli_write(buffer);
      cli_write("\r\n");
    }
    index++;
    varName = variableRegistry.getName(index);
  }
}
