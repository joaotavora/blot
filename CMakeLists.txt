cmake_minimum_required(VERSION 3.30)
project(blot LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)

find_package(PkgConfig REQUIRED)
pkg_check_modules(RE2 REQUIRED re2) # RE2 has no cmake files
if(RE2_FOUND)
  add_library(re2::re2 INTERFACE IMPORTED)
  set_target_properties(re2::re2 PROPERTIES
    INTERFACE_COMPILE_OPTIONS "${RE2_CFLAGS}"
    INTERFACE_LINK_LIBRARIES "${RE2_LDFLAGS}"
  )
endif()

find_package(Boost REQUIRED COMPONENTS process json)
find_package(Fmt REQUIRED)
find_package(CLI11 REQUIRED)
find_package(doctest REQUIRED)

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

# Test fixture library - shows up in compile_commands.json
file(GLOB fixture_sources CONFIGURE_DEPENDS test/fixture/*.cpp)
add_library(fixture ${fixture_sources})

# Generate test configuration header
configure_file(test/test_config.h.in test/test_config.h)

# Doctest runner
add_executable(test_blot test/test_blot.cpp)
target_include_directories(test_blot PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/test)
target_link_libraries(test_blot PRIVATE
  blot_lib
  doctest::doctest
)

enable_testing()

# Use doctest's test discovery to register individual test cases
include(doctest)
doctest_discover_tests(test_blot)
