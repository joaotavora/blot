#include <fmt/std.h>
#include <re2/re2.h>
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <boost/json.hpp>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>

#include "../libblot/logger.hpp"
#include "../libblot/utils.hpp"
#include "blot/assembly.hpp"
#include "blot/blot.hpp"
#include "blot/ccj.hpp"
#include "blot/infer.hpp"
#include "options.hpp"

namespace fs = std::filesystem;
namespace blot = xpto::blot;
namespace json = boost::json;

using blot::utils::throwf;

auto grab_input(const blot::file_options& fopts) {
  LOG_DEBUG(
      "asm_file_name={}\nsrc_file_name={}\ncompile_commands_path={}",
      fopts.asm_file_name, fopts.src_file_name, fopts.compile_commands_path);

  auto slurp = [](std::istream& in) -> std::string {
    return std::string{
      std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>()};
  };

  if (fopts.asm_file_name) {
    LOG_INFO("Reading from {}", *fopts.asm_file_name);
    std::ifstream fstream;
    fstream.open(*fopts.asm_file_name);
    return slurp(fstream);
  } else if (fopts.src_file_name) {
    fs::path ccj_path;
    if (fopts.compile_commands_path) {
      ccj_path = *fopts.compile_commands_path;
      LOG_INFO("Using provided compile_commands.json: {}", ccj_path);
    } else {
      auto ccj = xpto::blot::find_ccj();
      if (!ccj) throwf("Can't find compile_commands.json");
      ccj_path = *ccj;
      LOG_INFO("Detected {}", ccj_path);
    }
    auto cmd = xpto::blot::find_compile_command(ccj_path, *fopts.src_file_name);
    if (!cmd) throwf("Can't find an entry for {}", *fopts.src_file_name);

    LOG_INFO("Got this command '{}'", cmd->command);

    return xpto::blot::get_asm(cmd->directory, cmd->command, cmd->file);
  } else {
    LOG_INFO("Reading from stdin");
    return slurp(std::cin);
  }
}

auto annotate(const std::string& input,
              const blot::annotation_options& aopts,
              bool json_output) {
  LOG_DEBUG(
      "-pd={}\n-pl={}\n-pc={}\n-pu={}\n-dm={}", aopts.preserve_directives,
      aopts.preserve_library_functions, aopts.preserve_comments,
      aopts.preserve_unused_labels, aopts.demangle);
  LOG_INFO("Annotating {} bytes of asm", input.length());
  auto result = xpto::blot::annotate(input, aopts);

  if (json_output) {
    json::object json_result;

    auto output_lines = xpto::blot::apply_demanglings(result);
    json::array assembly_lines(output_lines.begin(), output_lines.end());

    json::array line_mappings;
    for (auto&& [src_line, asm_start, asm_end] : result.linemap) {
      json::object mapping;
      mapping["source_line"] = src_line;
      mapping["asm_start"] = asm_start;
      mapping["asm_end"] = asm_end;
      line_mappings.push_back(mapping);
    }

    json_result["assembly"] = assembly_lines;
    json_result["line_mappings"] = line_mappings;

    std::cout << json::serialize(json_result) << "\n";
  } else {
    auto output_lines = xpto::blot::apply_demanglings(result);
    for (const auto& line : output_lines) {
      std::cout << line << "\n";
    }
  }
}

int main(int argc, char* argv[]) {

  xpto::blot::infer();
  xpto::blot::file_options fopts{};
  xpto::blot::annotation_options aopts{};
  int loglevel{3};
  bool json_output{false};

  auto done =
    parse_options(std::span(argv, argc), loglevel, fopts, aopts, json_output);
  if (done) return done.value();

  xpto::logger::set_level(static_cast<xpto::logger::level>(loglevel));
  LOG_DEBUG("loglevel={}", loglevel);

  try {
    auto input = grab_input(fopts);
    annotate(input, aopts, json_output);
  } catch (std::exception& e) {
    if (json_output) {
      json::object json_result;
      json_result["error"] = blot::utils::demangle_symbol(typeid(e).name());
      json_result["details"] = e.what();
      std::cout << json::serialize(json_result) << "\n";
    } else {
      std::cerr << e.what() << "\n";
      return -1;
    }
  }
}
