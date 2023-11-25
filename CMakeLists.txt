cmake_minimum_required(VERSION 3.25)
project(Channel)

set(CMAKE_CXX_STANDARD 23)

option(build_test "build test" OFF)
option(build_benchmark "build benchmark" OFF)

if (build_test)
    message(STATUS "build test")
    enable_testing()
    include(tests/gtest.cmake)
    add_subdirectory(tests)
endif()

if (build_benchmark)
    message(STATUS "build benchmark")
    include(benchmark/benchmark.cmake)
    add_subdirectory(benchmark)
endif()

add_executable(Channel example.cpp Channel.hpp)
