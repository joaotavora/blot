cmake_minimum_required(VERSION 3.30)
project(bolt LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)

find_package(PkgConfig REQUIRED)
pkg_check_modules(RE2 REQUIRED re2) # RE2 has no cmake files :(
if(RE2_FOUND)
  add_library(re2::re2 INTERFACE IMPORTED)
  set_target_properties(re2::re2 PROPERTIES
    INTERFACE_COMPILE_OPTIONS "${RE2_CFLAGS}"
    INTERFACE_LINK_LIBRARIES "${RE2_LDFLAGS}"
    )
endif()

add_executable(bolt_exe main.cpp)
target_link_libraries(bolt_exe PRIVATE re2::re2)
set_target_properties(bolt_exe PROPERTIES OUTPUT_NAME bolt)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(bolt_exe PRIVATE "-fsanitize=address,undefined")
  target_link_options(bolt_exe PRIVATE "-fsanitize=address,undefined")
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  # target_compile_options(bolt_exe PRIVATE "-pg")
  # target_link_options(bolt_exe PRIVATE "-pg")
endif()
