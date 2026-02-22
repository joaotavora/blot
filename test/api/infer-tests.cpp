#include <doctest/doctest.h>

#include <filesystem>

#include "blot/ccj.hpp"
#include "test_config.h"

using std::filesystem::path;

TEST_CASE("infer-basic") {
  // Test that infer finds the expected includer
  path gcc_includes = path{TEST_FIXTURE_DIR} / "gcc-includes";

  auto result =
      xpto::blot::infer(gcc_includes / "compile_commands.json", "header.hpp");

  REQUIRE(result.has_value());
  CHECK(result->file.filename() == "source.cpp");
}

TEST_CASE("infer-go-into-dir") {
  // Test that infer finds the expected includer including from subdirectory
  path gcc_includes = path{TEST_FIXTURE_DIR} / "gcc-includes";

  auto result = xpto::blot::infer(
      gcc_includes / "compile_commands.json", "need-an-include-dir.hpp");

  REQUIRE(result.has_value());
  CHECK(result->file.filename() == "source.cpp");
}
