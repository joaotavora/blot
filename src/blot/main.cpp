#include <fmt/std.h>
#include <re2/re2.h>
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <boost/json.hpp>
#include <boost/json/array.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <variant>

#include "../libblot/json_helpers.hpp"
#include "../libblot/linespan.hpp"
#include "../libblot/logger.hpp"
#include "../libblot/utils.hpp"
#include "blot/assembly.hpp"
#include "blot/blot.hpp"
#include "blot/ccj.hpp"
#include "options.hpp"
#include "web_server.hpp"

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
    auto cmd = blot::infer(ccj_path, *fopts.src_file_name);
    if (!cmd) throwf("Can't find an entry for {}", *fopts.src_file_name);

    LOG_INFO("Got this command '{}'", cmd->command);

    auto c_result = blot::get_asm(*cmd);
    return c_result;
  } else {
    LOG_INFO("Reading from stdin");
    return simple_input{slurp(std::cin), true};
  }
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

int main_nojson(blot::file_options& fopts, blot::annotation_options& aopts) {
  try {
    auto [_directory, input] = std::visit(
        [&](auto&& w) -> std::pair<fs::path, std::string> {
          using T = std::decay_t<decltype(w)>;
          if constexpr (std::is_same_v<T, simple_input>) {
            return {fs::current_path(), w.assembly};
          } else {
            return {w.invocation.directory, w.assembly};
          }
        },
        grab_input(fopts));
    auto a_result = annotate(input, aopts, fopts.src_file_name);
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

  if (fopts.web_mode) {
    fs::path ccj_path;
    if (fopts.compile_commands_path) {
      ccj_path = *fopts.compile_commands_path;
    } else {
      auto ccj = blot::find_ccj();
      if (!ccj) {
        std::cerr << "blot --web: can't find compile_commands.json in cwd\n";
        return 1;
      }
      ccj_path = *ccj;
    }
    blot::run_web_server(ccj_path, fopts.port);
    return 0;
  }

  if (!json_output) return main_nojson(fopts, aopts);

  // from this point on, JSON stuff
  json::object json_result;
  int retval = 0;
  json_result["cwd"] = std::filesystem::current_path().string();
  json_result["annotation_options"] = aopts_to_json(aopts);
  json_result["file_options"] = fopts_to_json(fopts);

  try {
    std::string assembly;
    std::visit(
        [&](auto&& w) {
          using T = std::decay_t<decltype(w)>;
          if constexpr (std::is_same_v<T, simple_input>) {
            assembly = w.assembly;
            json_result.erase("file_options");  // it would be confusing
            json_result["assembly_file"] =
                w.from_stdin ? "<stdin>" : fopts.asm_file_name->c_str();
          } else {
            assembly = w.assembly;
            json_result["compiler_invocation"] = meta_to_json(w.invocation);
          }
        },
        grab_input(fopts));
    auto res = annotate_to_json(assembly, aopts, fopts.src_file_name);
    json_result.insert(res.begin(), res.end());
  } catch (blot::compilation_error& e) {
    json_result["compiler_invocation"] = meta_to_json(e.invocation);
    json_result["error"] = error_to_json(e);
    auto& desc = json_result["error"].as_object();
    xpto::linespan ls{e.dribble};
    desc["dribble"] = json::array(ls.begin(), ls.end());
    retval = -1;
  } catch (std::exception& e) {
    json_result["error"] = blot::error_to_json(e);
    retval = -1;
  }
  std::cout << json::serialize(json_result) << "\n";
  return retval;
}
