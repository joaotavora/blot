cmake_minimum_required(VERSION 3.30)
project(blot LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)

find_program(CCACHE ccache)
if(CCACHE)
  set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
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
file(GLOB blot_lib_sources CONFIGURE_DEPENDS src/libblot/*.cpp)
add_library(blot_lib ${blot_lib_sources})
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

# Server library (http,websockets and stdio)
file(GLOB blot_server_lib_sources CONFIGURE_DEPENDS src/server/*.cpp)
add_library(blot_server_lib ${blot_server_lib_sources})
target_include_directories(blot_server_lib PUBLIC
  include src/server src/libblot ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(blot_server_lib PUBLIC
  blot_lib
  Boost::headers
)
set(WEB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/web)
configure_file(
    src/server/web-config.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/web-config.hpp
    @ONLY
)

# CLI executable
file(GLOB blot_cli_sources CONFIGURE_DEPENDS src/cli/*.cpp)
add_executable(blot_cli_exe ${blot_cli_sources})
target_include_directories(blot_cli_exe PRIVATE src/blot)
target_link_libraries(blot_cli_exe PRIVATE
  blot_lib
  blot_server_lib
  CLI11::CLI11
  Boost::headers
)
set_target_properties(blot_cli_exe PROPERTIES OUTPUT_NAME blot)

# Doctest tests
file(GLOB_RECURSE test_blot_files test/tests/*.cpp)
add_executable(test_blot ${test_blot_files})
target_include_directories(test_blot PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/test/api)
target_link_libraries(test_blot PRIVATE
  blot_lib
  blot_server_lib
  doctest::doctest
)
enable_testing()
include(doctest)
doctest_discover_tests(test_blot)
target_compile_definitions(test_blot PRIVATE
  TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/test/fixture"
)

# Non C++ tests system tests
include(test/tests/cli-tests.cmake)

# Proof-of concept "spoof" executable for filesystem interception
# using overlayfs.  To be integrated later
add_executable(spoof_exe src/spoof/main.cpp)
target_link_libraries(spoof_exe PRIVATE
  Boost::process
  fmt::fmt
)

# Utility targets
include(cmake/utils.cmake)
