#include "ccj.hpp"

#include <boost/filesystem.hpp>
#include <boost/json.hpp>
#include <boost/system/system_error.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>

#include "blot/logger.hpp"

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
    try {
      const auto& obj = entry.as_object();
      auto get = [&](const char* k) { return obj.at(k).as_string().c_str(); };

      fs::path file = fs::absolute(get("file"));
      fs::path directory = fs::absolute(get("directory"));
      std::string command = get("command");

      if (file == target_path)
        return compile_command{
          .directory = directory,
          .command = command,
          .file = file,
        };
    }
    // NOLINTNEXTLINE(*empty-catch*)
    catch (boost::system::system_error& e) {
      LOG_INFO("Having trouble because {}", e.what());
    }
  }
  LOG_ERROR("No compilation command found for {}", target_path.c_str());
  return std::nullopt;
}

}  // namespace xpto::blot
