#include "blot/blot.hpp"

#include <fmt/std.h>
#include <re2/re2.h>

#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "linespan.hpp"
#include "logger.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

namespace xpto::blot {

template <typename Dest, typename T, size_t N, std::size_t... Is>
constexpr auto make_pointer_array_impl(
    std::array<T, N>& values, std::index_sequence<Is...>) {
  return std::array<Dest, N>{&values[Is]...};
}

template <typename Dest, typename T, size_t N>
constexpr auto make_pointer_array(std::array<T, N>& values) {
  return make_pointer_array_impl<Dest>(values, std::make_index_sequence<N>{});
}

size_t to_size_t(std::string_view sv) {  // "or lose" semantics
  size_t result{};
  auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), result);
  if (ec == std::errc{} && ptr == sv.end()) return result;
  utils::throwf<std::runtime_error>("'{}' isn't a number!", sv);
}

using input_t = xpto::linespan;
using match_t = std::string_view;
using matches_t = std::span<match_t>;
using label_t = std::string_view;
using demangling_t = std::pair<std::string_view, std::string>;

template <typename Output, typename Input, typename F>
void sweeping(
    const Input& input, Output& output, const annotation_options& o, F fn) {
  size_t linum{1};

  std::array<match_t, 10> matches;
  auto match_ptrs = make_pointer_array<const RE2::Arg>(matches);
  auto arg_ptrs = make_pointer_array<const RE2::Arg* const>(match_ptrs);

  for (auto it = input.begin();;) {
    bool done{false};

    auto preserve = [&]() {
      linum++;
      output.emplace_back(*it);
      ++it;
      done = true;
    };
    auto kill = [&]() {
      auto old = it;
      ++it;
      done = true;
    };

    auto match = [&](auto&& re, matches_t& out_matches,
                     int offset = 0) -> bool {
      // NOLINTNEXTLINE(*-pointer-arithmetic)
      auto from = it->cbegin() + offset;
      match_t a(from, it->cend());
      if (RE2::FindAndConsumeN(
              &a, re, &arg_ptrs.at(1), re.NumberOfCapturingGroups())) {
        matches[0] = match_t(from, a.begin());
        out_matches =
            matches_t(matches.begin(), re.NumberOfCapturingGroups() + 1);
        return true;
      } else
        return false;
    };
    auto asm_linum = [&]() -> size_t { return linum; };

    if (it == input.end()) {
      LOG_TRACE("EOF");
      break;
    }
    if (!it->size()) {
      kill();
      continue;
    }
    fn(preserve, kill, match, it, asm_linum);
    if (!done) {
      if (o.preserve_directives)
        preserve();
      else
        kill();
    }
  }
}

// clang-format off
const RE2 r_label_start                {R"(^([^:]+): *(?:#|$)(?:.*))"};
const RE2 r_has_opcode                 {R"(^[[:space:]]+[A-Za-z]+[[:space:]]*)"};
const RE2 r_comment_only               {R"(^[[:space:]]*(?:[#;@]|//|/\*.*\*/).*$)"};
const RE2 r_label_reference            {R"(\.[A-Z_a-z][$.0-9A-Z_a-z]*)"};
const RE2 r_defines_global             {R"(^[[:space:]]*\.globa?l[[:space:]]*([.A-Z_a-z][$.0-9A-Z_a-z]*))"};
const RE2 r_defines_function_or_object {R"(^[[:space:]]*\.type[[:space:]]*(.*),[[:space:]]*[%@])"};
const RE2 r_file_directive             {R"(^[[:space:]]*\.file[[:space:]]+([[:digit:]]+)(?:[[:space:]]+\"([^\"]+)\")?[[:space:]]+\"([^\"]+)\"(?:[[:space:]]+md5[[:space:]]+(0x[[:xdigit:]]+))?.*)"};
const RE2 r_source_tag                 {R"(^[[:space:]]*\.loc[[:space:]]+([[:digit:]]+)[[:space:]]+([[:digit:]]+).*)"};
const RE2 r_source_stab                {R"(^.*\.stabn[[:space:]]+([[:digit:]]+),0,([[:digit:]]+),.*)"};
const RE2 r_endblock                   {R"(\.(?:cfi_endproc|data|section|text))"};
const RE2 r_data_defn                  {R"(^[[:space:]]*\.(string|asciz|ascii|[1248]?byte|short|word|long|quad|value|zero))"};
// clang-format on

struct file_info {
  std::set<size_t> tags;
  std::string_view directory;
  std::string_view filename;
  std::string_view md5;

  bool operator==(const file_info& other) const noexcept {
    if (!md5.empty()) return md5 == other.md5;
    if (filename == other.filename) return true;
    return false;  // TODO check with directory and basename
  }
};

struct parser_state {
  std::unordered_map<label_t, std::vector<label_t>> routines;
  std::unordered_set<label_t> globals{};
  std::optional<label_t> current_global{};
  // Base directory of the compilation (from the DWARF5 .file 0 entry).
  // Used to resolve relative .file paths to absolute ones for target matching.
  fs::path compile_dir{};
  // compiler info on file asked to annotate, or first .file in asm output
  std::optional<file_info> annotation_target_info{};
  std::unordered_set<label_t> target_file_routines{};
  std::unordered_set<label_t> used_labels{};

  // Internal linemap using map/set for efficient merging
  std::map<linum_t, std::set<std::pair<linum_t, linum_t>>> internal_linemap{};

  void register_mapping(linum_t source_linum, linum_t asm_linum) {
    auto [probe, inserted] =
        internal_linemap.insert({source_linum, {{asm_linum, asm_linum}}});
    if (!inserted) {
      auto& set = probe->second;
      auto y = set.begin();
      for (auto x = y++; x != set.end(); ++x, ++y) {
        if (asm_linum == x->first - 1) {
          set.emplace(asm_linum, x->second);
          set.erase(x);
        } else if (asm_linum == x->second + 1) {
          if (y != set.end() && y->first - 1 == asm_linum) {
            set.emplace(x->first, y->second);
            set.erase(x);
            set.erase(y);
          } else {
            set.emplace(x->first, asm_linum);
            set.erase(x);
          }
          return;
        }
      }
      set.emplace(asm_linum, asm_linum);
    }
  }

  // Convert internal linemap to public format
  linemap_t get_linemap() const {
    linemap_t result;
    for (const auto& [src_line, ranges] : internal_linemap) {
      for (const auto& [start, end] : ranges) {
        result.emplace_back(src_line, start, end);
      }
    }
    return result;
  }
};

void intermediate(parser_state& s, const annotation_options& o) {
  if (o.preserve_library_functions) {
    for (auto&& [label, callees] : s.routines) {
      s.used_labels.insert(label);
      for (auto&& callee : callees) {
        s.used_labels.insert(callee);
      }
    }
  } else {
    for (auto&& label : s.target_file_routines) {
      s.used_labels.insert(label);
      for (auto&& callee : s.routines[label]) {
        s.used_labels.insert(callee);
      }
    }
  }
}

auto first_pass(
    const auto& input, parser_state& s, const annotation_options& options,
    const std::optional<fs::path>& annotation_target) {
  auto a_target = annotation_target;  // copy
  using output_t =
      std::vector<typename std::decay_t<decltype(input)>::value_type>;
  output_t output;

  matches_t matches{};
  sweeping<output_t>(
      input, output, options,
      [&](auto preserve, auto kill, auto match_1, auto it, auto) {
        auto match = [&](auto&& re, int from = 0) {
          return match_1(re, matches, from);
        };
        if (it->at(0) != '\t') {
          if ((match(r_label_start))) {
            LOG_TRACE("FP1.1 '{}'", *it);
            if (s.globals.contains(matches[1])) {
              LOG_TRACE("FP1.1.1 '{}'", *it);
              s.current_global = matches[1];
            }
            LOG_TRACE("Preserve: FP1.1 '{}'", *it);
            preserve();
          } else {
            LOG_TRACE("Kill: FP1.1 '{}'", *it);
            kill();
          }
        } else {
          if (s.current_global && match(r_has_opcode)) {
            LOG_TRACE("FP2.1 '{}'", *it);
            auto offset = matches[0].size();
            s.routines[*s.current_global];
            while (match(r_label_reference, offset)) {
              LOG_TRACE("FP2.1.1 '{}'", *it);
              s.routines[*s.current_global].push_back(matches[0]);
              offset += matches[0].size();
            }
            LOG_TRACE("Preserve: FP2.1 '{}'", *it);
            preserve();
          } else if (!options.preserve_comments && match(r_comment_only)) {
            LOG_TRACE("Kill: FP2.2 '{}'", *it);
            kill();
          } else if (
              match(r_defines_global) || match(r_defines_function_or_object)) {
            LOG_TRACE("FP2.3 '{}'", *it);
            s.globals.insert(matches[1]);
          } else if (match(r_file_directive)) {
            LOG_TRACE("FP2.4 '{}'", *it);
            // Format: .file fileno [dirname] filename [md5 value]
            auto fileno = to_size_t(matches[1]);
            file_info info{
              .tags = {fileno},
              .directory = matches[2],
              .filename = matches[3] == "-" ? "<stdin>" : matches[3],
              .md5 = matches[4]};
            LOG_DEBUG(
                "FP2.4.1 added file {} -> {} dir={} md5={}", fileno,
                info.filename, info.directory, info.md5);

            // Presumably, .file 0 in DWARF5 format always carries the
            // compilation directory.
            if (fileno == 0) {
              s.compile_dir = fs::absolute(info.directory);
              if (!a_target) {
                a_target = s.compile_dir / info.filename;
              } else {
                a_target = fs::absolute(*a_target).lexically_normal();
              }
              LOG_DEBUG(
                  "FP2.4.1 compile_dir = {} a_target={}", s.compile_dir,
                  *a_target);
            }
            if (s.compile_dir.empty()) {
              utils::throwf<std::runtime_error>(
                  "Couldn't find compilation directory in asm directives.");
            }
            // Reconstruct full path of this .file entry and compare
            // against the requested (or guessed) annotation_target.
            // The reason for this complication is different ways to
            // report on files here.  Reconstructing the directory
            // needs to be done carefully.  For the same 'source.cpp'
            // file, different compilers emit different info.
            //
            // GCC:
            // .file "source.cpp"        # ignored, doesn't match here
            // .file 0 "/…/gcc-deep-hierarchy-2" "source.cpp"
            // .file 1 "header.hpp"
            // .file 2 "inner/header.hpp"
            // .file 3 "source.cpp"
            //
            // Clang:
            // .file "source.cpp"
            // .file 0 "/…/clang-deep-hierarchy-2" "source.cpp" md5 …
            // .file 1 "." "header.hpp" md5 …
            // .file 2 "./inner" "header.hpp" md5 …
            auto entry_path = [&]() -> fs::path {
              if (!info.directory.empty()) {
                auto d = fs::path{info.directory};
                if (!d.is_absolute()) d = s.compile_dir / d;
                return (d / fs::path{info.filename}).lexically_normal();
              }
              return (s.compile_dir / fs::path{info.filename})
                  .lexically_normal();
            }();
            //  In either situation above we want entry_path() to
            //  return:
            //
            //  0-> /path/to/clang-deep-hierarchy-2/source.cpp
            //  1-> /path/to/clang-deep-hierarchy-2/header.hpp
            //  2-> /path/to/clang-deep-hierarchy-2/inner/header.hpp
            //  3-> /path/to/clang-deep-hierarchy-2/source.cpp
            LOG_TRACE(
                "Trying entry_path='{}' against probe='{}'", entry_path,
                *a_target);
            if (entry_path == *a_target) {
              LOG_TRACE(
                  "FP2.4.1 Matched annotation_target='{}', tag={}", *a_target,
                  fileno);
              if (!s.annotation_target_info) {
                LOG_DEBUG(
                    "FP2.4.1 Initializing annotation_target_info for '{}'",
                    *a_target);
                s.annotation_target_info = info;
              }
              s.annotation_target_info->tags.insert(fileno);
            }
          } else if (match(r_source_tag)) {
            LOG_TRACE("FP2.5 '{}'", *it);
            if (s.current_global && s.annotation_target_info &&
                s.annotation_target_info->tags.contains(
                    to_size_t(matches[1]))) {
              LOG_TRACE("FP2.5.1 '{}'", *it);
              s.target_file_routines.insert(*s.current_global);
            }
            LOG_TRACE("Preserve: FP2.5 '{}'", *it);
            preserve();
          } else if (match(r_endblock)) {
            LOG_TRACE("FP2.6 '{}'", *it);
            s.current_global = std::nullopt;
            LOG_TRACE("Preserve: FP2.6 '{}'", *it);
            preserve();
          } else {
            LOG_TRACE("Preserve: FP2.7 '{}'", *it);
            preserve();
          }
        }
      });
  if (!s.annotation_target_info) {
    utils::throwf<std::runtime_error>(
        "At end of first pass, no annotation target info for '{}' (converted "
        "from '{}')",
        a_target.value_or("<empty>"), annotation_target.value_or("<empty>"));
  }
  return output;
}

annotation_result second_pass(
    const auto& input, parser_state& s, const annotation_options& options) {
  std::optional<label_t> reachable_label{};
  std::optional<size_t> source_linum{};

  using output_t = std::vector<std::string_view>;
  output_t output;

  std::vector<demangling_t> demanglings;

  sweeping(
      input, output, options,
      [&](auto preserve_1, auto kill, auto match_1, auto it, auto asm_linum) {
        matches_t matches{};

        auto match = [&](auto&& re) { return match_1(re, matches); };
        // The "preserve" of second pass contains the demangling
        // logic, and then calls the regular preserve_1().
        auto preserve = [&]() {
          // Collect demangling information if requested
          if (options.demangle) {
            std::string_view line = *it;
            std::string_view mangled;

            while (
                RE2::FindAndConsume(&line, R"((_Z[A-Za-z0-9_]+))", &mangled)) {
              std::string demangled = utils::demangle_symbol(mangled);

              // Only store if demangling actually changed the symbol
              if (demangled != mangled) {
                demanglings.emplace_back(mangled, std::move(demangled));
              }
            }
          }
          preserve_1();
        };

        if (it->at(0) != '\t') {
          if ((match(r_label_start))) {
            LOG_TRACE("SP1.1 '{}'", *it);
            label_t l = matches[1];
            if (s.used_labels.contains(l)) {
              reachable_label = l;
              LOG_TRACE("Preserve: SP1.1.1 '{}'", *it);
              preserve();
            } else if (options.preserve_unused_labels) {
              LOG_TRACE("Preserve: SP1.1.2 '{}'", *it);
              preserve();
            } else {
              LOG_TRACE("Kill: SP1.1.3 '{}'", *it);
              kill();
            }
          }
        } else {
          if (match(r_data_defn) && reachable_label) {
            LOG_TRACE("Preserve: SP2.1 '{}'", *it);
            preserve();
          } else if (match(r_has_opcode) && reachable_label) {
            if (source_linum) {
              s.register_mapping(*source_linum, asm_linum());
              LOG_TRACE("SP2.2.1 '{}'", *it);
            }
            LOG_TRACE("Preserve: SP2.2 '{}'", *it);
            preserve();
          } else if (match(r_source_tag)) {
            LOG_TRACE("SP2.3 '{}'", *it);
            source_linum = [&]() -> std::optional<int> {
              auto fileno = to_size_t(matches[1]);
              if (s.annotation_target_info &&
                  s.annotation_target_info->tags.contains(fileno)) {
                return to_size_t(matches[2]);
              } else {
                return std::nullopt;
              }
            }();
          } else if (match(r_source_stab)) {
            LOG_TRACE("SP2.4 '{}'", *it);
            // http://www.math.utah.edu/docs/info/stabs_11.html
            // 68     0x44     N_SLINE   line number in text segment
            // 100    0x64     N_SO      path and name of source file
            // 132    0x84     N_SOL     Name of sub-source (#include) file.
            auto a = to_size_t(matches[1]);
            switch (a) {
              case 68:
                source_linum = to_size_t(matches[2]);
                break;
              case 100:
              case 132:
                source_linum = std::nullopt;
                break;
              default: {
              }
            }
          } else if (match(r_endblock)) {
            LOG_TRACE("SP2.5 '{}'", *it);
            reachable_label = std::nullopt;
          }
        }
      });
  return {output, s.get_linemap(), demanglings};
}

std::vector<std::string> apply_demanglings(const annotation_result& result) {
  std::vector<std::string> output;
  output.reserve(result.output.size());

  auto demangling_it = result.demanglings.begin();

  for (const auto& line : result.output) {
    // Collect all demanglings that apply to this line
    std::vector<demangling_t> line_demanglings;

    while (demangling_it != result.demanglings.end()) {
      const auto& [mangled_sv, demangled] = *demangling_it;

      // Check if mangled_sv is within this line
      if (mangled_sv.data() >= line.data() &&
          // NOLINTNEXTLINE(*-pointer-arithmetic*)
          mangled_sv.data() + mangled_sv.size() <= line.data() + line.size()) {
        line_demanglings.emplace_back(mangled_sv, demangled);
        ++demangling_it;
      } else {
        // This demangling doesn't apply to current line, stop checking
        break;
      }
    }
    if (line_demanglings.empty()) {
      output.push_back(std::string{line});
    } else {
      // Apply demanglings in reverse order (right-to-left) to avoid position
      // shifts
      std::string demangled_line{line};
      for (auto it = line_demanglings.rbegin(); it != line_demanglings.rend();
           ++it) {
        const auto& [mangled_sv, demangled] = *it;

        // Find position of mangled symbol in current demangled_line
        size_t offset = mangled_sv.data() - line.data();

        // Replace the mangled symbol with demangled version
        demangled_line.replace(offset, mangled_sv.size(), demangled);
      }
      output.push_back(std::move(demangled_line));
    }
  }
  return output;
}

annotation_result annotate(
    std::span<const char> input, const annotation_options& aopts,
    const std::optional<fs::path>& annotation_target) {
  LOG_DEBUG(
      "-pd={}\n-pl={}\n-pc={}\n-pu={}\n-dm={}", aopts.preserve_directives,
      aopts.preserve_library_functions, aopts.preserve_comments,
      aopts.preserve_unused_labels, aopts.demangle);
  LOG_INFO("Annotating {} bytes of asm", input.size());

  xpto::linespan lspan{input};
  parser_state state{};

  auto fp_output = first_pass(lspan, state, aopts, annotation_target);
  intermediate(state, aopts);
  return second_pass(fp_output, state, aopts);
}

}  // namespace xpto::blot
