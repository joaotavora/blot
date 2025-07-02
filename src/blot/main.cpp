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
namespace json = boost::json;


int main(int argc, char* argv[]) { // NOLINT(*exception*)
  xpto::blot::file_options fopts;
  xpto::blot::annotation_options aopts{};
  int loglevel{3};
  bool json_output{false};
  auto done = parse_options(
      std::span(argv, argc), loglevel, fopts, aopts, json_output);
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
  auto result = xpto::blot::annotate(input, aopts);
  
  if (json_output) {
    json::object json_result;
    json::array assembly_lines;
    
    for (auto&& line : result.output) {
      assembly_lines.push_back(json::value(line));
    }
    
    json::array line_mappings;
    for (auto&& [src_line, asm_ranges] : result.linemap) {
      for (auto&& [asm_start, asm_end] : asm_ranges) {
        json::object mapping;
        mapping["source_line"] = src_line;
        mapping["asm_start"] = asm_start;
        mapping["asm_end"] = asm_end;
        line_mappings.push_back(mapping);
      }
    }
    
    json_result["assembly"] = assembly_lines;
    json_result["line_mappings"] = line_mappings;
    
    std::cout << json::serialize(json_result) << std::endl;
  } else {
    for (auto&& line : result.output) {
      std::cout << line << "\n";
    }
  }
}
