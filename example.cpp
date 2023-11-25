#include <iostream>
#include <vector>
#include <unordered_map>
#include <random>
#include <thread>
#include "Channel.hpp"
using namespace Async;

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
};

int main() {
    using type = std::unordered_map<int, People>;
    auto [sp, rp] = Channel<People>::create();
    std::thread t([rp = std::move(rp)]{
        //block
        for (auto& res : *rp) {
            auto& people = *res;
            std::cout << " receive interval:" << (timestamp() - people.timestamp)
                << "ns, age:" << people.age << ", id:" << people.id << std::endl;
        }
//        for (;;) {
//            auto res = rp->tryReceive(); // No blocking
//            auto res = rp->receive(); // block
//            auto res = rp->tryReceiveAll(); //No blocking
//        }
    });
    //send single message
    sp->send(People{timestamp(), randomAge(), randomId()});
    sp->send(People{timestamp(), randomAge(), randomId()});
    sp << People{timestamp(), randomAge(), randomId()};
    sp << (People{timestamp(), randomAge(), randomId()});

    //send multi message
    std::vector<People> peoples;
    peoples.reserve(10);
    for(int i = 0; i < 4; i++) {
        peoples.emplace_back(timestamp(), randomAge(), randomId());
    }
    sp << peoples;

    peoples.clear();
    for(int i = 0; i < 10; i++) {
        peoples.emplace_back(timestamp(), randomAge(), randomId());
    }
    sp << (peoples | std::views::take(3)); // << higher priority than |
    peoples | std::views::drop(4) | SenderView(sp); // SenderView
    sp->done();
    t.join();
    return 0;
}
