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

file(GLOB sources CONFIGURE_DEPENDS src/blot/*.cpp)
add_executable(blot_exe ${sources})
target_include_directories(blot_exe PRIVATE include)
target_link_libraries(blot_exe PRIVATE
  re2::re2
  Boost::headers
  Boost::process
  Boost::json
  fmt::fmt
)
set_target_properties(blot_exe PROPERTIES OUTPUT_NAME blot)
target_compile_definitions(blot_exe PRIVATE LOG_NO_TIMESTAMP ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(blot_exe PRIVATE "-fsanitize=address,undefined")
  target_link_options(blot_exe PRIVATE "-fsanitize=address,undefined")
endif()

# This 'silly' library is just here so these files show up in blot's
# `compile_commands.json' file and thus I can reference them directly
# in my tests.
file(GLOB fixture_sources CONFIGURE_DEPENDS test/*.cpp)
add_library(fixture ${fixture_sources})
