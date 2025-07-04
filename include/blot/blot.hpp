#pragma once

#include <set>
#include <map>
#include <vector>
#include <string>
#include <span>

namespace xpto::blot {

struct annotation_options {
  bool preserve_directives{};
  bool preserve_comments{};
  bool preserve_library_functions{};
  bool preserve_unused_labels{};
  bool demangle{};
};

using linum_t = size_t;
using linemap_t = std::map<linum_t, std::set<std::pair<linum_t, linum_t>>>;

struct annotation_result {
  std::vector<std::string_view> output;
  linemap_t linemap;
  std::vector<std::pair<std::string_view, std::string>> demanglings;
};

annotation_result annotate(
    std::span<const char> input, const annotation_options& options);
}  // namespace xpto::blot
