# Cpp-Channel

[![build](https://github.com/Nevermore1994/Cpp-Channel/workflows/build/badge.svg)](https://github.com/Nevermore1994/Cpp-Channel/actions)

### Thread-safe container for sharing data between threads. Header-only.

* Thread-safe push and fetch.
* Use stream operators to push (<<) and fetch (>>) items.
* Support Batch operations.
* Support Ranges
* No blocking or blocking fetch.
* Range-based for loop supported.
* If either the sender or receiver is released, the channel will be closed.
* Integrates well with STL algorithms in some cases. Eg: std::move(rp->begin(), rp->end(), ...).
* Tested with GCC(gcc 13), Clang(LLVM 16 and XCode15.0), and MSVC(Visual Studio 2022).

## Requirements

* C++23 or newer

## Installation

You just need to include the header file [Channel.hpp](Channel.hpp)

## Usage

* Create
```c++
    auto [sp, rp] = Channel<int>::create(); //any movable type or support bit copy type
```

* Send
```c++
    sp->send(0);
    sp << 1;
    sp << 2 << 3;
    
    //batch operations
    std::vector<int> nums {1, 2, 3, 4, 5, 6, 7};
    sp << nums; 
    // or sp->send(nums);
    
    //use ranges
    sp << (nums | std::views::take(3)); 
    // or sp->send(nums | std::views::take(3));

    //use | operator
    for (bool res : nums | std::views::drop(4) | std::views::sender(sp)) {
        //if p = true, send success
        std::cout << "send result:" << std::boolalpha << res << std::endl;
    }
    
    //close
    sp->done(); //or sp.reset();
```

* Receive
```c++
    auto t = *(rp->receive());
    int t1, t2;
    rp >> t1 >> t2;
    //use for range, blocking
    for (const auto& res : *rp) {
        std::cout << res << std::endl;
    }
    
    //use ranges, blocking
    auto func = ;
    for (const auto& num : *rp | std::views::filter([](const auto& num) { 
            return num % 2 == 0; 
        })) {
        std::cout << num << std::endl;
    }
    
    //use STL algorithm, blocking
    std::vector<int> values;
    std::move(rp->begin(), rp->end(), std::back_inserter(values));
    
    //no blocking
    auto res = rp->tryReceive(); 
    //no blocking
    for (const auto& res : rp->tryReceiveAll()) {
        std::cout << res << std::endl;  
    }
    
    //can use STL or Ranges
    for (const auto& res : rp->tryReceiveAll() | std::transform([](const auto& num) {
            return num + 3;   
        })) {
        std::cout << res << std::endl;
    }
```

For specific usage, please refer to [example.cpp](./example.cpp)

If you cannot support C++23, you can refer to [cpp-channel][def]

[def]: https://github.com/andreiavrammsd/cpp-channel