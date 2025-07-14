#!/bin/bash
set -e

# Usage: blot_and_compare.sh <blot_executable> <compile_commands.json> <source_file> <expected_json>

# Compare JSON output using process substitution
diff -u <(jq --sort-keys . "$4") <("$1" --ccj "$2" "$3" --json | jq --sort-keys .)