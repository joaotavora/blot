#pragma once

/**
 * @file ccj.hpp
 * @brief Utilities for locating and querying @c compile_commands.json
 * databases.
 *
 * A compile commands database records the exact compiler invocation
 * used to build every translation unit in a project.  This module
 * provides two entry points: @c find_ccj() for auto-discovery and @c
 * infer() for resolving which translation unit is responsible for a
 * given source or header file.
 *
 * @c infer() works by parsing each translation unit in the database
 * and walking its full inclusion tree.  Relative @c -I flags in the
 * stored compiler commands are resolved against the @c directory field
 * of each database entry, matching the behaviour of the original
 * compiler invocation.
 */

#include <filesystem>
#include <optional>

#include "blot/compile_command.hpp"

namespace xpto::blot {

namespace fs = std::filesystem;

/** @brief Find @c compile_commands.json in current working directory.
 *
 * Returns the absolute path to the file when one is found there, or
 * an empty optional otherwise.
 */
std::optional<fs::path> find_ccj();

/** @brief Find compile command covering @p source_file.
 *
 * This function is intended primarily for header files, which do not
 * appear directly in a compile commands database but are compiled as
 * part of some translation unit that includes them.  It also works
 * for source files that are listed directly in the database, in which
 * case a match is found without needing to examine inclusions.
 *
 * @p compile_commands_path may be absolute or relative to the current
 * working directory.  The @c directory and @c file fields of the
 * returned @c compile_command are always absolute, resolved using the
 * parent directory of @p compile_commands_path.
 *
 * @p source_file is matched against the absolute paths of files
 * included by each translation unit, both directly and transitively.
 * If a relative path, @p source_file is first resolved against the
 * current directory.
 *
 * Returns the @c compile_command for the first matching translation
 * unit, or an empty optional if no entry in the database includes @p
 * source_file.  Throws if the database cannot be read or parsed.
 */
std::optional<compile_command> infer(
    const fs::path& compile_commands_path, const fs::path& source_file);

}  // namespace xpto::blot
