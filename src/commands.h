#ifndef COMMANDS_H_
#define COMMANDS_H_

#include "shell/shell.h"

class HelpCommand : public CommandBase {
public:
  HelpCommand();
  void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) override;
};

class LogCommand : public CommandBase {
public:
  LogCommand();
  void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) override;
};

class TopCommand : public CommandBase {
public:
  TopCommand();
  void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) override;
};

class ExitCommand : public CommandBase {
public:
  ExitCommand();
  void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) override;
};

class SetCommand : public CommandBase {
public:
  SetCommand();
  void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) override;
};

class GetCommand : public CommandBase {
public:
  GetCommand();
  void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) override;
};

class ListCommand : public CommandBase {
public:
  ListCommand();
  void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) override;
};

class CommandConfigLoad : public CommandBase {
public:
  CommandConfigLoad();
  void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) override;
};

class CommandConfigSave : public CommandBase {
public:
  CommandConfigSave();
  void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) override;
};

class CommandConfigReset : public CommandBase {
public:
  CommandConfigReset();
  void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) override;
};

class CommandConfigDump : public CommandBase {
public:
  CommandConfigDump();
  void handle(int argc, std::array<const char *, CLI_MAX_ARGS> argv) override;
};

#endif
