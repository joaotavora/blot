// Test with library functions that generate assembly code
#include <vector>

auto foo(const std::vector<int>& vec) {
  return vec.size();
}
