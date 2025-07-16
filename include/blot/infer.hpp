#pragma once

#include <filesystem>
#include <optional>

namespace xpto::blot {

std::optional<std::filesystem::path> infer(
    const std::filesystem::path& compile_commands_path, 
    const std::filesystem::path& header_file);

}  // namespace xpto::blot