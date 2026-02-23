#pragma once

/**
 * @file assembly.hpp
 * @brief Assembly generation from a compile command.
 *
 * This module takes a @c compile_command and produces the assembly output
 * that the compiler generates for it.  It does so by re-running the stored
 * compiler command with @c -c replaced by @c -S and @c -o @c - appended, so
 * that the assembly is written to stdout and captured.  A @c -g1 flag is
 * also added to ensure basic source-location directives are emitted.
 */

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "blot/compile_command.hpp"

namespace xpto::blot {

namespace fs = std::filesystem;

/** @brief Compiler, arguments, directory, and version for one build.
 *
 * @c compiler is the path or name of the compiler executable.  @c args
 * holds the complete argument list passed to it.  @c directory is the
 * working directory in which it was (or would be) invoked.
 * @c compiler_version is a human-readable version string extracted from
 * the compiler's @c --version output, or @c "<unknown>" if it could not
 * be determined.
 */
struct compiler_invocation {
  std::string compiler;
  std::vector<std::string> args;
  fs::path directory;
  std::string compiler_version;
};

/** @brief Thrown when compiler exits with non-zero status.
 *
 * @c invocation records exactly how the compiler was called.  @c dribble
 * holds the raw text the compiler wrote to stderr, which typically contains
 * the diagnostic messages explaining the failure.
 */
struct compilation_error : std::runtime_error {
  compilation_error(
      const std::string& desc, compiler_invocation i, std::string s)
      : std::runtime_error{desc},
        invocation{std::move(i)},
        dribble{std::move(s)} {}
  compiler_invocation invocation;
  std::string dribble;
};

/** @brief Assembly text and invocation from a successful compilation.
 *
 * @c assembly holds the raw assembly output as a string.  @c invocation
 * records the compiler and arguments that were used, which is useful for
 * display and diagnostic purposes.
 */
struct compilation_result {
  std::string assembly;
  compiler_invocation invocation;
};

/** @brief Compile source file to assembly.
 *
 * Runs the compiler described by @p cmd, replacing the @c -c flag with
 * @c -S and directing output to stdout.  The working directory is set to
 * @c cmd.directory.  Throws @c compilation_error if the compiler exits
 * with a non-zero status.
 */
compilation_result get_asm(const compile_command& cmd);

}  // namespace xpto::blot
