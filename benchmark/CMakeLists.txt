cmake_minimum_required(VERSION 3.25)

add_executable(channel_benchmark benchmark.cpp)
target_link_libraries(channel_benchmark benchmark::benchmark)
