#include "shell.h"
#include "history.h"
#include "log.h"

extern void cli_write(const char *str);
extern void cli_write_char(char c);

CommandBase::CommandBase(const CMD_STRING &_name, const HELP_STRING &_help) : name(_name), help(_help) {
}

Shell::Shell(FreeRTOS::QueueBase<LINE_STRING> *logQueue) : _logQueue(logQueue), _commandCount(0), _shutdownRequested(false) {
}

void Shell::registerCommand(CommandBase *command) {
  if (_commandCount < CLI_MAX_COMMANDS) {
    _commands[_commandCount] = command;
    _commandCount++;
  }
}

void Shell::inputChar(char c) {
  if (c == '\r' || c == '\n') {
    _history.add(_lineBuffer);
    executeLine();
    _lineBuffer.clear();
    _history.reset();
    showPrompt();
  } else if (c == '\b' || c == 127) {
    if (_lineBuffer.size() > 0) {
      _lineBuffer.pop_back();
      redrawLine();
    }
  } else if (c >= 32 && c < 127 && _lineBuffer.size() < (CLI_MAX_LINE_LENGTH - 1)) {
    _lineBuffer.push_back(c);
    cli_write_char(c);
  }
}

void Shell::navigateHistory(int direction) {
  _lineBuffer = _history.get(direction);
  redrawLine();
}

void Shell::executeLine() {
  size_t                                 argc = 0;
  std::array<const char *, CLI_MAX_ARGS> argv;
  parseLine(_lineBuffer.c_str(), argc, argv);
  _lineBuffer.clear();
  _history.reset();

  if (argc > 0) {
    for (size_t i = 0; i < _commandCount; i++) {
      if (_commands[i]->name.equals(argv[0])) {
        _commands[i]->handle(argc, argv);
        return;
      }
    }
    cli_write("Unknown command. Type 'help' for list.\r\n");
  }
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
  cli_write(_lineBuffer.c_str());
}

void Shell::showPrompt() {
  cli_write("> ");
}

void Shell::printHelp() {
  for (size_t i = 0; i < _commandCount; i++) {
    cli_write(_commands[i]->name.c_str());
    cli_write(" - ");
    cli_write(_commands[i]->help.c_str());
    cli_write("\r\n");
  }
}

void Shell::checkLog() {
  std::optional<LINE_STRING> log = _logQueue->receive(0);
  if (log) {
    if (!_lineBuffer.empty()) {
      redrawLine();
    }
    cli_write((*log).c_str());
    cli_write("\r\n");
    if (!_lineBuffer.empty()) {
      showPrompt();
      cli_write(_lineBuffer.c_str());
    }
  }
}

void Shell::exit() {
  logger.info("Shell: exit requested (Ctrl-C)");
  _shutdownRequested.store(true, std::memory_order_release);
}

bool Shell::shutdownRequested() {
  return _shutdownRequested.load(std::memory_order_acquire);
}
