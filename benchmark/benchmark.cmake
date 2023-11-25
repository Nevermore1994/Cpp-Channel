cmake_minimum_required(VERSION 3.25)

include(FetchContent)
FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/benchmark
        )

FetchContent_MakeAvailable(benchmark)
