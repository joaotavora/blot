#pragma once

// Arthur O'Dwyer's "AtScopeExit" explained in
// https://www.youtube.com/watch?v=lKG1m2NkANM

// NOLINTBEGIN(*macro-usage*)

namespace xpto::utils {
template <typename Lam>
class AtScopeExit {
  Lam m_lam;
public:
 AtScopeExit(const AtScopeExit &) = delete;
 AtScopeExit(AtScopeExit &&) = delete;
 AtScopeExit &operator=(const AtScopeExit &) = delete;
 AtScopeExit &operator=(AtScopeExit &&) = delete;
 AtScopeExit(Lam action) : m_lam(static_cast<Lam&&>(action)) {}
 ~AtScopeExit() { m_lam(); }
};
}

#define TOKEN_PASTEx(x,y) x ## y
#define TOKEN_PASTE(x, y) TOKEN_PASTEx(x, y)

#define AUTO_INTERNAL(lname, aname, ...)                   \
  auto lname = [&]() { __VA_ARGS__; };                     \
  xpto::utils::AtScopeExit aname(lname)

#define AUTO_INTERNAL2(ctr, ...)                           \
AUTO_INTERNAL(TOKEN_PASTE(AUTO_func, ctr),                 \
              TOKEN_PASTE(AUTO_instance, ctr), __VA_ARGS__)

#define AUTO(...) AUTO_INTERNAL2(__COUNTER__, __VA_ARGS__)
// NOLINTEND(*macro-usage*)
