#include "header.hpp"
#include "inner/header.hpp"
#include "inner/hmmm.hpp"

int use_outer() { return outer_fn(); }
int use_inner() { return inner_fn(); }

int hmmm_test(const char* coiso) {
  hmmm test{std::span(coiso, coiso + 1000)};

  auto retval = 0;
  for (auto l : test) {
    retval++;
  }
  return 42 + retval;
}
