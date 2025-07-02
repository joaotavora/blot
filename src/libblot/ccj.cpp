#include "blot/ccj.hpp"

#include <boost/filesystem.hpp>
#include <boost/json.hpp>
#include <boost/system/system_error.hpp>
#include <fmt/std.h>
#include <filesystem>
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
    const fs::path& compile_commands_path, const fs::path& target_path) {
  // First, parse the compile_commands.json file as is
  json::value json_content = [&]() {
    std::ifstream blob(compile_commands_path.string());
    if (!blob) {
      throw std::runtime_error(fmt::format(
          "Could not open compile_commands.json at {}",
          compile_commands_path));
    }

    std::string content(
        (std::istreambuf_iterator<char>(blob)),
        std::istreambuf_iterator<char>());
    auto ret = json::parse(content);
    if (!ret.is_array()) {
      throw std::runtime_error(std::format(
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
        ?
        fs::absolute(target_path):
        fs::relative(target_path, ccj_dir);

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
  LOG_ERROR("No compilation command found for {} in {}",
      target_path,
      compile_commands_path);
  return std::nullopt;
}

}  // namespace xpto::blot
