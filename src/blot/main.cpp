#include "assembly.hpp"
#include "blot/blot.hpp"
#include "blot/logger.hpp"
#include "ccj.hpp"
#include "options.hpp"

#include <re2/re2.h>
#include <unistd.h>

#include <fmt/std.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <span>

#include <CLI/CLI.hpp>

namespace fs = std::filesystem;
namespace blot = xpto::blot;


int main(int argc, char* argv[]) { // NOLINT(*exception*)
  xpto::blot::file_options fopts;
  xpto::blot::annotation_options aopts{};
  int loglevel{3};

  auto done = parse_options(
      std::span(argv, argc), loglevel, fopts, aopts);
  if (done) return done.value();

  LOG_INFO("loglevel={}", loglevel);

  LOG_DEBUG("-pd={}\n-pl={}\n-pc={}\n-pu={}",
      aopts.preserve_directives,
      aopts.preserve_library_functions,
      aopts.preserve_comments,
      aopts.preserve_unused_labels
      );

  LOG_DEBUG("asm_file_name={}\nsrc_file_name={}",
      fopts.asm_file_name,
      fopts.src_file_name
      );

  xpto::logger::set_level(static_cast<xpto::logger::level>(loglevel));

  std::string input{};

  if (fopts.asm_file_name) {
    LOG_INFO("Reading from {}", *fopts.asm_file_name);
    std::ifstream fstream;
    fstream.open(*fopts.asm_file_name);
    input = blot::get_asm(fstream);
  } else if (!isatty(::fileno(stdin))) {
    LOG_INFO("Piped input detected");
    input = blot::get_asm(std::cin);
  } else if (fopts.src_file_name) {
    auto ccj = xpto::blot::find_ccj();
    if (!ccj) {
      LOG_ERROR("Can't find compile_commands.json");
      return -1;
    }
    LOG_INFO("Detected {}", *ccj);
    auto cmd = xpto::blot::find_compile_command(*ccj, *fopts.src_file_name);
    if (!cmd) {
      LOG_ERROR("Can't find an entry for {}", *fopts.src_file_name);
      return -1;
    }
    LOG_INFO("Got this command '{}'", cmd->command);
    input = xpto::blot::get_asm(cmd->directory, cmd->command, cmd->file);
  } else {
    LOG_INFO("Reading from stdin");
    input = blot::get_asm(std::cin);
  }

  LOG_INFO("Annotating {} bytes of asm", input.length());
  for (auto&& l : xpto::blot::annotate(input, aopts))
    std::cout << l << "\n";
}
