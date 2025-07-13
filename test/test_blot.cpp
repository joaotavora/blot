#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
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

  // Load expected JSON file
  json::object load_expected(const std::string& test_name) {
    std::ifstream file(test_name + ".json");
    REQUIRE(file.is_open());

    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    auto parsed = json::parse(content);
    return parsed.as_object();
  }

  // Generate assembly for a test file using compile commands
  std::string generate_assembly(const std::string& test_file) {
    auto cmd = xpto::blot::find_compile_command(ccj_path, test_file);
    REQUIRE(cmd.has_value());

    return xpto::blot::get_asm(cmd->directory, cmd->command, cmd->file);
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
    std::string assembly =
        xpto::blot::get_asm(cmd->directory, cmd->command, cmd->file);

    // Run blot annotation with provided options
    auto result = xpto::blot::annotate(assembly, aopts);

    // Load expected results
    std::ifstream file(expectation_file);
    REQUIRE(file.is_open());
    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    auto expected = json::parse(content).as_object();

    // Compare assembly output (apply demanglings)
    auto output_lines = xpto::blot::apply_demanglings(result);
    auto& expected_assembly = expected["assembly"].as_array();
    REQUIRE(output_lines.size() == expected_assembly.size());
    for (size_t i = 0; i < output_lines.size(); ++i) {
      CHECK(output_lines[i] == expected_assembly[i].as_string());
    }

    // Compare line mappings (new array format)
    auto& expected_mappings = expected["line_mappings"].as_array();
    REQUIRE(result.linemap.size() == expected_mappings.size());

    for (size_t i = 0; i < expected_mappings.size(); ++i) {
      auto& expected_mapping = expected_mappings[i].as_object();
      auto& [src_line, asm_start, asm_end] = result.linemap[i];

      CHECK(src_line == expected_mapping["source_line"].as_int64());
      CHECK(asm_start == expected_mapping["asm_start"].as_int64());
      CHECK(asm_end == expected_mapping["asm_end"].as_int64());
    }
  }
};

// Global fixture instance
TestFixture fixture;

TEST_CASE("test00_annotation") {
  fixture.test_annotation_against_expectation(
      "test00.cpp", "test00.json", fixture.ccj_path);
}

TEST_CASE("test01_annotation") {
  fixture.test_annotation_against_expectation(
      "test01.cpp", "test01.json", fixture.ccj_path);
}

TEST_CASE("test02_annotation_demangling") {
  fixture.test_annotation_against_expectation(
      "test02.cpp", "test02.json", fixture.ccj_path, {.demangle = true});
}

TEST_CASE("test03_annotation_comments") {
  fixture.test_annotation_against_expectation(
      "test03.cpp", "test03.json", fixture.ccj_path,
      {.preserve_directives = true, .preserve_comments = true});
}

TEST_CASE("test04_annotation_library_functions") {
  fixture.test_annotation_against_expectation(
      "test04.cpp", "test04.json", fixture.ccj_path,
      {.preserve_library_functions = true});
}

TEST_CASE("test04_annotation_no_library_functions") {
  fixture.test_annotation_against_expectation(
      "test04.cpp", "test04_no_preserve.json", fixture.ccj_path,
      {.preserve_library_functions = false});
}

TEST_CASE("test05_annotation_minimal") {
  fixture.test_annotation_against_expectation(
      "test05.cpp", "test05.json", fixture.ccj_path);
}
