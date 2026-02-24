#pragma once

#include <string>

inline std::string thingy() {
  return "yoyo!";
}

inline int inner_fn() { [[maybe_unused]] std::string foo = thingy(); return 2;}
