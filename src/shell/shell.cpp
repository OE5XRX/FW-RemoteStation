#include "shell.h"

#include "history.h"

extern void cli_write(const char *str);
extern void cli_write_char(char c);

CommandBase::CommandBase(const CMD_STRING &_name, const HELP_STRING &_help) : name(_name), help(_help) {
}

Shell::Shell(FreeRTOS::QueueBase<LINE_STRING> *logQueue) : _logQueue(logQueue), _commandCount(0) {
}

void Shell::registerCommand(CommandBase *command) {
  if (_commandCount < CLI_MAX_COMMANDS) {
    _commands[_commandCount] = command;
    _commandCount++;
  }
}

void Shell::inputChar(char c) {
  if (c == '\r') {
    cli_write("\r\n");
    _history.add(_lineBuffer.data());
    executeLine();
    _lineBuffer.clear();
    _history.reset();
    showPrompt();
  } else if (c == '\b' || c == 127) {
    if (_lineBuffer.length() > 0) {
      _lineBuffer.pop();
      cli_write("\b \b");
    }
  } else if (c >= 32 && c < 127 && _lineBuffer.length() < (CLI_MAX_LINE_LENGTH - 1)) {
    _lineBuffer.append(c);
    cli_write_char(c);
  } else if (c == '\n') {
  } else {
    cli_write("something is wrong...");
  }
}

void Shell::navigateHistory(int direction) {
  _lineBuffer.copy_from(_history.get(direction));
  redrawLine();
}

void Shell::executeLine() {
  size_t                                 argc = 0;
  std::array<const char *, CLI_MAX_ARGS> argv;
  parseLine(_lineBuffer.data(), argc, argv);

  if (argc == 0) {
    return;
  }

  for (size_t i = 0; i < _commandCount; i++) {
    if (argv[0] && _commands[i]->name.compare(argv[0])) {
      _commands[i]->handle(argc, argv.data());
      return;
    }
  }

  cli_write("Unknown command. Type 'help' for list.\r\n");
}

void Shell::parseLine(const char *line, size_t &argc, std::array<const char *, CLI_MAX_ARGS> &argv) {
  static char buffer[CLI_MAX_LINE_LENGTH];
  std::strncpy(buffer, line, CLI_MAX_LINE_LENGTH);
  buffer[CLI_MAX_LINE_LENGTH - 1] = '\0';

  argc        = 0;
  char *token = std::strtok(buffer, " ");
  while (token && argc < CLI_MAX_ARGS) {
    argv[argc++] = token;
    token        = std::strtok(nullptr, " ");
  }
}

void Shell::redrawLine() {
  cli_write("\r");
  showPrompt();
  cli_write(_lineBuffer.data());
}

void Shell::showPrompt() {
  cli_write("> ");
}

void Shell::printHelp() {
  cli_write("Available commands:\r\n");
  for (size_t i = 0; i < _commandCount; i++) {
    cli_write("  ");
    cli_write(_commands[i]->name.data());
    cli_write(": ");
    cli_write(_commands[i]->help.data());
    cli_write("\r\n");
  }
}

void Shell::checkLog() {
  if (std::optional<LINE_STRING> log = _logQueue->receive(pdMS_TO_TICKS(10))) {
    if (_lineBuffer.length() == 0) {
      cli_write("\r");
    } else {
      cli_write("\n\r");
    }
    cli_write((*log).data());
    cli_write("\n\r");
    redrawLine();
  }
}
