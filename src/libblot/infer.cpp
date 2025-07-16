#include "blot/infer.hpp"

#include <clang-c/Index.h>
#include <fmt/std.h>

#include <boost/json.hpp>
#include <fstream>
#include <string>

#include "auto.hpp"
#include "logger.hpp"
#include "utils.hpp"

namespace xpto::blot {

namespace fs = std::filesystem;

std::optional<fs::path> infer(
    const fs::path& compile_commands_path, const fs::path& header_file) {
  LOG_INFO(
      "Searching for includes of '{}' in '{}'", header_file.string(),
      compile_commands_path.string());

  // Parse compile_commands.json to get all .cpp files
  std::ifstream file(compile_commands_path);
  if (!file.is_open())
    utils::throwf(
        "Failed to open compile_commands.json: {}",
        compile_commands_path.string());

  // Create clang index
  CXIndex index = clang_createIndex(0, 0);
  AUTO(clang_disposeIndex(index));

  std::string content(
      (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

  auto json_data = boost::json::parse(content);
  auto& entries = json_data.as_array();

  struct context_s {
    const fs::path* includee{};
    const fs::path* includer{};
  } context{&header_file};

  // Process each .cpp file
  for (auto&& entry : entries) {
    auto& obj = entry.as_object();
    fs::path file = obj["file"].as_string().c_str();
    fs::path dir = obj["directory"].as_string().c_str();
    fs::path full = dir / file;
    context.includer = &full;

    CXTranslationUnit unit = clang_parseTranslationUnit(
        index, full.c_str(), nullptr, 0, nullptr, 0, CXTranslationUnit_None);
    AUTO(clang_disposeTranslationUnit(unit));

    if (!unit) continue;  // what is this? failure to parse? ¯\_(ツ)_/¯

    // Get the number of inclusions
    try {
      clang_getInclusions(
          unit,
          [](CXFile included_file, CXSourceLocation* inclusion_stack,
             unsigned include_len, CXClientData cookie) {
            auto* context = static_cast<context_s*>(cookie);

            CXString filename = clang_getFileName(included_file);
            AUTO(clang_disposeString(filename));
            fs::path includee = clang_getCString(filename);

            // Check if this inclusion matches our target header
            if (includee == *context->includee ||
                includee.filename() == context->includee->filename()) {
              LOG_INFO(
                  "SUCCESS: Found includer of '{}' in translation unit '{}'",
                  *context->includee, *context->includer);
              throw context->includer;
            }
          },
          &context);  // NOLINT
    } catch (const fs::path* found) {
      return *found;
    }
  }
  return std::nullopt;
}

}  // namespace xpto::blot
