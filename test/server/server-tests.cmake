# Server tests (HTTP + JSONRPC session tests).
#
# session_*.py tests are transport-agnostic JSONRPC session tests.  They are
# registered twice: once with BLOT_TRANSPORT=ws (web_ prefix) and once with
# BLOT_TRANSPORT=stdio (stdio_ prefix).
#
# http_*.py tests cover HTTP REST endpoints and are registered once
# (ws transport only, since HTTP is only available for --web).
#
# Adding a new .py file here is sufficient to register it.

set(_blot_env_base
    "BLOT_EXE=$<TARGET_FILE:blot_exe>"
    "BLOT_FIXTURE_DIR=${CMAKE_SOURCE_DIR}/test/fixture"
)

# Session tests (ws + stdio)

file(GLOB _session_scripts
    LIST_DIRECTORIES false
    "${CMAKE_SOURCE_DIR}/test/server/session_*.py"
)

foreach(_script ${_session_scripts})
    get_filename_component(_stem ${_script} NAME_WE)
    string(REGEX REPLACE "^session_" "server_ws_" _ws_name ${_stem})
    string(REGEX REPLACE "^session_" "server_stdio_" _stdio_name ${_stem})

    add_test(NAME ${_ws_name} COMMAND python3 ${_script})
    set_tests_properties(${_ws_name} PROPERTIES
        ENVIRONMENT "${_blot_env_base};BLOT_TRANSPORT=ws"
    )

    add_test(NAME ${_stdio_name} COMMAND python3 ${_script})
    set_tests_properties(${_stdio_name} PROPERTIES
        ENVIRONMENT "${_blot_env_base};BLOT_TRANSPORT=stdio"
    )
endforeach()

# HTTP-only tests

file(GLOB _http_scripts
    LIST_DIRECTORIES false
    "${CMAKE_SOURCE_DIR}/test/server/http_*.py"
)

foreach(_script ${_http_scripts})
    get_filename_component(_stem ${_script} NAME_WE)
    string(REGEX REPLACE "^http_" "server_http_" _name ${_stem})

    add_test(NAME ${_name} COMMAND python3 ${_script})
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "${_blot_env_base};BLOT_TRANSPORT=ws"
    )
endforeach()
