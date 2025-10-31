#include "history.h"

History::History() : _count(0), _index(-1) {
}

LINE_STRING History::get(int direction) {
  if (_count == 0) {
    return LINE_STRING();
  }

  _index += direction;
  if (_index < 0) {
    _index = 0;
  }
  if (_index >= static_cast<int>(_count)) {
    _index = static_cast<int>(_count) - 1;
  }

  return _data[_index];
}

void History::add(const LINE_STRING &line) {
  for (int32_t i = CLI_HISTORY_DEPTH - 2; i >= 0; i--) {
    _data[i + 1].copy_from(_data[i]);
  }
  _data[0].copy_from(line);
  _count++;
  if (_count > CLI_HISTORY_DEPTH) {
    _count = CLI_HISTORY_DEPTH;
  }
}

void History::reset() {
  _index = -1;
}
