# Cpp-Channel


# Channel

[![build](https://github.com/Nevermore1994/Cpp-Channel/workflows/build/badge.svg)](https://github.com/Nevermore1994/Cpp-Channel/actions)

### Thread-safe container for sharing data between threads. Header-only.

* Thread-safe push and fetch.
* Use stream operators to push (<<) items.
* Support Batch operations.
* Support Ranges
* No blocking or blocking fetch.
* Range-based for loop supported.
* If the sender or any receiver is released, the channel will be closed
* Integrates well with STL algorithms in some cases. Eg: std::move(rp->begin(), rp->end(), ...).
* Tested with GCC(GCC 13), Clang(LLVM 16), and MSVC(VS 2022).

## Requirements

* C++23 or newer

## Installation

You just need to include the header file

## Usage

* send single message
```c++
    auto [sp, rp] = Channel<int>::create();
    sp->send(0);
    sp << 1;
    sp << 2 << 3;
    
```

* batch operations
```c++
    auto [sp, rp] = Channel<int>::create();
    std::vector<int> nums {1, 2, 3, 4, 5, 6, 7};
    sp << nums; 
    // or sp->send(nums);
    
    //use ranges
    sp << (nums | std::views::take(3)); // << higher priority than |
    // or sp->send(nums | std::views::take(3));

    //use | operator
    for (bool res : nums | std::views::drop(4) | SenderView(sp)){
        //p = true, send success
        std::cout << "send result:" << std::boolalpha << res << std::endl;
    }
    
```

* receive message
```c++
    auto [sp, rp] = Channel<int>::create();
    //use for range, blocking
    for (auto& res : *rp) {
        std::cout << res << std::endl;
    }
    
    //use ranges, blocking
    auto func = [](auto& num) {
        return num % 2 == 0;
    };
    for (const auto& num : *rp | std::views::filter(func)) {
        std::cout << num << std::endl;
    }
    
    std::vector<int> values;
    //use STL algorithm, blocking
    std::move(rp->begin(), rp->end(), std::back_inserter(values));
    for(auto& res: values) {
        std::cout << res << std::endl;
    }
    
```

* NoBlocking receive message
```C++
    auto [sp, rp] = Channel<int>::create();
    auto res = rp->tryReceive(); // No blocking
    auto res = rp->tryReceiveAll(); //No blocking
```

For specific usage, please refer to [example.cpp](./example.cpp)

If you cannot support C++23, you can refer to [cpp-channel][def]

[def]: https://github.com/andreiavrammsd/cpp-channel