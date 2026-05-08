cmake_minimum_required(VERSION 3.20)
project(chttp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_FUZZ "Build fuzz tests" OFF)
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(ENABLE_ASAN ON)
endif()

set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic -Werror")
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -Wall -Wextra")

if(ENABLE_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,undefined")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
endif()

add_library(chttp_lib STATIC
    src/server.c
    src/http_parser.c
    src/http_response.c
    src/file_handler.c
    src/connection.c
    src/logger.c
)
target_include_directories(chttp_lib PUBLIC include)
target_include_directories(chttp_lib PRIVATE src)

add_executable(chttp src/main.c)
target_link_libraries(chttp PRIVATE chttp_lib)

install(TARGETS chttp DESTINATION bin)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
