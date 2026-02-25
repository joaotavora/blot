#include <cstdio>

#include "header1.hpp"
#include "header2.hpp"
#include "utils.hpp"

// A source-defined function unrelated to header1.
// noinline so it remains a real call site in the assembly.
__attribute__((noinline)) int source_helper(int x) { return x * x + 1; }

// A source-defined function template (.weak COMDAT, like first_pass() in
// blot.cpp).  It inlines count_positives() from header1.hpp, so it lands in
// target_file_routines when annotating header1.hpp.  But it is DEFINED in
// source.cpp (first .loc = source.cpp), so its callees must NOT appear:
//   - fixture_throwf<>  (.weak template from utils.hpp) — mirrors
//   throwf/first_pass
//   - source_helper     (.globl)
//   - .LC* string-literal data labels from puts()
template <bool = false>
__attribute__((noinline)) int source_tmpl(const int* arr, int n) {
  int cnt = count_positives(arr, n);  // inlines from header1
  int t = fixture_throwf(cnt);        // .weak callee from utils.hpp
  if (t == 0) puts("zero count");     // .LC* string label
  return source_helper(t) + 1;        // .globl callee
}

// Uses both headers AND calls source_helper and source_tmpl.
// When annotating header1.hpp, source_helper should NOT appear in the output
// even though it is transitively reachable from these functions.
int process(const int* arr, int n) {
  int cnt = count_positives(arr, n);  // inlines from header1
  int d = double_it(cnt);             // inlines from header2
  int t = source_tmpl(arr, n);        // instantiates source_tmpl<false>
  return source_helper(d + t) + 1;    // local call (+1 prevents TCO)
}
