#!/bin/bash
set -e

# Usage: blot_and_compare.sh
#              [--stdin] [--demangle|-pd|-pc|-pu|-pl] <blot_executable>
#              <compile_commands.json> <source_file> <expected_json>

USE_STDIN=false
BLOT_FLAGS=()

# Parse flags
while [[ $# -gt 0 ]]; do
    case $1 in
        --stdin)
            USE_STDIN=true
            shift
            ;;
        --demangle|-pd|-pc|-pu|-pl)
            BLOT_FLAGS+=("$1")
            shift
            ;;
        *)
            break
            ;;
    esac
done

# Check we have the required arguments
if [ $# -ne 4 ]; then
    echo "Usage: $0 [--stdin] [--demangle|-pd|-pc|-pu|-pl] \\"
    echo "          <blot_executable> <compile_commands.json> \\"
    echo "          <source_file> <expected_json>"
    exit 1
fi

BLOT_EXECUTABLE="$1"
CCJ="$2"
SOURCE_FILE="$3"
EXPECTED_JSON="$4"

if [ "$USE_STDIN" = true ]; then
    # Find compile command for the source file
    FILENAME=$(basename "$SOURCE_FILE")
    COMPILE_CMD=$(jq -r \
        --arg file "$FILENAME"                                                 \
        '.[] | select(.file == $file) | .command'                              \
        "$CCJ")

    if [ -z "$COMPILE_CMD" ] || [ "$COMPILE_CMD" = "null" ]; then
        echo "ERROR: No compilation command found for $FILENAME in $CCJ"
        exit 1
    fi

    # Get the directory from compile_commands.json
    COMPILE_DIR=$(jq -r                                                        \
        --arg file "$FILENAME"                                                 \
        '.[] | select(.file == $file) | .directory'                            \
        "$CCJ")

    # Replace -c filename with -S -x c++ -, replace -g with -g1, remove -o flags
    ASM_CMD=$(echo "$COMPILE_CMD"                                              \
        | sed "s/-c $FILENAME/-S -x c++ -/g"                                   \
        | sed 's/-g[0-9]*/-g1/g'                                               \
        | sed 's/-o [^ ]*//g'                                                  \
        | sed 's/$/ -o -/')
    cd "$(dirname "$CCJ")/$COMPILE_DIR"
    # Now, run the double pipe.
    diff -u <(jq --sort-keys . "$EXPECTED_JSON")                               \
           <(cat "$FILENAME" | eval "$ASM_CMD"                                 \
                             | "$BLOT_EXECUTABLE" "${BLOT_FLAGS[@]}" --json    \
                             | jq --sort-keys .)

    # For posterity, the "single pipe" setup would look like this:
    #
    #  ASM_CMD=$(echo "$COMPILE_CMD"                                           \
    #      | sed 's/-c/-S/g'                                                   \
    #      | sed 's/-g[0-9]*/-g1/g'                                            \
    #      | sed 's/-o [^ ]*//g'                                               \
    #      | sed 's/$/ -o -/')
    #
    #  # Run the assembly generation and pipe to blot
    #  cd "$(dirname "$CCJ")/$COMPILE_DIR"
    #  diff -u <(jq --sort-keys . "$EXPECTED_JSON")                            \
    #         <(eval "$ASM_CMD" | "$BLOT_EXECUTABLE" "${BLOT_FLAGS[@]}" --json \
    #                           | jq --sort-keys .)
else
    # Normal behavior: use --ccj flag
    diff -u <(jq --sort-keys . "$EXPECTED_JSON")                               \
            <("$BLOT_EXECUTABLE" "${BLOT_FLAGS[@]}" --ccj "$CCJ" "$SOURCE_FILE" \
                --json | jq --sort-keys .)
fi
