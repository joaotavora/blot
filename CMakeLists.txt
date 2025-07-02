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

find_package(Boost REQUIRED COMPONENTS process json unit_test_framework)
find_package(Fmt REQUIRED)
find_package(CLI11 REQUIRED)

# Core blot library
file(GLOB lib_sources CONFIGURE_DEPENDS src/blot/*.cpp)
list(REMOVE_ITEM lib_sources ${CMAKE_CURRENT_SOURCE_DIR}/src/blot/main.cpp)
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
add_executable(blot_exe src/blot/main.cpp)
target_link_libraries(blot_exe PRIVATE blot_lib)
set_target_properties(blot_exe PROPERTIES OUTPUT_NAME blot)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(blot_exe PRIVATE "-fsanitize=address,undefined")
  target_link_options(blot_exe PRIVATE "-fsanitize=address,undefined")
endif()

# Test fixture library - shows up in compile_commands.json
file(GLOB fixture_sources CONFIGURE_DEPENDS test/fixture/*.cpp)
add_library(fixture ${fixture_sources})

# Boost.Test runner
add_executable(test_blot test/test_blot.cpp)
target_link_libraries(test_blot PRIVATE
  blot_lib
  Boost::unit_test_framework
)

enable_testing()
add_test(NAME blot_tests COMMAND test_blot)
