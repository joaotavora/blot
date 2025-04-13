#include "ccj.hpp"
#include "bolt/logger.hpp"

#include <boost/filesystem.hpp>
#include <boost/json.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>

namespace xpto::bolt {

namespace fs = std::filesystem;
namespace json = boost::json;
namespace bs = boost::system;

std::optional<fs::path> find_compile_commands() {
  auto probe = fs::current_path() / "compile_commands.json";
  if (fs::exists(probe)) return probe;
  return std::nullopt;
}

std::optional<json::object> find_compile_command(
    const fs::path& compile_commands_path, const fs::path& target_file) {
  std::ifstream file(compile_commands_path.string());
  if (!file) {
    throw std::runtime_error(std::format(
        "Could not open compile_commands.json at {}",
        compile_commands_path.string()));
  }

  std::string content(
      (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

  json::value json_content = json::parse(content);

  if (!json_content.is_array()) {
    throw std::runtime_error(std::format(
        "Could not open compile_commands.json at {}",
        compile_commands_path.string()));
  }

  // Find the right entry for our file
  fs::path target_path = fs::absolute(target_file);

  for (const auto& entry : json_content.as_array()) {
    if (!entry.is_object()) continue;

    const auto& obj = entry.as_object();
    if (!obj.contains("file")) continue;

    const auto& atfile = obj.at("file");
    if (!atfile.is_string()) continue;
    fs::path entry_path = fs::absolute(atfile.as_string().c_str());

    if (entry_path == target_path) return obj;
  }
  LOG_ERROR("No compilation command found for {}", target_path.c_str());
  return std::nullopt;
}

}  // namespace xpto::bolt
