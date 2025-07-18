# System tests for blot executable
# These test the CLI interface and executable behavior

# Test basic functionality with full JSON comparison
add_test(
    NAME cli_gcc_basic
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot-and-compare.sh
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-gcc-basic.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-gcc-basic.json
)

# Test basic functionality using stdin (assembly piped to blot)
add_test(
    NAME cli_gcc_stdin_basic
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot-and-compare.sh
        --stdin
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-gcc-basic.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-gcc-basic.json
)

# Test CLI with preserve library functions flag
add_test(
    NAME cli_gcc_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot-and-compare.sh
        -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-gcc-preserve-library-functions.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-gcc-preserve-library-functions.json
)

# Test CLI with preserve library functions flag using stdin
add_test(
    NAME cli_gcc_stdin_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot-and-compare.sh
        --stdin -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-gcc-preserve-library-functions.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-gcc-preserve-library-functions.json
)

# Test CLI with clang++ compiler
add_test(
    NAME cli_clang_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot-and-compare.sh
        -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-clang-preserve-library-functions.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-clang-preserve-library-functions.json
)

# Test CLI with clang++ compiler using stdin
add_test(
    NAME cli_stdin_clang_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot-and-compare.sh
        --stdin -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-clang-preserve-library-functions.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-clang-preserve-library-functions.json
)

# Test CLI with clang++ demangle support
add_test(
    NAME cli_clang_demangle
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot-and-compare.sh
        --demangle
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-clang-demangle.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-clang-demangle.json
)

# Test CLI with clang++ demangle support using stdin
add_test(
    NAME cli_stdin_clang_demangle
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot-and-compare.sh
        --stdin --demangle
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-clang-demangle.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-clang-demangle.json
)

# Test CLI error handling with JSON output
add_test(
    NAME cli_gcc_errors
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot-and-compare.sh
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-gcc-errors.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt-gcc-errors.json
)
