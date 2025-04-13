#pragma once

#include <boost/json.hpp>
#include <filesystem>
#include <optional>

namespace xpto::bolt {

namespace fs = std::filesystem;
namespace json = boost::json;

std::optional<fs::path> find_compile_commands();

std::optional<json::object> find_compile_command(
    const fs::path& compile_commands_path, const fs::path& target_file);

}
