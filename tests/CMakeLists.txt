cmake_minimum_required(VERSION 3.25)

include(GoogleTest)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(channel_test test.cpp)

target_link_libraries(channel_test
        gtest
        gtest_main
)

gtest_add_tests(TARGET channel_test)