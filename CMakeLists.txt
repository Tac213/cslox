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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/vm.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/vm.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/compiler.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/compiler.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/scanner.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/scanner.c"
)
add_executable(clox::lox ALIAS lox)

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    target_compile_definitions(lox PRIVATE DEBUG_TRACE_EXECUTION)
endif()
