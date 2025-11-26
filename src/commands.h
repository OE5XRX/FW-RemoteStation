#ifndef COMMANDS_H_
#define COMMANDS_H_

#include "shell/shell.h"

class HelpCommand : public CommandBase {
public:
  HelpCommand();
  void handle(int argc, const char *argv[]) override;
};

class LogCommand : public CommandBase {
public:
  LogCommand();
  void handle(int argc, const char *argv[]) override;
};

class TopCommand : public CommandBase {
public:
  TopCommand();
  void handle(int argc, const char *argv[]) override;
};

class ExitCommand : public CommandBase {
public:
  ExitCommand();
  void handle(int argc, const char *argv[]) override;
};

class SetCommand : public CommandBase {
public:
  SetCommand();
  void handle(int argc, const char *argv[]) override;
};

class GetCommand : public CommandBase {
public:
  GetCommand();
  void handle(int argc, const char *argv[]) override;
};

class ListCommand : public CommandBase {
public:
  ListCommand();
  void handle(int argc, const char *argv[]) override;
};

#endif
