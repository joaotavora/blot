#include <fmt/std.h>
#include <re2/re2.h>
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <boost/json.hpp>
#include <boost/json/array.hpp>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>

#include "../libblot/logger.hpp"
#include "../libblot/utils.hpp"
#include "blot/assembly.hpp"
#include "blot/blot.hpp"
#include "blot/ccj.hpp"
#include "options.hpp"

namespace fs = std::filesystem;
namespace blot = xpto::blot;
namespace json = boost::json;

using blot::utils::throwf;

struct simple_input {
  std::string assembly;
  bool from_stdin;
};

using grabbed_input_t = std::variant<simple_input, blot::compilation_result>;

grabbed_input_t grab_input(blot::file_options& fopts) {
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
    return simple_input{slurp(fstream), false};
  } else if (fopts.src_file_name) {
    fs::path ccj_path;
    if (fopts.compile_commands_path) {
      ccj_path = *fopts.compile_commands_path;
      LOG_INFO("Using provided compile_commands.json: {}", ccj_path);
    } else {
      auto ccj = blot::find_ccj();
      if (!ccj) throwf("Can't find compile_commands.json");
      ccj_path = *ccj;
      fopts.compile_commands_path = ccj_path;
      LOG_INFO("Detected {}", ccj_path);
    }
    auto cmd = blot::find_compile_command(ccj_path, *fopts.src_file_name);
    if (!cmd) throwf("Can't find an entry for {}", *fopts.src_file_name);

    LOG_INFO("Got this command '{}'", cmd->command);

    auto c_result = blot::get_asm(cmd->directory, cmd->command, cmd->file);
    return c_result;
  } else {
    LOG_INFO("Reading from stdin");
    return simple_input{slurp(std::cin), true};
  }
}

json::object aopts_to_json(const blot::annotation_options& aopts) {
  json::object annotation_options;
  annotation_options["demangle"] = aopts.demangle;
  annotation_options["preserve_directives"] = aopts.preserve_directives;
  annotation_options["preserve_library_functions"] =
      aopts.preserve_library_functions;
  annotation_options["preserve_comments"] = aopts.preserve_comments;
  annotation_options["preserve_unused_labels"] = aopts.preserve_unused_labels;
  return annotation_options;
}

json::object fopts_to_json(const blot::file_options& fopts) {
  json::object file_options;
  if (fopts.compile_commands_path)
    file_options["compile_commands_path"] =
        fopts.compile_commands_path->string();
  else
    file_options["compile_commands_path"] = nullptr;

  if (fopts.src_file_name)
    file_options["source_file"] = fopts.src_file_name->string();
  else
    file_options["source_file"] = nullptr;

  return file_options;
}

json::object meta_to_json(const blot::compiler_invocation& inv) {
  json::object meta;
  meta["compiler_version"] = nullptr;            // TODO: Get from compiler
  meta["stdlib_version"] = nullptr;              // TODO: Get from compiler
  meta["directory"] = inv.directory.c_str();
  meta["compiler"] = inv.compiler;
  meta["args"] = json::array(inv.args.begin(), inv.args.end());
  return meta;
}

json::object annotate_to_json(
    std::string_view input, const blot::annotation_options& aopts) {
  json::object res;
  auto a_result = annotate(input, aopts);
  auto output_lines = blot::apply_demanglings(a_result);

  json::array assembly_lines(output_lines.begin(), output_lines.end());
  json::array line_mappings;
  for (auto&& [src_line, asm_start, asm_end] : a_result.linemap) {
    json::object mapping;
    mapping["source_line"] = src_line;
    mapping["asm_start"] = asm_start;
    mapping["asm_end"] = asm_end;
    line_mappings.push_back(mapping);
  }

  res["assembly"] = assembly_lines;
  res["line_mappings"] = line_mappings;

  return res;
}

int main_nojson(blot::file_options& fopts, blot::annotation_options& aopts) {
  try {
    std::string input = std::visit(
        [&](auto&& w) {
          using T = std::decay_t<decltype(w)>;
          if constexpr (std::is_same_v<T, std::string>) {
            return w;
          } else {
            return w.assembly;
          }
        },
        grab_input(fopts));
    auto a_result = annotate(input, aopts);
    for (auto&& l : blot::apply_demanglings(a_result)) {
      std::cout << l << "\n";
    }
    return 0;
  } catch (std::exception& e) {
    std::cerr << e.what() << "\n";
    return -1;
  }
}

int main(int argc, char* argv[]) {
  blot::file_options fopts{};
  blot::annotation_options aopts{};
  int loglevel{3};
  bool json_output{false};

  auto done =
      parse_options(std::span(argv, argc), loglevel, fopts, aopts, json_output);
  if (done) return done.value();

  xpto::logger::set_level(static_cast<xpto::logger::level>(loglevel));
  LOG_DEBUG("loglevel={}", loglevel);

  if (!json_output) return main_nojson(fopts, aopts);

  // from this point on, JSON stuff
  json::object json_result;
  int retval = 0;
  json_result["cwd"] = std::filesystem::current_path().string();
  json_result["annotation_options"] = aopts_to_json(aopts);

  try {
    std::string assembly;
    std::visit(
        [&](auto&& w) {
          using T = std::decay_t<decltype(w)>;
          if constexpr (std::is_same_v<T, simple_input>) {
            assembly = w.assembly;
            json_result["assembly_file"] = w.from_stdin?"<stdin>":fopts.asm_file_name->c_str();
          } else {
            assembly = w.assembly;
            json_result["file_options"] = fopts_to_json(fopts);
            json_result["compiler_invocation"] = meta_to_json(w.invocation);
          }
        },
        grab_input(fopts));
    auto res = annotate_to_json(assembly, aopts);
    json_result.insert(res.begin(), res.end());
  } catch (std::exception& e) {
    json_result["error"] = blot::utils::demangle_symbol(typeid(e).name());
    json_result["details"] = e.what();
    retval = -1;
  }
  std::cout << json::serialize(json_result) << "\n";
  return retval;
}
