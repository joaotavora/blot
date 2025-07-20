#include <doctest/doctest.h>

#include <filesystem>

#include "blot/ccj.hpp"
#include "test_config.h"

TEST_CASE("infer-basic") {
  // Test that infer finds the expected includer
  std::filesystem::path fixture_dir{TEST_FIXTURE_DIR};

  auto result = xpto::blot::infer(
      fixture_dir / "compile_commands.json", "fxt-gcc-includes.hpp");

  REQUIRE(result.has_value());
  CHECK(result->filename() == "fxt-gcc-includes.cpp");
}

TEST_CASE("infer-go-into-dir") {
  // Test that infer finds the expected includer
  std::filesystem::path fixture_dir{TEST_FIXTURE_DIR};

  auto result = xpto::blot::infer(
      fixture_dir / "compile_commands.json", "need-an-include-dir.hpp");

  REQUIRE(result.has_value());
  CHECK(result->filename() == "fxt-gcc-includes.cpp");
}
