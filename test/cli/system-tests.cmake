# System tests for blot executable
# These test the CLI interface and executable behavior

# Test basic functionality with full JSON comparison
add_test(
    NAME cli_gcc_basic
    COMMAND ${CMAKE_SOURCE_DIR}/test/util/blot-and-compare.sh
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-basic/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-basic/source.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-basic/expected.json
)

# Test basic functionality using stdin (assembly piped to blot)
add_test(
    NAME cli_gcc_stdin_basic
    COMMAND ${CMAKE_SOURCE_DIR}/test/util/blot-and-compare.sh
        --stdin
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-basic/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-basic/source.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-basic/expected.json
)

# Test CLI with preserve library functions flag
add_test(
    NAME cli_gcc_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/util/blot-and-compare.sh
        -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-preserve-library-functions/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-preserve-library-functions/source.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-preserve-library-functions/expected.json
)

# Test CLI with preserve library functions flag using stdin
add_test(
    NAME cli_gcc_stdin_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/util/blot-and-compare.sh
        --stdin -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-preserve-library-functions/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-preserve-library-functions/source.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-preserve-library-functions/expected.json
)

# Test CLI with clang++ compiler
add_test(
    NAME cli_clang_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/util/blot-and-compare.sh
        -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-preserve-library-functions/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-preserve-library-functions/source.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-preserve-library-functions/expected.json
)

# Test CLI with clang++ compiler using stdin
add_test(
    NAME cli_stdin_clang_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/util/blot-and-compare.sh
        --stdin -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-preserve-library-functions/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-preserve-library-functions/source.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-preserve-library-functions/expected.json
)

# Test CLI with clang++ demangle support
add_test(
    NAME cli_clang_demangle
    COMMAND ${CMAKE_SOURCE_DIR}/test/util/blot-and-compare.sh
        --demangle
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-demangle/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-demangle/source.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-demangle/expected.json
)

# Test CLI with clang++ demangle support using stdin
add_test(
    NAME cli_stdin_clang_demangle
    COMMAND ${CMAKE_SOURCE_DIR}/test/util/blot-and-compare.sh
        --stdin --demangle
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-demangle/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-demangle/source.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/clang-demangle/expected.json
)

# Test CLI error handling with JSON output
add_test(
    NAME cli_gcc_errors
    COMMAND ${CMAKE_SOURCE_DIR}/test/util/blot-and-compare.sh
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-errors/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-errors/source.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/gcc-errors/expected.json
)
