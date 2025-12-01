#include "app_config.h"
#include "VariableRegistry.h"
#include "config.h"

static Variable<uint8_t>  g_varVolume("volume", Config::current().volume);
static Variable<float>    g_varFreq("frequency", Config::current().frequency);
static Variable<bool>     g_varTxEnable("tx_enable", Config::current().txEnable);
static Variable<uint32_t> g_varDummy("dummy", Config::current().dummy);
