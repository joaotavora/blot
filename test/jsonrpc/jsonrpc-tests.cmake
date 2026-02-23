# JSONRPC server tests
# These test the JSONRPC server interface
# The Python tests spawn the server as a subprocess

# Test basic lifecycle (initialize, shutdown, exit)
add_test(
    NAME jsonrpc_basic
    COMMAND python3 ${CMAKE_SOURCE_DIR}/test/jsonrpc/test_basic.py
)
set_tests_properties(jsonrpc_basic PROPERTIES
    ENVIRONMENT "BLOT_JSONRPC_SERVER=$<TARGET_FILE:blot_jsonrpc_server>"
)

# Test annotate method
add_test(
    NAME jsonrpc_annotate
    COMMAND python3 ${CMAKE_SOURCE_DIR}/test/jsonrpc/test_annotate.py
)
set_tests_properties(jsonrpc_annotate PROPERTIES
    ENVIRONMENT "BLOT_JSONRPC_SERVER=$<TARGET_FILE:blot_jsonrpc_server>"
)

# Test error handling
add_test(
    NAME jsonrpc_errors
    COMMAND python3 ${CMAKE_SOURCE_DIR}/test/jsonrpc/test_errors.py
)
set_tests_properties(jsonrpc_errors PROPERTIES
    ENVIRONMENT "BLOT_JSONRPC_SERVER=$<TARGET_FILE:blot_jsonrpc_server>"
)
