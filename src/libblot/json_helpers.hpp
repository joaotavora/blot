#pragma once

#include <boost/json.hpp>
#include <boost/json/array.hpp>
#include <filesystem>
#include <optional>

#include "blot/assembly.hpp"
#include "blot/blot.hpp"
#include "utils.hpp"

namespace xpto::blot {

namespace json = boost::json;
namespace fs = std::filesystem;

inline json::object aopts_to_json(const annotation_options& aopts) {
  json::object annotation_options;
  annotation_options["demangle"] = aopts.demangle;
  annotation_options["preserve_directives"] = aopts.preserve_directives;
  annotation_options["preserve_library_functions"] =
      aopts.preserve_library_functions;
  annotation_options["preserve_comments"] = aopts.preserve_comments;
  annotation_options["preserve_unused_labels"] = aopts.preserve_unused_labels;
  return annotation_options;
}

inline json::object meta_to_json(const compiler_invocation& inv) {
  json::object meta;
  meta["compiler_version"] = inv.compiler_version;
  meta["directory"] = inv.directory.c_str();
  meta["compiler"] = inv.compiler;
  meta["args"] = json::array(inv.args.begin(), inv.args.end());
  return meta;
}

inline json::object annotate_to_json(
    std::string_view input, const annotation_options& aopts,
    const std::optional<fs::path>& target_file = std::nullopt) {
  json::object res;
  auto a_result = annotate(input, aopts, target_file);
  auto output_lines = apply_demanglings(a_result);

  json::array assembly_lines(output_lines.begin(), output_lines.end());
  json::array line_mappings;
  for (auto&& [src_line, asm_start, asm_end] : a_result.linemap) {
    json::object mapping;
    mapping["source_line"] = src_line;
    mapping["asm_start"] = asm_start;
    mapping["asm_end"] = asm_end;
    line_mappings.push_back(mapping);
  }

  res["assembly"] = assembly_lines;
  res["line_mappings"] = line_mappings;

  return res;
}

inline json::object error_to_json(const std::exception& e) {
  json::object res;
  res["name"] = utils::demangle_symbol(typeid(e).name());
  res["details"] = e.what();
  return res;
}

}  // namespace xpto::blot
