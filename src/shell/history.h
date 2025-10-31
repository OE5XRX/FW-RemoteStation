#ifndef HISTORY_H_
#define HISTORY_H_

#include <array>

#include "constant.h"

class History {
public:
  History();

  LINE_STRING get(int direction);
  void        add(const LINE_STRING &line);
  void        reset();

private:
  std::array<LINE_STRING, CLI_HISTORY_DEPTH> _data;
  size_t                                     _count;
  int32_t                                    _index;
};

#endif
