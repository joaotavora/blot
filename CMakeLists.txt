cmake_minimum_required(VERSION 3.30)
project(blot LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)

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
find_package(Clang REQUIRED)

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
  libclang
)
target_compile_definitions(blot_lib PUBLIC LOG_NO_TIMESTAMP ON)

# Main executable
file(GLOB blot_sources CONFIGURE_DEPENDS src/blot/*.cpp)
add_executable(blot_exe ${blot_sources})
target_include_directories(blot_exe PRIVATE include)
target_link_libraries(blot_exe PRIVATE
  blot_lib
  CLI11::CLI11
)
set_target_properties(blot_exe PROPERTIES OUTPUT_NAME blot)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(blot_exe PRIVATE "-fsanitize=address,undefined")
  target_link_options(blot_exe PRIVATE "-fsanitize=address,undefined")
endif()

# Generate test configuration header
configure_file(test/test_config.h.in test/test_config.h)

# Doctest runner
add_executable(test_blot test/main.cpp test/annotation_tests.cpp test/infer_tests.cpp)
target_include_directories(test_blot PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/test)
target_link_libraries(test_blot PRIVATE
  blot_lib
  doctest::doctest
)

enable_testing()

# Use doctest's test discovery to register individual test cases
include(doctest)
doctest_discover_tests(test_blot)

# Add system tests for blot executable
include(test/system_tests.cmake)

# Utility targets
include(cmake/utils.cmake)
