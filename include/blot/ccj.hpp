#pragma once

#include <filesystem>
#include <optional>

#include "blot/compile_command.hpp"

namespace xpto::blot {

namespace fs = std::filesystem;

std::optional<fs::path> find_ccj();

std::optional<compile_command> infer(
    const fs::path& compile_commands_path, const fs::path& header_file);

}  // namespace xpto::blot
