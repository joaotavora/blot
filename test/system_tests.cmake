# System tests for blot executable
# These test the CLI interface and executable behavior

# Test basic functionality with full JSON comparison
add_test(
    NAME system_basic_json
    COMMAND ${CMAKE_SOURCE_DIR}/test/blot_and_compare.sh
        $<TARGET_FILE:blot_exe>
        ${CMAKE_SOURCE_DIR}/test/fixture/compile_commands.json
        ${CMAKE_SOURCE_DIR}/test/fixture/basic.cpp
        ${CMAKE_SOURCE_DIR}/test/fixture/basic.json
)
