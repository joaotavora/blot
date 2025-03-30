#include <iostream>
#include <istream>
#include <optional>
#include <regex>
#include <unordered_map>
#include <unordered_set>

auto slurp(std::istream& is) {
  std::vector<std::string> v;
  std::string s{};
  while (true) {
    if (!std::getline(is, s)) break;
    v.push_back(std::move(s));
  }
  return v;
}

// user options
struct user_options {
  bool preserve_directives{false};
  bool preserve_comments{false};
  bool preserve_library_functions{false};
  bool preserve_unused_labels{false};
};

void sweeping(std::vector<std::string>& input, const user_options& o, auto fn) {
  for (auto it = input.begin();;) {
    std::smatch matches;
    struct eof {};
    bool done{false};
    size_t linum{1};
    

    try {
      auto preserve = [&]() {
        ++it;
        done = true;
        // std::cerr << "Preserving: " << *it << "\n";
      };
      auto kill = [&]() {
        auto old = it;
        ++it;
        done = true;
        // std::cerr << "Killing: " << *it << "\n";
        input.erase(old);
      };
      auto match = [&](auto&& re, const std::smatch** out_matches) -> bool {
        auto& l = *it;
        linum++;
        if (it == input.end()) throw eof{};
        if (std::regex_match(l, matches, re)) {
          if (out_matches) *out_matches = &matches;
          return true;
        } else
          return false;
      };
      auto asm_linum = [&]() -> size_t { return linum; };

      fn(preserve, kill, match, it, asm_linum);
      if (!done) {
        if (o.preserve_directives)
          preserve();
        else
          kill();
      }
    } catch (eof& i) {
      break;
    }
  }
}

using input_t = std::vector<std::string>;
using line_t = std::string;
using label_t = std::string;
using linum_t = size_t;

// clang-format off
const std::regex r_label_start                {R"(^([^:]+): *(?:#|$)(?:.*))"};
const std::regex r_has_opcode                 {R"(^[[:space:]]+[A-Za-z]+)"};
const std::regex r_comment_only               {R"(^[[:space:]]*(?:[#;@]|//|/\*.*\*/).*$)"};
const std::regex r_label_reference            {R"(\.[A-Z_a-z][$.0-9A-Z_a-z]*)"};
const std::regex r_defines_global             {R"(^[[:space:]]*\.globa?l[[:space:]]*([.A-Z_a-z][$.0-9A-Z_a-z]*))"};
const std::regex r_defines_function_or_object {R"(^[[:space:]]*\.type[[:space:]]*(.*),[[:space:]]*[%@])"};
const std::regex r_source_file_hint           {R"(^[[:space:]]*\.file[[:space:]]+([[:digit:]]+)[[:space:]]+\"([^\"]+)\"(?:[[:space:]]+\"([^\"]+)\")?.*)"};
const std::regex r_source_tag                 {R"(^[[:space:]]*\.loc[[:space:]]+([[:digit:]]+)[[:space:]]+([[:digit:]]+).*)"};
const std::regex r_source_stab                {R"(^.*\.stabn[[:space:]]+([[:digit:]]+),0,([[:digit:]]+),.*)"};
const std::regex r_endblock                   {R"(\.(?:cfi_endproc|data|section|text))"};
const std::regex r_data_defn                  {R"(^[[:space:]]*\.(string|asciz|ascii|[1248]?byte|short|word|long|quad|value|zero))"};
// clang-format on

struct parser_state {
  std::unordered_map<line_t, std::vector<label_t>> routines;
  std::optional<label_t> current_routine{};
  std::optional<label_t> main_file_tag{};
  std::vector<label_t> main_file_routines{};
  std::unordered_set<label_t> used_labels{};

  std::unordered_map<linum_t, std::vector<linum_t>> line_mappings{};

  void register_mapping(linum_t source_linum, linum_t asm_linum) {
    line_mappings[source_linum].push_back(asm_linum);
  }
};

void first_pass(
    input_t& input, const std::string& main_file_name, parser_state& s,
    const user_options& o) {


  sweeping(input, o, [&](auto preserve, auto kill, auto match_1, auto it, auto) {
    const std::smatch* matches{};

    auto match = [&](auto&& re) { return match_1(re, &matches); };

    if ((*it)[0] != '\t') {
      if ((match(r_label_start))) {
        if (!s.routines.contains(matches->str(1)))
          s.current_routine = matches->str(1);
        preserve();
      } else {
        kill();
      }
    } else {
      if (s.current_routine && match(r_has_opcode)) {
        while (match(r_label_reference))
          s.routines[*s.current_routine].push_back(matches->str(0));
        preserve();
      } else if (!o.preserve_comments && match(r_comment_only)) {
        kill();
      } else if (
          match(r_defines_global) || match(r_defines_function_or_object)) {
        s.routines[matches->str(1)] = {};
      } else if (
          match(r_source_file_hint) &&
          main_file_name ==
              (matches->length(3) ? matches->str(3) : matches->str(2))) {
        s.main_file_tag = matches->str(1);
      } else if (match(r_source_tag)) {
        if (s.current_routine && s.main_file_tag &&
            matches->str(1) == s.main_file_tag) {
          s.main_file_routines.push_back(*s.current_routine);
          preserve();
        }
      } else if (match(r_endblock)) {
        s.current_routine = std::nullopt;
        preserve();
      } else {
        preserve();
      }
    }
  });
}

void intermediate(parser_state& s, const user_options& o) {
  //highly fishy
  std::vector<label_t> interesting_routines;
  if (o.preserve_library_functions) {
    interesting_routines.reserve(s.routines.size());
    for (auto&& [label, callees] : s.routines)
      interesting_routines.push_back(label);
  } else {
    interesting_routines = s.main_file_routines;
  }
  for (auto&& label : interesting_routines) {
    s.used_labels.insert(label);

    for (auto&& callee : s.routines[label]) {
      s.used_labels.insert(callee);
    }
  }
}

void second_pass(input_t& input, parser_state& s, const user_options& o) {
  sweeping(input, o, [&](auto preserve, auto kill, auto match_1, auto it, auto asm_linum) {
    const std::smatch* matches{};

    auto match = [&](auto&& re) { return match_1(re, &matches); };

    std::optional<label_t> reachable_label{};
    std::optional<size_t> source_linum{};

    if ((*it)[0] != '\t') {
      if ((match(r_label_start))) {
        label_t l = matches->str(1);
        if (s.used_labels.contains(l)) {
          reachable_label = l;
          preserve();
        } else if (o.preserve_unused_labels) {
          preserve();
        } else {
          kill();
        }
      }
    } else {
      if (match(r_data_defn) && reachable_label) {
        preserve();
      } else if (match(r_has_opcode) && reachable_label) {
        if (source_linum) s.register_mapping(*source_linum, asm_linum());
        preserve();
      } else if (match(r_source_tag)) {
        source_linum = 42; //bogus
      } else if (match(r_source_stab)) {
        source_linum = std::nullopt; //bogus
      } else if (match(r_endblock)) {
        reachable_label = std::nullopt;
      }
    }
  });
}

int main() {
  auto input = slurp(std::cin);

  parser_state state{};
  user_options options{};

  first_pass(input, "<stdin>", state, options);
  intermediate(state, options);
  second_pass(input, state, options);

  for (auto&& l : input) std::cout << l << "\n";
}
