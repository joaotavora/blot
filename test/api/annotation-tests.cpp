#include <doctest/doctest.h>

#include <boost/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "blot/assembly.hpp"
#include "blot/blot.hpp"
#include "blot/ccj.hpp"
#include "test_config.h"

namespace fs = std::filesystem;
namespace json = boost::json;

// Reusable test function for any fixture (new API: just pass fixture name)
void test_annotation_against_expectation(
    const std::string& fixture_name,
    const xpto::blot::annotation_options& aopts = {}) {
  auto fixture_subdir = fs::path(TEST_FIXTURE_DIR) / fixture_name;

  // Change to fixture subdirectory
  fs::current_path(fixture_subdir);

  // Use standard names within each fixture
  auto ccj_file = fs::path("compile_commands.json");
  auto cpp_file = fs::path("source.cpp");
  auto expectation_file = fs::path("expected.json");

  // Generate assembly from compile commands
  auto cmd = xpto::blot::infer(ccj_file, cpp_file);
  REQUIRE(cmd.has_value());
  auto c_result = xpto::blot::get_asm(*cmd);

  // Run blot annotation with provided options
  auto a_result = xpto::blot::annotate(c_result.assembly, aopts);

  // Load expected results
  std::ifstream file(expectation_file);
  REQUIRE(file.is_open());
  std::string content(
      (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  auto expected = json::parse(content).as_object();

  // Compare assembly output (apply demanglings)
  auto output_lines = xpto::blot::apply_demanglings(a_result);
  auto& expected_assembly = expected["assembly"].as_array();
  REQUIRE(output_lines.size() == expected_assembly.size());
  for (size_t i = 0; i < output_lines.size(); ++i) {
    CHECK(output_lines[i] == expected_assembly[i].as_string());
  }

  // Compare line mappings (new array format)
  auto& expected_mappings = expected["line_mappings"].as_array();
  REQUIRE(a_result.linemap.size() == expected_mappings.size());

  for (size_t i = 0; i < expected_mappings.size(); ++i) {
    auto& expected_mapping = expected_mappings[i].as_object();
    auto& [src_line, asm_start, asm_end] = a_result.linemap[i];

    CHECK(src_line == expected_mapping["source_line"].as_int64());
    CHECK(asm_start == expected_mapping["asm_start"].as_int64());
    CHECK(asm_end == expected_mapping["asm_end"].as_int64());
  }
}

TEST_CASE("api_gcc_basic") { test_annotation_against_expectation("gcc-basic"); }

TEST_CASE("api_gcc_still_pretty_basic") {
  test_annotation_against_expectation("gcc-still-pretty-basic");
}

TEST_CASE("api_gcc_demangle") {
  test_annotation_against_expectation("gcc-demangle", {.demangle = true});
}

TEST_CASE("api_gcc_preserve_directives") {
  test_annotation_against_expectation(
      "gcc-preserve-directives",
      {.preserve_directives = true, .preserve_comments = true});
}

TEST_CASE("api_gcc_preserve_library_functions") {
  test_annotation_against_expectation(
      "gcc-preserve-library-functions", {.preserve_library_functions = true});
}

TEST_CASE("api_gcc_no_preserve_library_functions") {
  test_annotation_against_expectation(
      "gcc-no-preserve-library-functions",
      {.preserve_library_functions = false});
}

TEST_CASE("api_gcc_minimal") {
  test_annotation_against_expectation("gcc-minimal");
}

TEST_CASE("api_clang_preserve_library_functions") {
  test_annotation_against_expectation(
      "clang-preserve-library-functions", {.preserve_library_functions = true});
}

TEST_CASE("api_clang_demangle") {
  test_annotation_against_expectation("clang-demangle", {.demangle = true});
}

TEST_CASE("api_gcc_errors") {
  // This test verifies that compilation errors are properly handled
  auto fixture = fs::path(TEST_FIXTURE_DIR) / "gcc-errors";
  fs::current_path(fixture);

  auto cmd = xpto::blot::infer("compile_commands.json", "source.cpp");
  REQUIRE(cmd.has_value());

  // The get_asm function should throw when compilation fails
  CHECK_THROWS_AS(xpto::blot::get_asm(*cmd), std::runtime_error);
}
