#include "bolt/bolt.hpp"

#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>

bool parse_options(
    std::vector<const char*> args, int& loglevel,
    std::string& asm_file_name, xpto::bolt::generation_options& gen_options) {
  namespace po = boost::program_options;
  // clang-format off
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "show this help")
    ("preserve-directives,pd",
        po::bool_switch(&gen_options.preserve_directives)->default_value(false),
        "preserve all non-comment assembly directives")
    ("preserve-comments,pc",
        po::bool_switch(&gen_options.preserve_comments)->default_value(false),
        "preserve comments")
    ("preserve-library-functions,pl",
        po::bool_switch(&gen_options.preserve_library_functions)->default_value(false),
        "preserve library functions")
    ("preserve-unused-labels,pu",
        po::bool_switch(&gen_options.preserve_unused_labels)->default_value(false),
        "preserve unused labels")
    ("d", po::value<int>(&loglevel)->default_value(
        static_cast<int>(xpto::logger::level::info)
        ),
        "Debug log level (default 3==INFO)")
    ("asm-file",
        po::value(&asm_file_name),
        "Read assembly from file ARG.")
    ;
  // clang-format on

  po::variables_map vm;
  po::store(po::parse_command_line(static_cast<int>(args.size()), args.data(), desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    desc.print(std::cout);
    return true;
  }
  return false;
}

// FIXME: doesn't belong in this file
std::vector<char> read_asm_from_file_or_stdin(const std::optional<std::string>& fname) {
  if (::isatty(fileno(stdin)) && fname) {
    std::ifstream fstream;
    fstream.open(*fname);
    std::vector<char> buf{
      std::istreambuf_iterator<char>{fstream},
      std::istreambuf_iterator<char>()};
    return buf;
  }
  std::vector<char> buf{
    std::istreambuf_iterator<char>{std::cin}, std::istreambuf_iterator<char>()};
  return buf;
}
