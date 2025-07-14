# System tests for blot executable
# These test the CLI interface and executable behavior

# Test basic functionality using --ccj flag
add_test(
    NAME system_basic
    COMMAND $<TARGET_FILE:blot_exe> --ccj ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json ${CMAKE_SOURCE_DIR}/test/fixture/basic.cpp
)

# Set the expected output for comparison
set_tests_properties(system_basic PROPERTIES
    PASS_REGULAR_EXPRESSION "_Z3foov:.*main:"
)
