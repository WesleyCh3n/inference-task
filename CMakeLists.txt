cmake_minimum_required(VERSION 3.10)

project(lirm-schema)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)

add_executable(main ${CMAKE_CURRENT_SOURCE_DIR}/main.cc)
