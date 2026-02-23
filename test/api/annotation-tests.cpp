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

TEST_CASE("api_gcc_includes_source") {
  // Annotating the TU directly (no target_file): main should appear, thingy
  // should not (it lives in the included header).
  auto fixture = fs::path(TEST_FIXTURE_DIR) / "gcc-includes";
  fs::current_path(fixture);

  auto cmd = xpto::blot::infer("compile_commands.json", "source.cpp");
  REQUIRE(cmd.has_value());
  auto c_result = xpto::blot::get_asm(*cmd);

  auto a_result = xpto::blot::annotate(c_result.assembly, {});
  auto lines = xpto::blot::apply_demanglings(a_result);

  bool found_main{false}, found_thingy_label{false};
  for (auto& l : lines) {
    if (l == "main:") found_main = true;
    // thingy may appear in a call instruction inside main's body, but its
    // function header (a label ending with ':') should not be present.
    if (l.ends_with(':') && l.find("thingy") != std::string::npos)
      found_thingy_label = true;
  }
  CHECK(found_main);
  CHECK(!found_thingy_label);
}

TEST_CASE("api_gcc_includes_header") {
  // Annotating a header file via target_file: thingy (defined in header.hpp)
  // should appear; main (defined in the including TU) should not.
  auto fixture = fs::path(TEST_FIXTURE_DIR) / "gcc-includes";
  fs::current_path(fixture);

  auto cmd = xpto::blot::infer("compile_commands.json", "header.hpp");
  REQUIRE(cmd.has_value());
  auto c_result = xpto::blot::get_asm(*cmd);

  auto a_result =
      xpto::blot::annotate(c_result.assembly, {}, fs::path{"header.hpp"});
  auto lines = xpto::blot::apply_demanglings(a_result);

  bool found_main{false}, found_thingy_label{false};
  for (auto& l : lines) {
    if (l == "main:") found_main = true;
    if (l.ends_with(':') && l.find("thingy") != std::string::npos)
      found_thingy_label = true;
  }
  CHECK(found_thingy_label);
  CHECK(!found_main);

  // Line mappings should reference lines in header.hpp (thingy body is at
  // lines 4-6 of that file).
  REQUIRE(!a_result.linemap.empty());
  for (auto& [src_line, asm_start, asm_end] : a_result.linemap) {
    CHECK(src_line >= 4);
    CHECK(src_line <= 6);
  }
}

// Returns true if line looks like a function label that contains needle.
// Handles GCC ("_Zfoo:") and Clang ("_Zfoo:   # @_Zfoo") styles.
static bool is_label_with(const std::string& line, std::string_view needle) {
  // Label lines are non-indented and contain ':' somewhere.
  return !line.starts_with('\t') && line.find(':') != std::string::npos &&
         line.find(needle) != std::string::npos;
}

// Two headers in different directories both named header.hpp, both included
// by a single source.cpp.  annotate() must use the full path to distinguish
// them, not just the basename.
TEST_CASE("api_gcc_deep_hierarchy_2_outer") {
  fs::path fixture = fs::path{TEST_FIXTURE_DIR} / "gcc-deep-hierarchy-2";
  fs::current_path(fixture);

  auto cmd = xpto::blot::infer("compile_commands.json", fixture / "header.hpp");
  REQUIRE(cmd.has_value());
  auto c_result = xpto::blot::get_asm(*cmd);

  // Ask for the outer header.hpp by absolute path.
  auto a_result =
      xpto::blot::annotate(c_result.assembly, {}, fixture / "header.hpp");
  auto lines = xpto::blot::apply_demanglings(a_result);

  bool found_outer{false}, found_inner{false};
  for (auto& l : lines) {
    if (is_label_with(l, "outer")) found_outer = true;
    if (is_label_with(l, "inner")) found_inner = true;
  }
  CHECK(found_outer);
  CHECK(!found_inner);
}

TEST_CASE("api_gcc_deep_hierarchy_2_inner") {
  fs::path fixture = fs::path{TEST_FIXTURE_DIR} / "gcc-deep-hierarchy-2";
  fs::current_path(fixture);

  auto cmd = xpto::blot::infer(
      "compile_commands.json", fixture / "inner" / "header.hpp");
  REQUIRE(cmd.has_value());
  auto c_result = xpto::blot::get_asm(*cmd);

  // Ask for inner/header.hpp by absolute path.
  auto a_result = xpto::blot::annotate(
      c_result.assembly, {}, fixture / "inner" / "header.hpp");
  auto lines = xpto::blot::apply_demanglings(a_result);

  bool found_outer{false}, found_inner{false};
  for (auto& l : lines) {
    if (is_label_with(l, "outer")) found_outer = true;
    if (is_label_with(l, "inner")) found_inner = true;
  }
  CHECK(found_inner);
  CHECK(!found_outer);
}

// Same as api_gcc_deep_hierarchy_2_* but compiled with clang++.
// Clang emits an explicit directory on every .file entry (e.g. "." and
// "./inner"), whereas GCC leaves the directory empty for non-primary files.
TEST_CASE("api_clang_deep_hierarchy_2_outer") {
  fs::path fixture = fs::path{TEST_FIXTURE_DIR} / "clang-deep-hierarchy-2";
  fs::current_path(fixture);

  auto cmd = xpto::blot::infer("compile_commands.json", fixture / "header.hpp");
  REQUIRE(cmd.has_value());
  auto c_result = xpto::blot::get_asm(*cmd);

  auto a_result =
      xpto::blot::annotate(c_result.assembly, {}, fixture / "header.hpp");
  auto lines = xpto::blot::apply_demanglings(a_result);

  bool found_outer{false}, found_inner{false};
  for (auto& l : lines) {
    if (is_label_with(l, "outer")) found_outer = true;
    if (is_label_with(l, "inner")) found_inner = true;
  }
  CHECK(found_outer);
  CHECK(!found_inner);
}

TEST_CASE("api_clang_deep_hierarchy_2_inner") {
  fs::path fixture = fs::path{TEST_FIXTURE_DIR} / "clang-deep-hierarchy-2";
  fs::current_path(fixture);

  auto cmd = xpto::blot::infer(
      "compile_commands.json", fixture / "inner" / "header.hpp");
  REQUIRE(cmd.has_value());
  auto c_result = xpto::blot::get_asm(*cmd);

  auto a_result = xpto::blot::annotate(
      c_result.assembly, {}, fixture / "inner" / "header.hpp");
  auto lines = xpto::blot::apply_demanglings(a_result);

  bool found_outer{false}, found_inner{false};
  for (auto& l : lines) {
    if (is_label_with(l, "outer")) found_outer = true;
    if (is_label_with(l, "inner")) found_inner = true;
  }
  CHECK(found_inner);
  CHECK(!found_outer);
}
