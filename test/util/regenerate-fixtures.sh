#!/bin/bash
set -e

# Regenerate test fixture files using their embedded metadata
#
# This script reads annotation options from existing fixture JSON files and
# regenerates them using the current compiler version. This is useful when:
# - The compiler version changes (e.g., GCC 15.1 -> 15.2)
# - The blot output format changes
# - You want to update all fixtures at once
#
# Usage: regenerate-fixtures.sh <blot_executable> <fixture_dir>
#   <blot_executable> - Path to the blot binary (e.g., build-Debug/blot)
#   <fixture_dir>     - Path to the fixture directory (e.g., test/fixture)
#
# The script can be run from any directory. Paths can be relative or absolute.
#
# Example (from project root):
#   test/util/regenerate-fixtures.sh build-Debug/blot test/fixture
#
# The script:
# 1. Reads each fxt-*.json file in the fixture directory
# 2. Extracts the source file, compile_commands.json, and annotation options
# 3. Runs blot with those options to regenerate the fixture
# 4. Removes directory-specific fields (cwd, compiler_invocation.directory)

if [ $# -ne 2 ]; then
  echo "Usage: $0 <blot_executable> <fixture_dir>"
  exit 1
fi

# Convert to absolute paths so we can change directories
BLOT=$(realpath "$1")
FIXTURE_DIR=$(realpath "$2")

# Change to fixture directory to match compile_commands.json expectations
cd "$FIXTURE_DIR"

for json_path in fxt-*.json; do
  echo "Processing $json_path..."

  # Read source file and compile_commands.json from JSON file_options
  # Use basename since we're in the fixture directory
  cpp_file=$(jq -r '.file_options.source_file' "$json_path" | xargs basename)
  ccj_file=$(jq -r '.file_options.compile_commands_path' "$json_path" | xargs basename)

  add_flag_maybe() {
    if jq -e ".annotation_options.${1} == true" "$json_path" >/dev/null; then
      flags+=("$2")
    fi
  }
  flags=()
  add_flag_maybe "demangle"                   "--demangle"
  add_flag_maybe "preserve_directives"        "--preserve-directives"
  add_flag_maybe "preserve_library_functions" "--preserve-library-functions"
  add_flag_maybe "preserve_comments"          "--preserve-comments"
  add_flag_maybe "preserve_unused_labels"     "--preserve-unused-labels"

  echo "Running: $BLOT --ccj=$ccj_file $cpp_file ${flags[*]} --json"
  "$BLOT" --ccj="$ccj_file" "$cpp_file" "${flags[@]}" --json \
    | jq 'del(.cwd) | del(.compiler_invocation.directory)'   \
    > "$json_path"
  echo "Regenerated $json_path"
done

echo "Fixture regeneration complete!"

# Local Variables:
# sh-basic-offset: 2
# End:
