#pragma once

#include <fmt/base.h>
#include <fmt/chrono.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
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

inline level global_level = level::fatal;  // NOLINT
inline void set_level(level level) { global_level = level; }

inline std::string get_current_timestamp() {
  auto now = std::chrono::floor<std::chrono::milliseconds>(
      std::chrono::system_clock::now());
  return fmt::format("{:%Y-%m-%d %H:%M:%S}", now);
}

// Core logging function
template <typename... Args>
inline void log(
    level level, const std::source_location& location,
    fmt::format_string<Args...> fmt, Args&&... args) {
  if (level > global_level) return;

  fmt::println(
      stderr,
#ifndef LOG_NO_TIMESTAMP
      "{} "
#endif
      "{}:{} {}: {}",
#ifndef LOG_NO_TIMESTAMP
      get_current_timestamp(),
#endif
      std::filesystem::path{location.file_name()}.filename().c_str(),
      location.line(), level_to_string(level),
      fmt::format(fmt, std::forward<Args>(args)...));
}

}  // namespace xpto::logger

// NOLINTBEGIN
// BLOT_LOG_MAX_LEVEL controls compile-time logging ceiling (0=fatal..5=trace).
// If not defined, all log macros are no-ops.
#ifndef BLOT_LOG_MAX_LEVEL
#define BLOT_LOG_MAX_LEVEL 5
#endif

#if BLOT_LOG_MAX_LEVEL >= 5
#  define LOG_TRACE(...)                                             \
     xpto::logger::log(                                               \
         xpto::logger::level::trace, std::source_location::current(), \
         __VA_ARGS__)
#else
#  define LOG_TRACE(...) ((void)0)
#endif

#if BLOT_LOG_MAX_LEVEL >= 4
#  define LOG_DEBUG(...)                                             \
     xpto::logger::log(                                               \
         xpto::logger::level::debug, std::source_location::current(), \
         __VA_ARGS__)
#else
#  define LOG_DEBUG(...) ((void)0)
#endif

#if BLOT_LOG_MAX_LEVEL >= 3
#  define LOG_INFO(...)  \
     xpto::logger::log(  \
         xpto::logger::level::info, std::source_location::current(), __VA_ARGS__)
#else
#  define LOG_INFO(...) ((void)0)
#endif

#if BLOT_LOG_MAX_LEVEL >= 2
#  define LOG_WARN(...)                                                \
     xpto::logger::log(                                                 \
         xpto::logger::level::warning, std::source_location::current(), \
         __VA_ARGS__)
#else
#  define LOG_WARN(...) ((void)0)
#endif

#if BLOT_LOG_MAX_LEVEL >= 1
#  define LOG_ERROR(...)                                             \
     xpto::logger::log(                                               \
         xpto::logger::level::error, std::source_location::current(), \
         __VA_ARGS__)
#else
#  define LOG_ERROR(...) ((void)0)
#endif

#if BLOT_LOG_MAX_LEVEL >= 0
#  define LOG_FATAL(...)                                             \
     xpto::logger::log(                                               \
         xpto::logger::level::fatal, std::source_location::current(), \
         __VA_ARGS__)
#else
#  define LOG_FATAL(...) ((void)0)
#endif
// NOLINTEND
