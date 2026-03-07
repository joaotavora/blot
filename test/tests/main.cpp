#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "logger.hpp"

int main(int argc, char** argv) {
  xpto::logger::set_level(xpto::logger::level::trace);
  return doctest::Context(argc, argv).run();
}
