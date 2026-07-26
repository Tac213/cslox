cmake_minimum_required(VERSION 3.19)
project(clox VERSION 0.1.0 LANGUAGES C)

# Debug options — default ON in Debug builds, OFF otherwise
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CLOX_DEBUG_DEFAULT ON)
else()
    set(CLOX_DEBUG_DEFAULT OFF)
endif()

option(CLOX_DEBUG_PRINT_CODE "Enable debug print code" ${CLOX_DEBUG_DEFAULT})
option(CLOX_DEBUG_TRACE_EXECUTION "Enable debug trace execution" ${CLOX_DEBUG_DEFAULT})
option(CLOX_DEBUG_STRESS_GC "Enable debug stress GC" OFF)
option(CLOX_DEBUG_LOG_GC "Enable debug log GC" ${CLOX_DEBUG_DEFAULT})

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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/object.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/object.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/table.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/clox/table.c"
)
add_executable(clox::lox ALIAS lox)

target_compile_definitions(lox
    PRIVATE
    $<$<BOOL:${CLOX_DEBUG_PRINT_CODE}>:DEBUG_PRINT_CODE>
    $<$<BOOL:${CLOX_DEBUG_TRACE_EXECUTION}>:DEBUG_TRACE_EXECUTION>
    $<$<BOOL:${CLOX_DEBUG_STRESS_GC}>:DEBUG_STRESS_GC>
    $<$<BOOL:${CLOX_DEBUG_LOG_GC}>:DEBUG_LOG_GC>
)
