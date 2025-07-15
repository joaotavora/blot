#pragma once

#include <fmt/format.h>
#include <stdexcept>
#include <cxxabi.h>

namespace xpto::blot::utils {
template <typename Exception=std::runtime_error, typename... Args>
[[noreturn]] void throwf(
    fmt::format_string<Args...> format_str, Args&&... args) {
  throw Exception(fmt::format(format_str, std::forward<Args>(args)...));
}

// Demangle C++ symbols using __cxa_demangle
inline std::string demangle_symbol(std::string_view mangled) {
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

}
