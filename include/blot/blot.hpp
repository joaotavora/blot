#pragma once

/**
 * @file blot.hpp
 * @brief Core assembly annotation API.
 *
 * The central function is @c annotate(), which takes raw assembly text and
 * returns a filtered, annotated view of it together with source-to-assembly
 * line mappings.  The result contains @c std::string_view members that point
 * into the original input buffer, so the caller must keep the input alive for
 * as long as the @c annotation_result is in use.  Call @c apply_demanglings()
 * to obtain an owned copy of the output lines with C++ symbol names replaced
 * by their demangled forms.
 */

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace xpto::blot {

/** @brief Flags controlling which elements @c annotate() emits.
 *
 * All flags default to @c false.  @c preserve_directives keeps assembler
 * directives (lines beginning with @c .).  @c preserve_comments keeps inline
 * comments.  @c preserve_library_functions keeps calls to standard-library
 * and compiler-runtime symbols that would otherwise be elided.
 * @c preserve_unused_labels keeps labels that are not referenced elsewhere
 * in the output.  @c demangle replaces mangled C++ symbol names with their
 * human-readable demangled forms via @c apply_demanglings().
 */
struct annotation_options {
  bool preserve_directives{};
  bool preserve_comments{};
  bool preserve_library_functions{};
  bool preserve_unused_labels{};
  bool demangle{};
};

/** @brief Line-number type for mapping structures. */
using linum_t = size_t;

/** @brief Mapping from source line to assembly output range.
 *
 * @c source_line is a 1-based line number in the original source file.
 * @c asm_start and @c asm_end are 1-based line numbers in the
 * @c annotate() output, and the range is inclusive.
 */
struct line_mapping {
  linum_t source_line{};
  linum_t asm_start{};
  linum_t asm_end{};
};

/** @brief Sequence of source-to-assembly line range mappings. */
using linemap_t = std::vector<line_mapping>;

/** @brief Result of an @c annotate() call.
 *
 * @c output is a sequence of @c string_view values, each pointing into the
 * input buffer passed to @c annotate().  The input must remain valid and
 * unmodified for as long as these views are in use.
 *
 * @c linemap maps source lines to ranges of assembly output lines.
 *
 * @c demanglings is a list of @c (mangled, demangled) pairs in the order
 * they appear in @c output.  It is consumed by @c apply_demanglings() to
 * produce an owned copy of the output with substitutions applied.
 */
struct annotation_result {
  std::vector<std::string_view> output;
  linemap_t linemap;
  std::vector<std::pair<std::string_view, std::string>> demanglings;
};

/** @brief Annotate assembly text and return filtered output.
 *
 * @p input is the complete assembly text, typically the @c assembly field
 * of a @c compilation_result.  The function performs two passes: the first
 * identifies functions, labels, and source mappings; the second emits the
 * filtered lines according to @p options.  The returned @c string_view
 * members in @c annotation_result::output point directly into @p input, so
 * @p input must outlive the result.
 *
 * @p target_file names the file whose functions should appear in the output.
 * Only functions that have at least one @c .loc directive referencing this
 * file are emitted; functions from other files (e.g. the @c .cpp translation
 * unit that includes a header) are filtered out.  When @p target_file is
 * @c nullopt the first @c .file entry in the assembly is used instead, which
 * is the correct behaviour when annotating a translation unit directly.
 */
annotation_result annotate(
    std::span<const char> input, const annotation_options& options,
    const std::optional<std::filesystem::path>& target_file = std::nullopt);

/** @brief Return annotated output with symbols demangled.
 *
 * Returns a @c vector<string> with the same number of elements as
 * @c result.output.  Each element is a copy of the corresponding view with
 * any mangled symbol names replaced by their demangled forms recorded in
 * @c result.demanglings.  Safe to use after the original input buffer has
 * been destroyed.
 */
std::vector<std::string> apply_demanglings(const annotation_result& result);

}  // namespace xpto::blot
