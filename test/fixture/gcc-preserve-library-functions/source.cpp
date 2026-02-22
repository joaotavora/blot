// Test with library functions that generate assembly code
// clang-format off
#include <vector>

auto foo(const std::vector<int>& vec) {
  return vec.size();
}
