#include <doctest/doctest.h>

#include <filesystem>

#include "blot/ccj.hpp"
#include "test_config.h"

namespace fs = std::filesystem;

TEST_CASE("infer-basic") {
  // Test that infer finds the expected includer
  fs::path gcc_includes = fs::path{TEST_FIXTURE_DIR} / "gcc-includes";

  auto result = xpto::blot::infer(
      gcc_includes / "compile_commands.json", gcc_includes / "header.hpp");

  REQUIRE(result.has_value());
  CHECK(result->file.filename() == "source.cpp");
}

TEST_CASE("infer-go-into-dir") {
  // Test that infer finds the expected includer including from subdirectory
  fs::path gcc_includes = fs::path{TEST_FIXTURE_DIR} / "gcc-includes";

  auto result = xpto::blot::infer(
      gcc_includes / "compile_commands.json",
      gcc_includes / "just-an-include-dir" / "need-an-include-dir.hpp");

  REQUIRE(result.has_value());
  CHECK(result->file.filename() == "source.cpp");
}

// The gcc-deep-hierarchy fixture has two independent translation units:
//   source-1.cpp  includes  header.hpp        (the outer/top-level header)
//   source-2.cpp  includes  inner/header.hpp  (a different header, deeper)
// Both included files share the basename "header.hpp".  source-1.cpp appears
// first in compile_commands.json.  These tests establish which translation
// unit infer() returns in each case.

TEST_CASE("infer-deep-outer-by-abspath") {
  // Searching by the absolute path of the outer header.hpp performs an exact
  // match and correctly identifies source-1.cpp.
  fs::path fixture = fs::path{TEST_FIXTURE_DIR} / "gcc-deep-hierarchy";

  auto result = xpto::blot::infer(
      fixture / "compile_commands.json", fixture / "header.hpp");

  REQUIRE(result.has_value());
  CHECK(result->file.filename() == "source-1.cpp");
}

TEST_CASE("infer-deep-inner-by-abspath") {
  // Searching by the absolute path of inner/header.hpp correctly identifies
  // source-2.cpp — the only translation unit that actually includes it.
  fs::path fixture = fs::path{TEST_FIXTURE_DIR} / "gcc-deep-hierarchy";

  auto result = xpto::blot::infer(
      fixture / "compile_commands.json", fixture / "inner" / "header.hpp");

  REQUIRE(result.has_value());
  CHECK(result->file.filename() == "source-2.cpp");
}

TEST_CASE("infer-deep-outer-by-relative-filename") {
  // A bare relative filename is resolved against the directory of the
  // compile_commands.json file.  "header.hpp" therefore becomes
  // fixture/header.hpp, which is the outer header included by source-1.cpp.
  fs::path fixture = fs::path{TEST_FIXTURE_DIR} / "gcc-deep-hierarchy";
  fs::current_path(fixture);

  auto result = xpto::blot::infer("compile_commands.json", "header.hpp");

  REQUIRE(result.has_value());
  CHECK(result->file.filename() == "source-1.cpp");
}

TEST_CASE("infer-deep-inner-by-relative-path") {
  // A relative path is resolved against the directory of the
  // compile_commands.json file.  "inner/header.hpp" therefore becomes
  // fixture/inner/header.hpp, which is included only by source-2.cpp.
  fs::path fixture = fs::path{TEST_FIXTURE_DIR} / "gcc-deep-hierarchy";
  fs::current_path(fixture);

  auto result = xpto::blot::infer("compile_commands.json", "inner/header.hpp");

  REQUIRE(result.has_value());
  CHECK(result->file.filename() == "source-2.cpp");
}
