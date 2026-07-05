cmake_minimum_required(VERSION 3.19)
project(clox VERSION 0.1.0 LANGUAGES C)

add_executable(lox
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/common.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/chunk.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/chunk.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/memory.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/memory.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/debug.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/debug.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/value.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/value.c"
)
add_executable(clox::lox ALIAS lox)
