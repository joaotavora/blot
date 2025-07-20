#pragma once

#include <filesystem>
#include <string>

namespace xpto::blot {

namespace fs = std::filesystem;

struct compile_command {
  fs::path directory;
  std::string command;
  fs::path file;
};

}  // namespace xpto::blot