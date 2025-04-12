#include <re2/re2.h>
#include <unistd.h>

#include <boost/json.hpp>
#include <exception>
#include <iostream>

#include "bolt/bolt.hpp"
#include "bolt/logger.hpp"

namespace json = boost::json;

bool parse_options(
    std::vector<const char*> args, int& loglevel, std::string& asm_file_name,
    xpto::bolt::generation_options& gen_options);

std::vector<char> read_asm_from_file_or_stdin(const std::optional<std::string>& fname);

int main(int argc, char* argv[]) {
  xpto::bolt::generation_options gen_options{};
  int loglevel{};
  std::string asm_file_name{};

  auto done = parse_options(
      std::vector<const char*>{argv, argv + argc}, loglevel, asm_file_name,
      gen_options);
  if (done) return 0;

  xpto::logger::set_level(static_cast<xpto::logger::level>(loglevel));

  try {
    auto input = read_asm_from_file_or_stdin(asm_file_name);
    for (auto&& l : xpto::bolt::annotate(input, gen_options))
      std::cout << l << "\n";

  } catch (std::exception& e) {
    LOG_FATAL("Whoops {}", e.what());
  }
}
