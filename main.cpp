#include <iostream>
#include <istream>
#include <regex>
#include <unordered_map>

auto slurp(std::istream& is) {
  std::vector<std::string> v;
  std::string s{};
  while (true) {
    if (!std::getline(is, s)) break;
    v.push_back(std::move(s));
  }
  return v;
}

void sweeping(std::vector<std::string>& input, auto fn) {
  for (auto it = input.begin();;) {
    std::smatch matches;
    struct DONE {};

    try {
      auto preserve = [&]() {
        // std::cerr << "Preserving: " << *it << "\n";
      };
      auto kill = [&]() {
        // std::cerr << "Killing: " << *it << "\n";
        input.erase(it);
      };
      auto match = [&](auto&& re, const std::smatch** out_matches) -> bool {
        auto& l = *it++;
        if (it == input.end()) throw DONE{};
        if (std::regex_match(l, matches, re)) {
          *out_matches = &matches;
          return true;
        } else
          return false;
      };

      fn(preserve, kill, match, it);
    } catch (DONE& i) {
      break;
    }
  }
}

using input_t = std::vector<std::string>;
using line_t = std::string;
using label_t = std::string;

void first_pass(input_t& input) {
  // user options
  auto preserve_directives(false);
  auto preserve_comments(false);

  // regexps
  std::regex r_label_start{"^([^:]+): *(?:#|$)(?:.*)"};
  std::regex r_has_opcode{"^[[:space:]]+[A-Za-z]+"};
  std::regex r_comment_only{R"(^[[:space:]]*(?:[#;@]|//|/\*.*\*/).*$)"};
  std::regex r_label_reference{R"(\.[A-Z_a-z][$.0-9A-Z_a-z]*)"};

  // first pass
  std::unordered_map<line_t, label_t> routines;
  std::optional<label_t> current_routine{};

  sweeping(input, [&](auto preserve, auto kill, auto match, auto it) {
    const std::smatch* matches{};

    if ((*it)[0] != '\t') {
      if ((match(r_label_start, &matches))) {
        if (!routines.contains((*matches)[1])) current_routine = (*matches)[1];
        preserve();
      } else {
        kill();
      }
    } else {
      if (current_routine && match(r_has_opcode, &matches)) {
        while (match(r_label_reference, &matches)) {
        }

      } else if (!preserve_comments && match(r_comment_only, &matches)) {
        kill();
      } else if (false) {
      } else if (false) {
      } else if (false) {
      } else if (false) {
      } else {
        preserve();
      }
    }
  });
}

void second_pass(input_t& input) {}

int main() {
  auto input = slurp(std::cin);

  first_pass(input);
  second_pass(input);

  for (auto&& l : input) std::cout << l << "\n";
}
