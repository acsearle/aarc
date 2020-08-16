//
//  queue.cpp
//  aarc
//
//  Created by Antony Searle on 12/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include <thread>

#include "queue.hpp"

#include "catch.hpp"

TEST_CASE("Queue") {

    atomic<queue<int>> a;
    
    auto N = 1'000;
    auto M = 16;
    
    std::vector<std::thread> t;
    std::mutex m;
    std::vector<int> x;
    std::vector<std::vector<int>> z;
    for (int i = 0; i != M; ++i) {
        t.emplace_back([&, i]() {
            for (int j = 0; j != N; ++j) {
                a.push(j + i * N);
            }
            std::vector<int> y;
            for (int j = 0; j != N; ++j) {
                int k = 0;
                if (a.try_pop(k))
                    y.emplace_back(k);
            }
            m.lock();
            x.insert(x.end(), y.begin(), y.end());
            z.emplace_back(std::move(y));
            m.unlock();
        });
    }
    for (auto&& s : t)
        s.join();
    
    // we popped as many as we pushed
    REQUIRE(x.size() == N * M);
    
    // when one thread pops the values pushed by another thread, their relative ordering is preserved
    for (auto& y : z) {
        std::stable_sort(y.begin(), y.end(), [=](int a, int b) {
            return (a / N) < (b / N); // coarse sort by pushing thread
        });
        // results in fine sort by value
        REQUIRE(std::is_sorted(y.begin(), y.end()));
    }
    
    // each pushed value was popped exactly once
    std::sort(x.begin(), x.end());
    x.erase(std::unique(x.begin(), x.end()), x.end());
    REQUIRE(x.size() == N * M);
    REQUIRE(x.front() == 0);
    REQUIRE(x.back() == N * M - 1);

    // only the final sentinel node and its predecessor (the stale _tail) remain alive
    //REQUIRE(Accountant::get() <= 2);
    
}

