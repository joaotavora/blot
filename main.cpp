#include "logger.hpp"
#include "linespan.hpp"

#include <re2/re2.h>

#include <array>
#include <exception>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <print>

using input_t = xpto::linespan;
using match_t = std::string_view;
using matches_t = std::span<match_t>;
using label_t = std::string_view;
using linum_t = size_t;

// user options
struct user_options {
  bool preserve_directives{false};
  bool preserve_comments{false};
  bool preserve_library_functions{false};
  bool preserve_unused_labels{false};
};

namespace utils {
template<typename Dest, typename T, size_t N, std::size_t... Is>
constexpr auto make_pointer_array_impl(std::array<T, N>& values, std::index_sequence<Is...>) {
  return std::array<Dest, N>{&values[Is]...};
}

template<typename Dest, typename T, size_t N>
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

} // namespace utils

template <typename Output, typename Input, typename F>
[[nodiscard]] Output sweeping(const Input& input, const user_options& o, F fn) {

  Output retval{};
  size_t linum{1};

  std::array<match_t, 10> matches;
  auto match_ptrs = utils::make_pointer_array<const RE2::Arg>(matches);
  auto arg_ptrs = utils::make_pointer_array<const RE2::Arg* const>(match_ptrs);

  for (auto it = input.begin();;) {
    bool done{false};

    auto preserve = [&]() {
      linum++;
      retval.push_back(*it);
      ++it;
      done = true;
    };
    auto kill = [&]() {
      auto old = it;
      ++it;
      done = true;
    };

    auto match = [&](auto&& re, matches_t& out_matches, int from = 0) -> bool {
      match_t a(it->cbegin() + from, it->cend());
      if (RE2::PartialMatchN(a, re, &arg_ptrs.at(1), re.NumberOfCapturingGroups())) {
        matches[0] = a;
        out_matches = matches_t(matches.begin(), re.NumberOfCapturingGroups());
        return true;
      } else
        return false;
    };
    auto asm_linum = [&]() -> size_t { return linum; };

    if (it == input.end()) {
      LOG_TRACE("EOF");
      break;
    }
    if (!it->size()) { kill(); continue; }
    fn(preserve, kill, match, it, asm_linum);
    if (!done) {
      if (o.preserve_directives) preserve(); else kill();
    }
  }
  return retval;
}

// clang-format off
const RE2 r_label_start                {R"(^([^:]+): *(?:#|$)(?:.*))"};
const RE2 r_has_opcode                 {R"(^[[:space:]]+[A-Za-z]+)"};
const RE2 r_comment_only               {R"(^[[:space:]]*(?:[#;@]|//|/\*.*\*/).*$)"};
const RE2 r_label_reference            {R"(\.[A-Z_a-z][$.0-9A-Z_a-z]*)"};
const RE2 r_defines_global             {R"(^[[:space:]]*\.globa?l[[:space:]]*([.A-Z_a-z][$.0-9A-Z_a-z]*))"};
const RE2 r_defines_function_or_object {R"(^[[:space:]]*\.type[[:space:]]*(.*),[[:space:]]*[%@])"};
const RE2 r_source_file_hint           {R"(^[[:space:]]*\.file[[:space:]]+([[:digit:]]+)[[:space:]]+\"([^\"]+)\"(?:[[:space:]]+\"([^\"]+)\")?.*)"};
const RE2 r_source_tag                 {R"(^[[:space:]]*\.loc[[:space:]]+([[:digit:]]+)[[:space:]]+([[:digit:]]+).*)"};
const RE2 r_source_stab                {R"(^.*\.stabn[[:space:]]+([[:digit:]]+),0,([[:digit:]]+),.*)"};
const RE2 r_endblock                   {R"(\.(?:cfi_endproc|data|section|text))"};
const RE2 r_data_defn                  {R"(^[[:space:]]*\.(string|asciz|ascii|[1248]?byte|short|word|long|quad|value|zero))"};
// clang-format on

struct parser_state {
  std::unordered_map<label_t, std::vector<label_t>> routines;
  std::optional<label_t> current_routine{};
  std::optional<label_t> main_file_tag{};
  std::unordered_set<label_t> main_file_routines{};
  std::unordered_set<label_t> used_labels{};

  std::unordered_map<linum_t, std::vector<linum_t>> line_mappings{};

  void register_mapping(linum_t source_linum, linum_t asm_linum) {
    line_mappings[source_linum].push_back(asm_linum);
  }
};

auto first_pass(
    const auto& input, const std::string& main_file_name, parser_state& s,
    const user_options& o) {

  using output_t = std::vector<typename std::decay_t<decltype(input)>::value_type>;

  matches_t matches{};
  return sweeping<output_t>(input, o, [&](auto preserve, auto kill, auto match_1, auto it, auto) {

    auto match = [&](auto&& re, int from = 0) { return match_1(re, matches, from); };
    if (it->at(0) != '\t') {
      if ((match(r_label_start))) {
        LOG_TRACE("FP1.1 '{}'", *it);
        if (s.routines.contains(matches[1])) {
          LOG_TRACE("FP1.1.1 '{}'", *it);
          s.current_routine = matches[1];
        }
        LOG_TRACE("Preserve: FP1.1 '{}'", *it);
        preserve();
      } else {
        LOG_TRACE("Kill: FP1.1 '{}'", *it);
        kill();
      }
    } else {
      if (s.current_routine && match(r_has_opcode)) {
        LOG_TRACE("FP2.1 '{}'", *it);
        auto offset = matches[0].size();
        while (match(r_label_reference, offset)) {
          LOG_TRACE("FP2.1.1 '{}'", *it);
          s.routines[*s.current_routine].push_back(matches[0]);
          offset += matches[0].size();
        }
        LOG_TRACE("Preserve: FP2.1 '{}'", *it);
        preserve();
      } else if (!o.preserve_comments && match(r_comment_only)) {
        LOG_TRACE("Kill: FP2.2 '{}'", *it);
        kill();
      } else if (
                 match(r_defines_global) || match(r_defines_function_or_object)) {
        LOG_TRACE("FP2.3 '{}'", *it);
        s.routines[matches[1]] = {};
      } else if (
          match(r_source_file_hint) &&
          main_file_name ==
            (matches[3].size() ? matches[3] : matches[2])) {
        LOG_TRACE("FP2.4 '{}'", *it);
        s.main_file_tag = matches[1];
      } else if (match(r_source_tag)) {
        LOG_TRACE("FP2.5 '{}'", *it);
        if (s.current_routine && s.main_file_tag &&
          matches[1] == s.main_file_tag) {
          LOG_TRACE("FP2.5.1 '{}'", *it);
          s.main_file_routines.insert(*s.current_routine);
        }
        LOG_TRACE("Preserve: FP2.5 '{}'", *it);
        preserve();
      } else if (match(r_endblock)) {
        LOG_TRACE("FP2.6 '{}'", *it);
        s.current_routine = std::nullopt;
        LOG_TRACE("Preserve: FP2.6 '{}'", *it);
        preserve();
      } else {
        LOG_TRACE("Preserve: FP2.7 '{}'", *it);
        preserve();
      }
    }
  });
}

void intermediate(parser_state& s, const user_options& o) {
  if (!s.main_file_tag)
    throw std::runtime_error("Cannot proceed without a 'main_file_tag'");
  if (o.preserve_library_functions) {
    for (auto&& [label, callees] : s.routines) {
      s.used_labels.insert(label);
      for (auto&& callee : callees) { s.used_labels.insert(callee); }
    }
  } else {
    for (auto&& label : s.main_file_routines) {
      s.used_labels.insert(label);
      for (auto&& callee : s.routines[label]) { s.used_labels.insert(callee); }
    }
  }
}

auto second_pass(const auto& input, parser_state& s, const user_options& o) {
  std::optional<label_t> reachable_label{};
  std::optional<size_t> source_linum{};

  using output_t = std::vector<typename std::decay_t<decltype(input)>::value_type>;

  return sweeping<output_t>(input, o, [&](auto preserve, auto kill, auto match_1, auto it, auto asm_linum) {

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
        } else if (o.preserve_unused_labels) {
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
          default: {}
          }
        }
      } else if (match(r_endblock)) {
        LOG_TRACE("SP2.5 '{}'", *it);
        reachable_label = std::nullopt;
      }
    }
  });
}

int main() {
  try {
  std::vector<char> buf{std::istreambuf_iterator<char>{std::cin},
                        std::istreambuf_iterator<char>()};
  // auto input = slurp(std::cin);
  auto input = xpto::linespan{buf};

  xpto::logger::set_level(xpto::logger::level::debug);

  parser_state state{};
  user_options options{};

  auto fp_output = first_pass(input, "<stdin>", state, options);
  intermediate(state, options);
  auto sp_output = second_pass(fp_output, state, options);

  for (auto&& l : sp_output) std::cout << l << "\n";

  } catch (std::exception& e) {
    LOG_FATAL("Whoops {}", e.what());
  }
}
