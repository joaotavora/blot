#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <print>
#include <source_location>
#include <string>
#include <string_view>

namespace xpto::logger {

enum class level : uint8_t { fatal, error, warning, info, debug, trace };

inline std::string_view level_to_string(level level) {
  // clang-format off
  switch (level) {
  case level::trace:   return "TRACE";
  case level::debug:   return "DEBUG";
  case level::info:    return "INFO";
  case level::warning: return "WARNING";
  case level::error:   return "ERROR";
  case level::fatal:   return "FATAL";
  default: return "UNKNOWN";
  }
  // clang-format on
}

inline level global_level = level::info;  // NOLINT
inline void set_level(level level) { global_level = level; }

inline std::string get_current_timestamp() {
  using namespace std::chrono;

  auto now = system_clock::now();
  auto ymd = year_month_day{floor<days>(now)};
  auto hms = hh_mm_ss{floor<milliseconds>(now - floor<days>(now))};

  return std::format("{} {}", ymd, hms);
}

// Core logging function
template <typename... Args>
inline void log(
    level level, const std::source_location& location,
    std::format_string<Args...> fmt, Args&&... args) {
  if (level > global_level) return;

  std::println(
      "{} {}:{} {}: {}", get_current_timestamp(),
      std::filesystem::path{location.file_name()}.filename().c_str(),
      location.line(), level_to_string(level),
      std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace xpto::logger

// NOLINTBEGIN
#define LOG_TRACE(...)                                             \
  xpto::logger::log(                                               \
      xpto::logger::level::trace, std::source_location::current(), \
      __VA_ARGS__)

#define LOG_DEBUG(...)                                             \
  xpto::logger::log(                                               \
      xpto::logger::level::debug, std::source_location::current(), \
      __VA_ARGS__)

#define LOG_INFO(...) \
  xpto::logger::log(  \
      xpto::logger::level::info, std::source_location::current(), __VA_ARGS__)

#define LOG_WARN(...)                                                \
  xpto::logger::log(                                                 \
      xpto::logger::level::warning, std::source_location::current(), \
      __VA_ARGS__)

#define LOG_ERROR(...)                                             \
  xpto::logger::log(                                               \
      xpto::logger::level::error, std::source_location::current(), \
      __VA_ARGS__)

#define LOG_FATAL(...)                                             \
  xpto::logger::log(                                               \
      xpto::logger::level::fatal, std::source_location::current(), \
      __VA_ARGS__)
// NOLINTEND
