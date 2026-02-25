#pragma once

// Mirrors the structure of the real blot utils::throwf: a function template in
// a header whose instantiation is a .weak COMDAT symbol.  We keep it as a
// simple arithmetic function so that GCC emits a plain .weak text label rather
// than routing the call through exception / unwind machinery.
template <typename Tag = void>
__attribute__((noinline)) int fixture_throwf(int x) {
  return x * 3 + 7;
}
