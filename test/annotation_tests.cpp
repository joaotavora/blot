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

struct TestFixture {
  fs::path fixture_dir;
  fs::path ccj_path;
  fs::path original_dir;

  TestFixture() : fixture_dir{TEST_FIXTURE_DIR} {
    original_dir = fs::current_path();

    ccj_path = fixture_dir / "compile_commands.json";

    // Change to fixture directory for relative path compilation
    fs::current_path(fixture_dir);
  }

  TestFixture(const TestFixture&) = default;
  TestFixture(TestFixture&&) = delete;
  TestFixture& operator=(const TestFixture&) = default;
  TestFixture& operator=(TestFixture&&) = delete;
  ~TestFixture() {
    // Return to original directory
    fs::current_path(original_dir);
  }

  // Reusable test function for any fixture
  void test_annotation_against_expectation(
      const fs::path& cpp_file, const fs::path& expectation_file,
      const fs::path& compile_commands_file,
      const xpto::blot::annotation_options& aopts = {}) {
    // Generate assembly from compile commands
    auto cmd =
        xpto::blot::find_compile_command(compile_commands_file, cpp_file);
    REQUIRE(cmd.has_value());
    auto c_result =
        xpto::blot::get_asm(cmd->directory, cmd->command, cmd->file);

    // Run blot annotation with provided options
    auto a_result = xpto::blot::annotate(c_result.assembly, aopts);

    // Load expected results
    std::ifstream file(expectation_file);
    REQUIRE(file.is_open());
    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
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
};

// Global fixture instance
TestFixture fixture;

TEST_CASE("api_gcc_basic") {
  fixture.test_annotation_against_expectation(
      "fxt_gcc_basic.cpp", "fxt_gcc_basic.json", fixture.ccj_path);
}

TEST_CASE("api_gcc_still_pretty_basic") {
  fixture.test_annotation_against_expectation(
      "fxt_gcc_still_pretty_basic.cpp", "fxt_gcc_still_pretty_basic.json",
      fixture.ccj_path);
}

TEST_CASE("api_gcc_demangle") {
  fixture.test_annotation_against_expectation(
      "fxt_gcc_demangle.cpp", "fxt_gcc_demangle.json", fixture.ccj_path,
      {.demangle = true});
}

TEST_CASE("api_gcc_preserve_directives") {
  fixture.test_annotation_against_expectation(
      "fxt_gcc_preserve_directives.cpp", "fxt_gcc_preserve_directives.json",
      fixture.ccj_path,
      {.preserve_directives = true, .preserve_comments = true});
}

TEST_CASE("api_gcc_preserve_library_functions") {
  fixture.test_annotation_against_expectation(
      "fxt_gcc_preserve_library_functions.cpp",
      "fxt_gcc_preserve_library_functions.json", fixture.ccj_path,
      {.preserve_library_functions = true});
}

TEST_CASE("api_gcc_no_preserve_library_functions") {
  fixture.test_annotation_against_expectation(
      "fxt_gcc_preserve_library_functions.cpp",
      "fxt_gcc_no_preserve_library_functions.json", fixture.ccj_path,
      {.preserve_library_functions = false});
}

TEST_CASE("api_gcc_minimal") {
  fixture.test_annotation_against_expectation(
      "fxt_gcc_minimal.cpp", "fxt_gcc_minimal.json", fixture.ccj_path);
}

TEST_CASE("api_clang_preserve_library_functions") {
  fixture.test_annotation_against_expectation(
      "fxt_clang_preserve_library_functions.cpp",
      "fxt_clang_preserve_library_functions.json", fixture.ccj_path,
      {.preserve_library_functions = true});
}

TEST_CASE("api_clang_demangle") {
  fixture.test_annotation_against_expectation(
      "fxt_clang_demangle.cpp", "fxt_clang_demangle.json", fixture.ccj_path,
      {.demangle = true});
}

TEST_CASE("api_gcc_errors") {
  // This test verifies that compilation errors are properly handled
  auto cmd =
      xpto::blot::find_compile_command(fixture.ccj_path, "fxt_gcc_errors.cpp");
  REQUIRE(cmd.has_value());

  // The get_asm function should throw when compilation fails
  CHECK_THROWS_AS(
      xpto::blot::get_asm(cmd->directory, cmd->command, cmd->file),
      std::runtime_error);
}
