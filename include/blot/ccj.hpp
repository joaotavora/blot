#pragma once

#include <filesystem>
#include <optional>

namespace xpto::blot {

namespace fs = std::filesystem;

struct compile_command {
  fs::path directory;
  std::string command;
  fs::path file;
};

std::optional<fs::path> find_ccj();

std::optional<compile_command> find_compile_command(
    const fs::path& compile_commands_path, const fs::path& target_file);

}
