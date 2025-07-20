#include "blot/ccj.hpp"

#include <clang-c/Index.h>
#include <fmt/std.h>

#include <boost/filesystem.hpp>
#include <boost/json.hpp>
#include <boost/system/system_error.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "auto.hpp"
#include "logger.hpp"
#include "utils.hpp"

namespace xpto::blot {

namespace fs = std::filesystem;
namespace json = boost::json;
namespace bs = boost::system;

std::optional<fs::path> find_ccj() {
  auto probe = fs::current_path() / "compile_commands.json";
  if (fs::exists(probe)) return probe;
  return std::nullopt;
}

std::optional<compile_command> find_compile_command(
    const fs::path& compile_commands_path, const fs::path& target_path) {
  // First, parse the compile_commands.json file as is
  json::value json_content = [&]() {
    std::ifstream blob(compile_commands_path.string());
    if (!blob) {
      throw std::runtime_error(
          fmt::format(
              "Could not open compile_commands.json at {}",
              compile_commands_path));
    }

    std::string content(
        (std::istreambuf_iterator<char>(blob)),
        std::istreambuf_iterator<char>());
    auto ret = json::parse(content);
    if (!ret.is_array()) {
      throw std::runtime_error(
          std::format(
              "Could not open compile_commands.json at {}",
              compile_commands_path.string()));
    }
    return ret;
  }();

  fs::path ccj_dir = compile_commands_path.parent_path();
  auto absolute_maybe = [&](const fs::path& p) -> fs::path {
    if (p.is_absolute()) {
      return p;
    } else {
      auto hmm = ccj_dir / p;
      return fs::absolute(hmm);
    }
  };

  for (const auto& entry : json_content.as_array()) {
    try {
      const auto& obj = entry.as_object();
      auto get = [&](const char* k) { return obj.at(k).as_string().c_str(); };

      fs::path ccj_entry_file = get("file");

      fs::path for_comp = ccj_entry_file.is_absolute()
                              ? fs::absolute(target_path)
                              : fs::relative(target_path, ccj_dir);

      if (ccj_entry_file == for_comp)
        return compile_command{
          .directory = absolute_maybe(get("directory")),
          .command = get("command"),
          .file = absolute_maybe(ccj_entry_file),
        };
    } catch (boost::system::system_error& e) {
      LOG_INFO("Having trouble because {}", e.what());
    }
  }
  LOG_ERROR(
      "No compilation command found for {} in {}", target_path,
      compile_commands_path);
  return std::nullopt;
}

// Helper function to create a translation unit from a file and its compile
// command
CXTranslationUnit create_translation_unit(
    CXIndex index, const fs::path& file_path, const std::string& command) {
  std::vector<std::string> args;
  std::vector<const char*> argv;
  std::istringstream iss(command);
  std::string token;

  // Skip the compiler executable name
  iss >> token;

  // Collect arguments until we hit compilation output flags
  // FIXME: In theory, include directories can come after -c and -o flags,
  // but libclang doesn't seem to handle this correctly. We may need to parse
  // the entire command and reorder arguments appropriately in the future.
  while (iss >> token) {
    if (token == "-o" || token == "-c") {
      break;
    }
    args.push_back(token);
  }

  argv.reserve(args.size());
  for (const auto& arg : args) {
    argv.push_back(arg.data());
  }

  return clang_parseTranslationUnit(
      index, file_path.c_str(), argv.data(), static_cast<int>(argv.size()),
      nullptr, 0, CXTranslationUnit_None);
}

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
    bool match{};
  } context{&header_file};

  // Process each .cpp file
  for (auto&& entry : entries) {
    auto& obj = entry.as_object();
    fs::path file = obj["file"].as_string().c_str();
    fs::path dir = obj["directory"].as_string().c_str();
    fs::path full = dir / file;
    context.includer = &full;

    // Parse command arguments and create translation unit
    std::string command = obj["command"].as_string().c_str();
    CXTranslationUnit unit = create_translation_unit(index, full, command);
    AUTO(clang_disposeTranslationUnit(unit));

    if (!unit) continue;  // what is this? failure to parse? ¯\_(ツ)_/¯
    LOG_DEBUG("OK: Examining '{}'", full);
    clang_getInclusions(
        unit,
        [](CXFile included_file, CXSourceLocation* inclusion_stack,
           unsigned include_len, CXClientData cookie) {
          auto* context = static_cast<context_s*>(cookie);

          CXString filename = clang_getFileName(included_file);
          AUTO(clang_disposeString(filename));
          fs::path includee = clang_getCString(filename);
          LOG_DEBUG("   OK: Saw this includee '{}'", includee);

          // Check if this inclusion matches our target header
          if (includee == *context->includee ||
              includee.filename() == context->includee->filename()) {
            LOG_INFO(
                "SUCCESS: Found includer of '{}' in translation unit '{}'",
                *context->includee, *context->includer);
            context->match = true;
          }
        },
        &context);
    if (context.match) return *context.includer;
  }
  return std::nullopt;
}

}  // namespace xpto::blot
