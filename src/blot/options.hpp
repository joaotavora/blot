#pragma once

#include <filesystem>
#include <span>

#include "blot/blot.hpp"

namespace fs = std::filesystem;

namespace xpto::blot {

struct file_options {
  std::optional<fs::path> asm_file_name{};
  std::optional<fs::path> src_file_name{};
  std::optional<fs::path> compile_commands_path{};
  bool web_mode{};
  bool stdio_mode{};
  int port{4242};
  std::optional<fs::path> web_root{};
};

std::optional<int> parse_options(
    std::span<char*> args, int& loglevel, xpto::blot::file_options& fopts,
    xpto::blot::annotation_options& aopts, bool& json_output);
}  // namespace xpto::blot
