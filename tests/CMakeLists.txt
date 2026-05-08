cmake_minimum_required(VERSION 3.20)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

project(chttp_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(unit_tests
    unit/test_http_parser.cpp
    unit/test_file_handler.cpp
    unit/test_response.cpp
)

target_include_directories(unit_tests PRIVATE ../include ../src)
target_link_libraries(unit_tests
    PRIVATE
    gtest_main
    chttp_lib
)

include(GoogleTest)
gtest_discover_tests(unit_tests)

if(BUILD_FUZZ)
    add_executable(fuzz_parser fuzz/fuzz_parser.c)
    set_target_properties(fuzz_parser PROPERTIES C_STANDARD 11)
    target_compile_options(fuzz_parser PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_parser PRIVATE -fsanitize=fuzzer)
    target_include_directories(fuzz_parser PRIVATE ../include ../src)
    target_link_libraries(fuzz_parser PRIVATE chttp_lib)
endif()
