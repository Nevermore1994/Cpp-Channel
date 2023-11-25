//
// Created by Nevermore on 2023/11/24.
// Channel benchmark
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include <benchmark/benchmark.h>
#include "../Channel.hpp"

static void ChannelWithInt(benchmark::State& state)
{
    auto [sp, rp] = Async::Channel<int>::create();
    int in = 1;
    for (auto _ : state) {
        sp << in;
        auto res = rp->receive();
    }
}

BENCHMARK(ChannelWithInt);

static void ChannelWithString(benchmark::State& state)
{
    auto [sp, rp] = Async::Channel<std::string>::create();
    std::string in = "1";
    for (auto _ : state) {
        sp << in;
        auto res = rp->receive();
    }
}

BENCHMARK(ChannelWithString);

struct TestValue {
    int value = 0;
    std::string key = "key";
};

static void ChannelWithStruct(benchmark::State& state)
{
    auto [sp, rp] = Async::Channel<TestValue>::create();
    TestValue in;
    for (auto _ : state) {
        sp << in;
        auto res = rp->receive();
    }
}

BENCHMARK(ChannelWithStruct);

BENCHMARK_MAIN();
