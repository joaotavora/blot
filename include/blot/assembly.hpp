#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace xpto::blot {

namespace fs = std::filesystem;

struct compiler_invocation {
  std::string compiler;
  std::vector<std::string> args;
  fs::path directory;
  std::string compiler_version;
};

struct compilation_error : std::runtime_error {
  compilation_error(
      const std::string& desc, compiler_invocation i, std::string s)
      : std::runtime_error{desc},
        invocation{std::move(i)},
        dribble{std::move(s)} {}
  compiler_invocation invocation;
  std::string dribble;
};

struct compilation_result {
  std::string assembly;
  compiler_invocation invocation;
};

compilation_result get_asm(
    const fs::path& directory, const std::string& command,
    const fs::path& file);

}  // namespace xpto::blot
