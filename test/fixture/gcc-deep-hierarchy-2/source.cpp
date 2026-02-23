#include "header.hpp"
#include "inner/header.hpp"

int use_outer() { return outer_fn(); }
int use_inner() { return inner_fn(); }
