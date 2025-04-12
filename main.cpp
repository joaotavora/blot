#include <re2/re2.h>
#include <unistd.h>

#include <boost/json.hpp>
#include <boost/program_options.hpp>
#include <exception>
#include <iostream>
#include <fstream>
#include <iterator>
#include <print>

#include <cstdio>

#include "bolt/linespan.hpp"
#include "bolt/logger.hpp"
#include "bolt/bolt.hpp"

  // namespace xpto

namespace po = boost::program_options;
namespace json = boost::json;

std::vector<char> slurp(const std::optional<std::string>& fname) {
  if (::isatty(fileno(stdin)) && fname) {
    std::ifstream fstream;
    fstream.open(*fname);
    std::vector<char> buf{
    std::istreambuf_iterator<char>{fstream}, std::istreambuf_iterator<char>()};
    return buf;
  }
  std::vector<char> buf{std::istreambuf_iterator<char>{std::cin}, std::istreambuf_iterator<char>()};
  return buf;
}

int main(int argc, char* argv[]) {
  xpto::bolt::generation_options gen_options{};
  int loglevel{};
  std::string asm_file_name{};

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
    ("in",
        po::value(&asm_file_name),
        "Read assembly from file ARG.")
    ;
  // clang-format on

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  xpto::logger::set_level(static_cast<xpto::logger::level>(loglevel));

  if (vm.count("help")) {
    desc.print(std::cout);
    return 1;
  }

  auto buf = slurp(asm_file_name);
  xpto::linespan input{buf};

  try {
    for (auto&& l : xpto::bolt::annotate(input, gen_options))
      std::cout << l << "\n";

  } catch (std::exception& e) {
    LOG_FATAL("Whoops {}", e.what());
  }
}
