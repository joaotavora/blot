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
# 1. Finds all subdirectories with expected.json
# 2. Reads annotation options from expected.json
# 3. Runs blot to regenerate the fixture's expected.json
# 4. Removes directory-specific fields (cwd, compiler_invocation.directory)

if [ $# -ne 2 ]; then
  echo "Usage: $0 <blot_executable> <fixture_dir>"
  exit 1
fi

# Convert to absolute paths
BLOT=$(realpath "$1")
FIXTURE_DIR=$(realpath "$2")

# Save original directory
ORIGINAL_DIR=$(pwd)

# Find all subdirectories with expected.json
for fixture_subdir in "$FIXTURE_DIR"/*/; do
  expected_json="${fixture_subdir}expected.json"

  # Skip if no expected.json
  if [ ! -f "$expected_json" ]; then
    continue
  fi

  fixture_name=$(basename "$fixture_subdir")
  echo "Processing $fixture_name..."

  # Change to the fixture subdirectory
  cd "$fixture_subdir"

  # Extract annotation options
  add_flag_maybe() {
    if jq -e ".annotation_options.${1} == true" expected.json >/dev/null; then
      flags+=("$2")
    fi
  }
  flags=()
  add_flag_maybe "demangle"                   "--demangle"
  add_flag_maybe "preserve_directives"        "--preserve-directives"
  add_flag_maybe "preserve_library_functions" "--preserve-library-functions"
  add_flag_maybe "preserve_comments"          "--preserve-comments"
  add_flag_maybe "preserve_unused_labels"     "--preserve-unused-labels"

  echo "Running: $BLOT --ccj=compile_commands.json source.cpp ${flags[*]} --json"
  "$BLOT" --ccj=compile_commands.json source.cpp "${flags[@]}" --json \
    | jq 'del(.cwd) | del(.compiler_invocation.directory)' \
    > expected.json
  echo "Regenerated $fixture_name/expected.json"

  # Return to original directory
  cd "$ORIGINAL_DIR"
done

echo "Fixture regeneration complete!"

# Local Variables:
# sh-basic-offset: 2
# End:
