#pragma once

/**
 * @file compile_command.hpp
 * @brief The @c compile_command type shared across the blot API.
 *
 * A @c compile_command is a normalised representation of one entry from a
 * @c compile_commands.json database.  It is produced by @c infer() and
 * consumed by @c get_asm().
 */

#include <filesystem>
#include <string>

namespace xpto::blot {

namespace fs = std::filesystem;

/** @brief Single entry from a compile commands database.
 *
 * @c directory is the working directory in which the compiler should be
 * invoked.  @c command is the full compiler command string exactly as stored
 * in the database.  @c file is the primary source file being compiled.
 *
 * When returned by @c infer(), all three fields are absolute paths.
 */
struct compile_command {
  fs::path directory;
  std::string command;
  fs::path file;
};

}  // namespace xpto::blot
