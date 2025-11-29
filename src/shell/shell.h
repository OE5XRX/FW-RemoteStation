#ifndef SHELL_H_
#define SHELL_H_

#include <FreeRTOS/Queue.hpp>
#include <array>
#include <atomic>

#include "constant.h"
#include "history.h"

class CommandBase {
public:
  typedef FixedString<CLI_CMD_MAX_NAME> CMD_STRING;
  typedef FixedString<CLI_CMD_MAX_HELP> HELP_STRING;

  CommandBase(const CMD_STRING &name, const HELP_STRING &help);

  virtual void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) = 0;

  const CMD_STRING  name;
  const HELP_STRING help;
};

class Shell {
public:
  Shell(FreeRTOS::QueueBase<LINE_STRING> *logQueue);

  void registerCommand(CommandBase *command);
  void inputChar(char c);
  void navigateHistory(int direction);
  void printHelp();

  void checkLog();
  void exit();              // löst Shutdown-Request aus (Ctrl-C)
  bool shutdownRequested(); // Abfrage ob Interrupt gesetzt wurde

private:
  LINE_STRING _lineBuffer;
  History     _history;

  FreeRTOS::QueueBase<LINE_STRING> *_logQueue;

  std::array<CommandBase *, CLI_MAX_COMMANDS> _commands;
  size_t                                      _commandCount;

  std::atomic<bool> _shutdownRequested;

  void executeLine();
  void parseLine(const char *line, size_t &argc, std::array<const char *, CLI_MAX_ARGS> &argv);
  void redrawLine();
  void showPrompt();
};

extern Shell shell;

#endif
