#include <stdio.h>
#include <string.h>

#include "VariableRegistry.h"
#include "commands.h"
#include "config.h"
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

  if (!VariableRegistry::instance().set(varName, varValue)) {
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

  if (!VariableRegistry::instance().get(varName, buffer, sizeof(buffer))) {
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
  const char *varName = VariableRegistry::instance().getName(index);
  while (varName != nullptr) {
    char buffer[32];
    if (VariableRegistry::instance().get(varName, buffer, sizeof(buffer))) {
      cli_write(varName);
      cli_write(" = ");
      cli_write(buffer);
      cli_write("\r\n");
    }
    index++;
    varName = VariableRegistry::instance().getName(index);
  }
}

CommandConfigLoad::CommandConfigLoad() : CommandBase(CMD_STRING("config_load"), HELP_STRING("Lädt Config aus persistentem Speicher.")) {
}

void CommandConfigLoad::handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) {
  (void)argc;
  (void)argv;

  if (!Config::load()) {
    cli_write("Config load FAILED – Defaults bleiben aktiv.\r\n");
    return;
  }
  cli_write("Config loaded. Sequence: ");
  char buffer[10];
  std::snprintf(buffer, sizeof(buffer), "%ld", Config::currentSequence());
  cli_write(buffer);
  cli_write("\r\n");
}

CommandConfigSave::CommandConfigSave() : CommandBase(CMD_STRING("config_save"), HELP_STRING("Speichert aktuelle Config persistent.")) {
}

void CommandConfigSave::handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) {
  (void)argc;
  (void)argv;

  if (!Config::save()) {
    cli_write("Config save FAILED!\r\n");
    return;
  }
  cli_write("Config saved.\r\n");
}

CommandConfigReset::CommandConfigReset() : CommandBase(CMD_STRING("config_reset"), HELP_STRING("Setzt Config auf Defaults [optional: save].")) {
}

void CommandConfigReset::handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) {
  Config::resetToDefaults();
  cli_write("Config reset -> defaults applied.\r\n");

  if (argc > 1 && std::strcmp(argv[1], "save") == 0) {
    if (Config::save()) {
      cli_write("Config saved.\r\n");
    } else {
      cli_write("Config save FAILED!\r\n");
    }
  }
}

CommandConfigDump::CommandConfigDump() : CommandBase(CMD_STRING("config_dump"), HELP_STRING("Zeigt aktuelle Config und CRC32 an.")) {
}

void CommandConfigDump::handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) {
  (void)argc;
  (void)argv;

  VariableRegistry &reg = VariableRegistry::instance();

  cli_write("Config variables:\r\n");

  const std::size_t n = reg.size();
  char              valueBuf[32];
  char              lineBuf[64];

  for (std::size_t i = 0; i < n; ++i) {
    VariableBase *var = reg.getVar(i);
    if (!var) {
      continue;
    }

    valueBuf[0] = '\0';
    var->getAsString(valueBuf, sizeof(valueBuf));

    std::snprintf(lineBuf, sizeof(lineBuf), "  %s = %s\r\n", var->name(), valueBuf);
    cli_write(lineBuf);
  }

  char line[64];
  std::snprintf(line, sizeof(line), "Sequence = %ld\r\n", Config::currentSequence());
  cli_write(line);
}
