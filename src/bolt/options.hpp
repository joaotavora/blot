#pragma once

#include "bolt/bolt.hpp"

#include <boost/program_options.hpp>

namespace xpto::bolt {

bool parse_options(
    std::vector<const char*> args, int& loglevel,
    std::string& asm_file_name, xpto::bolt::generation_options& gen_options);
}
