#include "blot/blot.hpp"
#include "linespan.hpp"
#include "logger.hpp"

#include <cxxabi.h>
#include <re2/re2.h>

#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>

namespace xpto::blot {

namespace detail {
using input_t = xpto::linespan;
using match_t = std::string_view;
using matches_t = std::span<match_t>;
using label_t = std::string_view;

namespace utils {
template <typename Dest, typename T, size_t N, std::size_t... Is>
constexpr auto make_pointer_array_impl(
    std::array<T, N>& values, std::index_sequence<Is...>) {
  return std::array<Dest, N>{&values[Is]...};
}

template <typename Dest, typename T, size_t N>
constexpr auto make_pointer_array(std::array<T, N>& values) {
  return make_pointer_array_impl<Dest>(values, std::make_index_sequence<N>{});
}

std::optional<size_t> to_size_t(std::string_view sv) {
  size_t result{};
  auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), result);
  if (ec == std::errc{} && ptr == sv.end())
    return result;
  else
    return std::nullopt;
}

// Demangle C++ symbols using __cxa_demangle
std::string demangle_symbol(std::string_view mangled) {
  int status = 0;
  std::string result{mangled};
  char* demangled =
      abi::__cxa_demangle(result.c_str(), nullptr, nullptr, &status);
  if (status == 0 && demangled) {
    result = demangled;
    std::free(demangled);  // NOLINT
  }
  return result;
}

}  // namespace utils

template <typename Output, typename Input, typename F>
void sweeping(
    const Input& input, Output& output, const annotation_options& o, F fn,
    std::vector<std::pair<std::string_view, std::string>>* demanglings = nullptr) {
  size_t linum{1};

  std::array<match_t, 10> matches;
  auto match_ptrs = utils::make_pointer_array<const RE2::Arg>(matches);
  auto arg_ptrs = utils::make_pointer_array<const RE2::Arg* const>(match_ptrs);

  for (auto it = input.begin();;) {
    bool done{false};

    auto preserve = [&]() {
      linum++;
      
      // Collect demangling information if requested
      if (o.demangle && demanglings) {
        std::string_view line = *it;
        re2::StringPiece input_piece(line.data(), line.size());
        re2::StringPiece match;
        
        // Find all mangled symbols in this line
        while (RE2::FindAndConsume(&input_piece, R"((_Z[A-Za-z0-9_]+))", &match)) {
          std::string_view mangled_sv(match.data(), match.size());
          std::string demangled = utils::demangle_symbol(mangled_sv);
          
          // Only store if demangling actually changed the symbol
          if (demangled != mangled_sv) {
            demanglings->emplace_back(mangled_sv, std::move(demangled));
          }
        }
      }
      
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
const RE2 r_main_file_name             {R"(^[[:space:]]*\.file[[:space:]]+\"([^\"]+)\"$)"};
const RE2 r_source_file_hint           {R"(^[[:space:]]*\.file[[:space:]]+([[:digit:]]+)[[:space:]]+\"([^\"]+)\"(?:[[:space:]]+\"([^\"]+)\")?.*)"};
const RE2 r_source_tag                 {R"(^[[:space:]]*\.loc[[:space:]]+([[:digit:]]+)[[:space:]]+([[:digit:]]+).*)"};
const RE2 r_source_stab                {R"(^.*\.stabn[[:space:]]+([[:digit:]]+),0,([[:digit:]]+),.*)"};
const RE2 r_endblock                   {R"(\.(?:cfi_endproc|data|section|text))"};
const RE2 r_data_defn                  {R"(^[[:space:]]*\.(string|asciz|ascii|[1248]?byte|short|word|long|quad|value|zero))"};
// clang-format on

struct parser_state {
  std::unordered_map<label_t, std::vector<label_t>> routines;
  std::unordered_set<label_t> globals{};
  std::optional<label_t> current_global{};
  std::optional<label_t> main_file_tag{};
  std::optional<std::string> main_file_name{};
  std::unordered_set<label_t> main_file_routines{};
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

namespace utils {}  // namespace utils

void intermediate(parser_state& s, const annotation_options& o) {
  if (!s.main_file_tag)
    throw std::runtime_error("Cannot proceed without a 'main_file_tag'");
  if (o.preserve_library_functions) {
    for (auto&& [label, callees] : s.routines) {
      s.used_labels.insert(label);
      for (auto&& callee : callees) {
        s.used_labels.insert(callee);
      }
    }
  } else {
    for (auto&& label : s.main_file_routines) {
      s.used_labels.insert(label);
      for (auto&& callee : s.routines[label]) {
        s.used_labels.insert(callee);
      }
    }
  }
}

auto first_pass(
    const auto& input, parser_state& s, const annotation_options& options) {
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
          } else if (match(r_source_file_hint)) {
            LOG_TRACE("FP2.4 '{}'", *it);
            // Horrible heuristic accounts for four cases
            //
            // cat test/test01.cpp | clang++ --std=c++23 -S -g -x c++ - -o -
            // cat test/test01.cpp | g++ --std=c++23 -S -g -x c++ - -o -
            // clang++ --std=c++23 -S -g test/test01.cpp -o -
            // gcc++ --std=c++23 -S -g test/test01.cpp -o -
            //
            // All of these produce slightly different '.file'
            // directives, and the following code tries to guess
            // accordingly.
            if (!s.main_file_name && matches[3].size()) {
              s.main_file_name = matches[3] == "-" ? "<stdin>" : matches[3];
              s.main_file_tag = matches[1];
              LOG_DEBUG(
                  "FP2.4.1 set main_file_name={} and main_file_tag={}",
                  *s.main_file_name, *s.main_file_tag);
            }
            if (s.main_file_name && !matches[3].size() &&
                *s.main_file_name == matches[2]) {
              LOG_DEBUG("FP2.4.2 updated main_file_tag={}", *s.main_file_tag);
              s.main_file_tag = matches[1];
            }
          } else if (match(r_source_tag)) {
            LOG_TRACE("FP2.5 '{}'", *it);
            if (s.current_global && s.main_file_tag &&
                matches[1] == s.main_file_tag) {
              LOG_TRACE("FP2.5.1 '{}'", *it);
              s.main_file_routines.insert(*s.current_global);
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
  return output;
}

annotation_result second_pass(
    const auto& input, parser_state& s, const annotation_options& options) {
  std::optional<label_t> reachable_label{};
  std::optional<size_t> source_linum{};

  using output_t = std::vector<std::string_view>;
  output_t output;
  
  std::vector<std::pair<std::string_view, std::string>> demanglings;

  sweeping(
      input, output, options,
      [&](auto preserve, auto kill, auto match_1, auto it, auto asm_linum) {
        matches_t matches{};
        auto match = [&](auto&& re) { return match_1(re, matches); };

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
              if (*s.main_file_tag == matches[1]) {
                return utils::to_size_t(matches[2]);
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
            auto a = utils::to_size_t(matches[1]);
            if (a) {
              switch (a.value()) {
                case 68:
                  source_linum = utils::to_size_t(matches[2]);
                  break;
                case 100:
                case 132:
                  source_linum = std::nullopt;
                  break;
                default: {
                }
              }
            }
          } else if (match(r_endblock)) {
            LOG_TRACE("SP2.5 '{}'", *it);
            reachable_label = std::nullopt;
          }
        }
      }, &demanglings);
  return {output, s.get_linemap(), demanglings};
}

}  // namespace detail


annotation_result annotate(
    std::span<const char> input, const annotation_options& options) {
  xpto::linespan lspan{input};
  detail::parser_state state{};

  auto fp_output = detail::first_pass(lspan, state, options);
  detail::intermediate(state, options);
  return detail::second_pass(fp_output, state, options);
}

}  // namespace xpto::blot
