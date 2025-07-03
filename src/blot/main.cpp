#include "blot/assembly.hpp"
#include "blot/blot.hpp"
#include "blot/ccj.hpp"
#include "../libblot/logger.hpp"
#include "options.hpp"

#include <fmt/std.h>
#include <re2/re2.h>
#include <unistd.h>
#include <boost/json.hpp>
#include <CLI/CLI.hpp>

#include <fstream>
#include <iostream>
#include <optional>
#include <span>

namespace fs = std::filesystem;
namespace blot = xpto::blot;
namespace json = boost::json;

// Apply demanglings to a string_view, returning the result as a string
std::string apply_demanglings(
    std::string_view line, 
    const std::vector<std::pair<std::string_view, std::string>>& demanglings) {
  
  // No demanglings for this line? Just return as string
  bool has_demangling = false;
  for (const auto& [mangled_sv, demangled] : demanglings) {
    if (mangled_sv.data() >= line.data() && 
        mangled_sv.data() + mangled_sv.size() <= line.data() + line.size()) {
      has_demangling = true;
      break;
    }
  }
  
  if (!has_demangling) {
    return std::string{line};
  }
  
  // Apply demanglings
  std::string result;
  const char* pos = line.data();
  const char* end = line.data() + line.size();
  
  for (const auto& [mangled_sv, demangled] : demanglings) {
    // Check if this demangling applies to current line
    if (mangled_sv.data() >= line.data() && 
        mangled_sv.data() + mangled_sv.size() <= line.data() + line.size()) {
      
      // Append text before the mangled symbol
      result.append(pos, mangled_sv.data());
      
      // Append the demangled symbol
      result.append(demangled);
      
      // Move position past the mangled symbol
      pos = mangled_sv.data() + mangled_sv.size();
    }
  }
  
  // Append remaining text
  result.append(pos, end);
  
  return result;
}

int main(int argc, char* argv[]) {  // NOLINT(*exception*)
  xpto::blot::file_options fopts;
  xpto::blot::annotation_options aopts{};
  int loglevel{3};
  bool json_output{false};

  auto slurp = [](std::istream& in) -> std::string {
    return std::string{
      std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>()};
  };

  auto done =
      parse_options(std::span(argv, argc), loglevel, fopts, aopts, json_output);
  if (done) return done.value();

  LOG_INFO("loglevel={}", loglevel);

  LOG_DEBUG(
      "-pd={}\n-pl={}\n-pc={}\n-pu={}\n-dm={}", aopts.preserve_directives,
      aopts.preserve_library_functions, aopts.preserve_comments,
      aopts.preserve_unused_labels, aopts.demangle);

  LOG_DEBUG(
      "asm_file_name={}\nsrc_file_name={}\ncompile_commands_path={}",
      fopts.asm_file_name, fopts.src_file_name, fopts.compile_commands_path);

  xpto::logger::set_level(static_cast<xpto::logger::level>(loglevel));

  std::string input{};

  if (fopts.asm_file_name) {
    LOG_INFO("Reading from {}", *fopts.asm_file_name);
    std::ifstream fstream;
    fstream.open(*fopts.asm_file_name);
    input = slurp(fstream);
  } else if (fopts.src_file_name) {
    fs::path ccj_path;
    if (fopts.compile_commands_path) {
      ccj_path = *fopts.compile_commands_path;
      LOG_INFO("Using provided compile_commands.json: {}", ccj_path);
    } else {
      auto ccj = xpto::blot::find_ccj();
      if (!ccj) {
        LOG_ERROR("Can't find compile_commands.json");
        return -1;
      }
      ccj_path = *ccj;
      LOG_INFO("Detected {}", ccj_path);
    }
    auto cmd = xpto::blot::find_compile_command(ccj_path, *fopts.src_file_name);
    if (!cmd) {
      LOG_ERROR("Can't find an entry for {}", *fopts.src_file_name);
      return -1;
    }
    LOG_INFO("Got this command '{}'", cmd->command);

    input = xpto::blot::get_asm(cmd->directory, cmd->command, cmd->file);
  } else {
    LOG_INFO("Reading from stdin");
    input = slurp(std::cin);
  }

  LOG_INFO("Annotating {} bytes of asm", input.length());
  auto result = xpto::blot::annotate(input, aopts);

  if (json_output) {
    json::object json_result;
    json::array assembly_lines;

    for (auto&& line : result.output) {
      std::string output_line = aopts.demangle ? 
        apply_demanglings(line, result.demanglings) : 
        std::string{line};
      assembly_lines.push_back(json::value(output_line));
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

    std::cout << json::serialize(json_result) << "\n";
  } else {
    for (auto&& line : result.output) {
      if (aopts.demangle) {
        std::cout << apply_demanglings(line, result.demanglings) << "\n";
      } else {
        std::cout << line << "\n";
      }
    }
  }
}
