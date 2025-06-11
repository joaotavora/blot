#include <re2/re2.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>

#include "assembly.hpp"
#include "blot/blot.hpp"
#include "blot/logger.hpp"
#include "ccj.hpp"
#include "options.hpp"

namespace fs = std::filesystem;
namespace blot = xpto::blot;

int main(int argc, char* argv[]) { // NOLINT(*exception*)
  xpto::blot::annotation_options gen_options{};
  int loglevel{};
  std::string asm_file_name{};
  std::string src_file_name{};

  auto done = parse_options(
      std::vector<const char*>{argv, argv + argc}, loglevel, asm_file_name,
      src_file_name, gen_options);
  if (done) return 0;

  xpto::logger::set_level(static_cast<xpto::logger::level>(loglevel));

  std::string input{};

  if (asm_file_name.size()) {
    LOG_INFO("Reading from {}", asm_file_name);
    std::ifstream fstream;
    fstream.open(asm_file_name);
    input = blot::get_asm(fstream);
  } else if (!isatty(::fileno(stdin))) {
    LOG_INFO("Piped input detected");
    input = blot::get_asm(std::cin);
  } else if (src_file_name.size()) {
    auto ccj = xpto::blot::find_ccj();
    if (!ccj) {
      LOG_ERROR("Can't find compile_commands.json", src_file_name);
      return -1;
    }
    LOG_INFO("Detected {}", ccj->c_str());
    auto cmd = xpto::blot::find_compile_command(*ccj, src_file_name);
    if (!cmd) {
      LOG_ERROR("Can't find an entry for {}", src_file_name);
      return -1;
    }
    LOG_INFO("Got this command '{}'", cmd->command);
    input = xpto::blot::get_asm(cmd->directory, cmd->command, cmd->file);
  } else {
    LOG_INFO("Reading from stdin");
    input = blot::get_asm(std::cin);
  }

  LOG_INFO("Annotating {} bytes of asm", input.length());
  for (auto&& l : xpto::blot::annotate(input, gen_options))
    std::cout << l << "\n";
}
