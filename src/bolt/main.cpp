#include "options.hpp"
#include "ccj.hpp"
#include "assembly.hpp"

#include "bolt/bolt.hpp"
#include "bolt/logger.hpp"

#include <re2/re2.h>

#include <unistd.h>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <optional>

namespace fs = std::filesystem;
namespace bolt = xpto::bolt;

int main(int argc, char* argv[]) {

  xpto::bolt::generation_options gen_options{};
  int loglevel{};
  std::string asm_file_name{};

  auto done = parse_options(
      std::vector<const char*>{argv, argv + argc}, loglevel, asm_file_name,
      gen_options);
  if (done) return 0;

  xpto::logger::set_level(static_cast<xpto::logger::level>(loglevel));

  std::string input{};

  if (asm_file_name.size()) {
    LOG_INFO("Reading from {}", asm_file_name);
    std::ifstream fstream;
    fstream.open(asm_file_name);
    input = bolt::get_asm(fstream);
  } else if (auto ccj = xpto::bolt::find_compile_commands(); ccj
    && ::isatty(::fileno(stdin))) {
    LOG_INFO("Detected {}", ccj->c_str());
  } else {
    LOG_INFO("Reading from stdin");
    input = bolt::get_asm(std::cin);
  }

  LOG_INFO("Annotating {} bytes of asm", input.length());
  for (auto&& l : xpto::bolt::annotate(input, gen_options))
    std::cout << l << "\n";
}
