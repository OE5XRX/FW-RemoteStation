#ifndef CONSTANT_H_
#define CONSTANT_H_

#include <cstdint>
#include <stddef.h>

constexpr size_t CLI_CMD_MAX_NAME = 10;
constexpr size_t CLI_CMD_MAX_HELP = 30;

constexpr size_t CLI_MAX_LINE_LENGTH = 100;
constexpr size_t CLI_MAX_ARGS        = 5;
constexpr size_t CLI_HISTORY_DEPTH   = 10;
constexpr size_t CLI_MAX_COMMANDS    = 2;

#include "../fixed_string.h"

typedef FixedString<CLI_MAX_LINE_LENGTH> LINE_STRING;

#endif
