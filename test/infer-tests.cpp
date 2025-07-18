#include <doctest/doctest.h>

#include <filesystem>

#include "blot/infer.hpp"
#include "test_config.h"

TEST_CASE("infer_basic") {
  // Test that infer finds the expected includer
  std::filesystem::path fixture_dir{TEST_FIXTURE_DIR};

  auto result = xpto::blot::infer(
      fixture_dir / "compile_commands.json", "fxt-gcc-includes.hpp");

  REQUIRE(result.has_value());
  CHECK(result->filename() == "fxt-gcc-includes.cpp");
}
