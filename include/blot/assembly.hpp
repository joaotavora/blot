#pragma once

#include <filesystem>
#include <string>

namespace xpto::blot {

namespace fs = std::filesystem;

std::string get_asm(
    const fs::path& directory, const std::string& command,
    const fs::path& file);

}  // namespace xpto::blot
