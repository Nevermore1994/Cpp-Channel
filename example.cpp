#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>
#include <thread>
#include <algorithm>
#include "Channel.hpp"

using namespace Async;
using namespace std::string_literals;

int32_t randomCommon(int32_t min, int32_t max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int32_t> dis(min, max);
    return dis(gen);
}

auto randomId() -> int32_t {
    return randomCommon(0, INT32_MAX);
}

auto randomAge() -> int32_t {
    return randomCommon(0, 99);
}

auto timestamp() -> int64_t {
    using namespace std::chrono;
    return time_point_cast<nanoseconds>(system_clock::now()).time_since_epoch().count();
}

struct People {
    int64_t timestamp = 0;
    int age = 0;
    int id = 0;

    People(int64_t t,int age_, int id_)
        : timestamp(t)
        , age(age_)
        , id(id_) {

    }

    People() {

    }
};

int main() {
    using type = std::unordered_map<int, People>;
    using namespace std::chrono_literals;
    auto [sp, rp] = Channel<People>::create();
    std::thread t([rp = std::move(rp)] {
        //use for range
        People p;
        rp >> p;
        for (auto& people : *rp) {
            std::cout << " receive interval:" << (timestamp() - people.timestamp)
                << "ns, age:" << people.age << ", id:" << people.id << std::endl;
        }

        //use ranges
        for (const auto& people : *rp | std::views::filter([](const auto& ex) {
            return ex.id % 2 == 0;
        }) | std::views::transform([](auto& ex) {
            ex.id += 4;
            return ex;
        }) ) {
           std::cout << " receive interval:" << (timestamp() - people.timestamp) << "ns, age:" << people.age << ", id:" << people.id << std::endl;
        }

        //can use STL algorithm
        std::vector<People> values;
        std::for_each(rp->begin(), rp->end(), [](auto& people) {
            std::cout << " receive interval:" << (timestamp() - people.timestamp) << "ns, age:" << people.age << ", id:"
                      << people.id << std::endl;
        });
    });

    //send single message
    sp->send(People{timestamp(), randomAge(), randomId()});
    sp << People{timestamp(), randomAge(), randomId()} << (People{timestamp(), randomAge(), randomId()});
    std::this_thread::sleep_for(1s);
    //send multi message
    std::vector<People> peoples;
    peoples.reserve(10);
    for(int i = 0; i < 4; i++) {
        peoples.emplace_back(timestamp(), randomAge(), randomId());
    }
    sp << peoples;
    //emplace back
    sp->emplace(timestamp(), randomAge(), randomId());

    peoples.clear();
    for(int i = 0; i < 10; i++) {
        peoples.emplace_back(timestamp(), randomAge(), randomId());
    }
    std::this_thread::sleep_for(1s);
    //can use ranges
    sp << (peoples | std::views::take(3)); // << higher priority than |
    for (bool p : peoples | std::views::drop(4) | std::views::sender(sp)) {
        //p = true, send success
        std::cout << "send ranges:" << std::boolalpha << p << std::endl;
    }
    sp->done();
    t.join();
    return 0;
}
