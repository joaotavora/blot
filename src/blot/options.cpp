#include "options.hpp"
#include "blot/blot.hpp"

#include <CLI/CLI.hpp>
#include <optional>

namespace fs = std::filesystem;

namespace xpto::blot {

std::optional<int> parse_options(
    std::span<char*> args, int& loglevel,
    xpto::blot::file_options& fopts,
    xpto::blot::annotation_options& aopts,
    bool& json_output) {
  CLI::App app{"Compiler explorer-like util"};

  app.allow_non_standard_option_names();

  app.add_flag(
      "-pd,--preserve-directives", aopts.preserve_directives,
      "preserve all non-comment assembly-directives")
    ->capture_default_str();
  app.add_flag(
      "-pc,--preserve-comments", aopts.preserve_comments,
      "preserve comments")
    ->capture_default_str();
  app.add_flag(
      "-pu,--preserve-unused", aopts.preserve_unused_labels,
      "preserve unused labels")
    ->capture_default_str();
  app.add_flag(
      "-pl,--preserve-library-functions", aopts.preserve_library_functions,
      "preserve library functions")
    ->capture_default_str();
  app.add_flag(
      "--demangle", aopts.demangle,
      "demangle C++ symbols")
    ->capture_default_str();
  app.add_option(
      "-d, --debug",
      loglevel,
      "Debug log level (3=INFO)")
    ->capture_default_str();
  app.add_option(
      "--asm-file",
      fopts.asm_file_name,
      "Read assembly directly from file");
  app.add_option(
      "--compile_commands,--ccj",
      fopts.compile_commands_path,
      "Path to compile_commands.json file");
  app.add_flag(
      "--json", json_output,
      "Output results in JSON format")
    ->capture_default_str();
  app.add_option(
      "source-file",
      fopts.src_file_name,
      "Source file to annotate");

  try {
    (app).parse(std::vector<std::string>(args.begin() + 1, args.end()));
  } catch (const CLI ::ParseError& e) {
    return (app).exit(e);
  };

  return std::nullopt;
}

}  // namespace xpto::blot
