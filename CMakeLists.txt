cmake_minimum_required(VERSION 3.30)
project(blot LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)

# Enable AddressSanitizer and UBSan in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

# Try modern CMake first (works with Conan), fallback to PkgConfig
find_package(re2 QUIET)
if(NOT re2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(re2 REQUIRED IMPORTED_TARGET re2)
    add_library(re2::re2 ALIAS PkgConfig::re2)
endif()
find_package(Boost REQUIRED COMPONENTS process json)
find_package(fmt REQUIRED)
find_package(CLI11 REQUIRED)
find_package(doctest REQUIRED)

# LLVM hasn't done its homework.  Need to find these two dynlibs
# explicitly.  Clang theoretically should pull in the other but
# because of some archlinux thing we have to pull them individually.
find_library(CLANG_CPP clang-cpp REQUIRED)
find_library(LLVM LLVM REQUIRED)

# Core blot library
file(GLOB lib_sources CONFIGURE_DEPENDS src/libblot/*.cpp)
add_library(blot_lib ${lib_sources})
target_include_directories(blot_lib PUBLIC include)
target_link_libraries(blot_lib PUBLIC
  re2::re2
  Boost::headers
  Boost::process
  Boost::json
  fmt::fmt
  ${CLANG_CPP}
  ${LLVM}
)
target_compile_definitions(blot_lib PUBLIC LOG_NO_TIMESTAMP)

# Configure web root path into a header so the server can find web/ at runtime
set(WEB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/web)
configure_file(
    src/blot/web_config.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/web_config.hpp
    @ONLY
)

# Main executable
file(GLOB blot_sources CONFIGURE_DEPENDS src/blot/*.cpp)
add_executable(blot_exe ${blot_sources})
target_include_directories(blot_exe PRIVATE include src/blot ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(blot_exe PRIVATE
  blot_lib
  CLI11::CLI11
  Boost::headers
)
set_target_properties(blot_exe PROPERTIES OUTPUT_NAME blot)

# Generate test configuration header
configure_file(test/api/test_config.h.in test/api/test_config.h)

# Doctest runner
add_executable(test_blot test/api/main.cpp test/api/annotation-tests.cpp test/api/infer-tests.cpp)
target_include_directories(test_blot PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/test/api)
target_link_libraries(test_blot PRIVATE
  blot_lib
  doctest::doctest
)

enable_testing()

# Use doctest's test discovery to register individual test cases
include(doctest)
doctest_discover_tests(test_blot)

# Add system tests for blot executable
include(test/cli/system-tests.cmake)

# Add JSONRPC server tests
include(test/jsonrpc/jsonrpc-tests.cmake)

# Add web API tests (HTTP + WebSocket)
include(test/web-api/web-api-tests.cmake)

# Spoof executable for filesystem interception using overlayfs
add_executable(spoof_exe src/spoof/main.cpp)
target_link_libraries(spoof_exe PRIVATE
  Boost::process
  fmt::fmt
)

# JSONRPC server executable
add_executable(blot_jsonrpc_server src/jsonrpc_server/main.cpp)
target_include_directories(blot_jsonrpc_server PRIVATE include)
target_link_libraries(blot_jsonrpc_server PRIVATE
  blot_lib
)

# Utility targets
include(cmake/utils.cmake)
