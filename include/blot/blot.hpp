#pragma once

#include <map>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace xpto::blot {

struct annotation_options {
  bool preserve_directives{};
  bool preserve_comments{};
  bool preserve_library_functions{};
  bool preserve_unused_labels{};
  bool demangle{};
};

using linum_t = size_t;
using linemap_t = std::vector<std::tuple<linum_t, linum_t, linum_t>>;

struct source_location {
  std::string filename;
  linum_t line;
};

using assembly_map_t = std::map<linum_t, source_location>;

struct annotation_result {
  std::vector<std::string_view> output;
  linemap_t linemap;
  assembly_map_t assembly_map;
  std::vector<std::pair<std::string_view, std::string>> demanglings;
};

annotation_result annotate(
    std::span<const char> input, const annotation_options& options);

std::vector<std::string> apply_demanglings(const annotation_result& result);

}  // namespace xpto::blot
