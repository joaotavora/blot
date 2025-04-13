cmake_minimum_required(VERSION 3.30)
project(bolt LANGUAGES CXX)
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

find_package(Boost REQUIRED COMPONENTS program_options process json)

file(GLOB sources CONFIGURE_DEPENDS src/bolt/*.cpp)
add_executable(bolt_exe ${sources})
target_include_directories(bolt_exe PRIVATE include)
target_link_libraries(bolt_exe PRIVATE
  re2::re2
  Boost::headers
  Boost::program_options
  Boost::process
  Boost::json
)
set_target_properties(bolt_exe PROPERTIES OUTPUT_NAME bolt)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(bolt_exe PRIVATE "-fsanitize=address,undefined")
  target_link_options(bolt_exe PRIVATE "-fsanitize=address,undefined")
endif()
