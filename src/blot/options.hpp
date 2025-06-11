#pragma once

#include "blot/blot.hpp"

#include <boost/program_options.hpp>

namespace xpto::blot {

bool parse_options(
    std::vector<const char*> args, int& loglevel, std::string& asm_file_name,
    std::string& src_file_name, xpto::blot::annotation_options& gen_options);
}
