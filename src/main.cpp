#include <FreeRTOS/Kernel.hpp>

#include "board.h"

#if !(defined(UNITTEST_BUILD) || defined(HOST_BUILD))
#include "hal/stm32/usb/cdc.h"
#endif

#include "commands.h"
#include "shell/log.h"

// neue Includes für Task-Liste
#include "FreeRTOS.h"
#include "task.h"

#include "VariableRegistry.h"
#include "config.h"
#include "hal/system_manager.h"

FreeRTOS::StaticQueue<LINE_STRING, 5> logQueue;
Shell                                 shell(&logQueue);
Log                                   logger(&logQueue);

#ifndef UNITTEST_BUILD
int main(void) {
  board_init();

  SystemManager::init();
  Config::init();

  HelpCommand helpCmd;
  shell.registerCommand(&helpCmd);
  LogCommand logCmd;
  shell.registerCommand(&logCmd);
  TopCommand topCmd;
  shell.registerCommand(&topCmd);
  ExitCommand exitCmd;
  shell.registerCommand(&exitCmd);

  SetCommand setCmd;
  shell.registerCommand(&setCmd);
  GetCommand getCmd;
  shell.registerCommand(&getCmd);
  ListCommand listCmd;
  shell.registerCommand(&listCmd);

  CommandConfigLoad cfgLoadCmd;
  shell.registerCommand(&cfgLoadCmd);
  CommandConfigSave cfgSaveCmd;
  shell.registerCommand(&cfgSaveCmd);
  CommandConfigReset cfgResetCmd;
  shell.registerCommand(&cfgResetCmd);
  CommandConfigDump cfgDumpCmd;
  shell.registerCommand(&cfgDumpCmd);

  FreeRTOS::Kernel::startScheduler();
  return 0;
}
#endif
