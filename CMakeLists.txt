cmake_minimum_required(VERSION 3.30)
project(bolt LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)

add_executable(bolt_exe main.cpp)
set_target_properties(bolt_exe PROPERTIES OUTPUT_NAME bolt)
target_compile_options(bolt_exe PRIVATE "-fsanitize=address,undefined")
target_link_options(bolt_exe PRIVATE "-fsanitize=address,undefined")
