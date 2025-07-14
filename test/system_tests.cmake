# System tests for blot executable
# These test the CLI interface and executable behavior

# Test basic functionality with full JSON comparison
add_test(
    NAME cli_gcc_basic
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot_and_compare.sh
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_gcc_basic.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_gcc_basic.json
)

# Test basic functionality using stdin (assembly piped to blot)
add_test(
    NAME cli_gcc_stdin_basic
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot_and_compare.sh
        --stdin
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_gcc_basic.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_gcc_basic.json
)

# Test CLI with preserve library functions flag
add_test(
    NAME cli_gcc_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot_and_compare.sh
        -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_gcc_preserve_library_functions.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_gcc_preserve_library_functions.json
)

# Test CLI with preserve library functions flag using stdin
add_test(
    NAME cli_gcc_stdin_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot_and_compare.sh
        --stdin -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_gcc_preserve_library_functions.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_gcc_preserve_library_functions.json
)

# Test CLI with clang++ compiler
add_test(
    NAME cli_clang_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot_and_compare.sh
        -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_clang_preserve_library_functions.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_clang_preserve_library_functions.json
)

# Test CLI with clang++ compiler using stdin
add_test(
    NAME cli_stdin_clang_preserve_library_functions
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot_and_compare.sh
        --stdin -pl
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_clang_preserve_library_functions.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_clang_preserve_library_functions.json
)

# Test CLI with clang++ demangle support
add_test(
    NAME cli_clang_demangle
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot_and_compare.sh
        --demangle
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_clang_demangle.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_clang_demangle.json
)

# Test CLI with clang++ demangle support using stdin
add_test(
    NAME cli_stdin_clang_demangle
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot_and_compare.sh
        --stdin --demangle
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_clang_demangle.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/fxt_clang_demangle.json
)
