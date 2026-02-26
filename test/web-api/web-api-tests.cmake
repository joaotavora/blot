# Web API tests (HTTP + WebSocket JSONRPC)
# Each test spawns a blot --web subprocess and makes HTTP/WS requests.
#
# Test name is derived from filename: test_ws_cache.py → web_api_ws_cache.
# Adding a new test_*.py file here is sufficient to register it.

set(_web_api_env
    "BLOT_EXE=$<TARGET_FILE:blot_exe>"
    "BLOT_FIXTURE_DIR=${CMAKE_SOURCE_DIR}/test/fixture"
)

file(GLOB _web_api_scripts
    LIST_DIRECTORIES false
    "${CMAKE_SOURCE_DIR}/test/web-api/test_*.py"
)

foreach(_script ${_web_api_scripts})
    get_filename_component(_stem ${_script} NAME_WE)
    string(REGEX REPLACE "^test_" "web_" _name ${_stem})
    add_test(NAME ${_name} COMMAND python3 ${_script})
    set_tests_properties(${_name} PROPERTIES ENVIRONMENT "${_web_api_env}")
endforeach()
