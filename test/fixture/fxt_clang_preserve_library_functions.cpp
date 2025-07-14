// Test with library functions that generate assembly code (clang version)
#include <vector>

auto foo(const std::vector<int>& vec) {
  return vec.size();
}