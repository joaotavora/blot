#pragma once

#include "blot/blot.hpp"

#include <filesystem>
#include <span>

namespace fs = std::filesystem;

namespace xpto::blot {

std::optional<int> parse_options(
    std::span<char*> args, int& loglevel,
    xpto::blot::file_options& fopts,
    xpto::blot::annotation_options& aopts);
}
