#!/bin/bash
set -e

# Simple script to regenerate fixture files by reading existing JSON metadata
# Usage: regenerate_fixtures.sh <blot_executable> <fixture_dir>

if [ $# -ne 2 ]; then
  echo "Usage: $0 <blot_executable> <fixture_dir>"
  exit 1
fi

BLOT="$1"
FIXTURE_DIR="$2"

for json_path in "$FIXTURE_DIR"/fxt_*.json; do
  json_file=$(basename "$json_path")
  
  # Read source file and compile_commands.json from JSON file_options
  cpp_path=$(jq -r '.file_options.source_file' "$json_path")
  ccj_path=$(jq -r '.file_options.compile_commands_path' "$json_path")

  echo "Processing $json_file..."

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

  echo "Running: $BLOT --ccj=$ccj_path $cpp_path ${flags[*]} --json"
  "$BLOT" --ccj="$ccj_path" "$cpp_path" "${flags[@]}" --json \
    | jq 'del(.cwd) | del(.compiler_invocation.directory)'   \
    > "$json_path"
  echo "Regenerated $json_file"
done

echo "Fixture regeneration complete!"

# Local Variables:
# sh-basic-offset: 2
# End:
