#define BOOST_TEST_MODULE BlotTests
#include <boost/json.hpp>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "blot/blot.hpp"
#include "blot/ccj.hpp"
#include "blot/assembly.hpp"

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
    BOOST_REQUIRE(file.is_open());

    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    auto parsed = json::parse(content);
    return parsed.as_object();
  }

  // Generate assembly for a test file using compile commands
  std::string generate_assembly(const std::string& test_file) {
    auto cmd = xpto::blot::find_compile_command(ccj_path, test_file);
    BOOST_REQUIRE(cmd.has_value());

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
    BOOST_REQUIRE(cmd.has_value());
    std::string assembly =
        xpto::blot::get_asm(cmd->directory, cmd->command, cmd->file);

    // Run blot annotation with provided options
    auto result = xpto::blot::annotate(assembly, aopts);

    // Load expected results
    std::ifstream file(expectation_file);
    BOOST_REQUIRE(file.is_open());
    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    auto expected = json::parse(content).as_object();

    // Compare assembly output
    auto& expected_assembly = expected["assembly"].as_array();
    BOOST_REQUIRE_EQUAL(result.output.size(), expected_assembly.size());
    for (size_t i = 0; i < result.output.size(); ++i) {
      BOOST_CHECK_EQUAL(result.output[i], expected_assembly[i].as_string());
    }

    // Compare line mappings
    auto& expected_mappings = expected["line_mappings"].as_object();
    BOOST_REQUIRE_EQUAL(result.linemap.size(), expected_mappings.size());

    for (auto& [src_line, asm_ranges] : result.linemap) {
      std::string src_key = std::to_string(src_line);
      BOOST_REQUIRE(expected_mappings.contains(src_key));

      auto& expected_ranges = expected_mappings[src_key].as_array();
      BOOST_REQUIRE_EQUAL(asm_ranges.size(), expected_ranges.size());

      auto range_it = asm_ranges.begin();
      for (size_t i = 0; i < expected_ranges.size(); ++i, ++range_it) {
        auto& expected_range = expected_ranges[i].as_object();
        BOOST_CHECK_EQUAL(range_it->first, expected_range["start"].as_int64());
        BOOST_CHECK_EQUAL(range_it->second, expected_range["end"].as_int64());
      }
    }
  }
};

BOOST_FIXTURE_TEST_SUITE(BlotTestSuite, TestFixture)

BOOST_AUTO_TEST_CASE(test00_annotation) {
  test_annotation_against_expectation("test00.cpp", "test00.json", ccj_path);
}

BOOST_AUTO_TEST_CASE(test01_annotation) {
  test_annotation_against_expectation("test01.cpp", "test01.json", ccj_path);
}

BOOST_AUTO_TEST_SUITE_END()
