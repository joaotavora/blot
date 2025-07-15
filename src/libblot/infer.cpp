#include "blot/infer.hpp"

#include <clang-c/Index.h>

namespace xpto::blot {

void infer() {
  // Dummy libclang call to test integration
  CXIndex index = clang_createIndex(0, 0);
  clang_disposeIndex(index);
}

}  // namespace xpto::blot
