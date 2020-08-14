//
//  stack.cpp
//  aarc
//
//  Created by Antony Searle on 12/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include <vector>
#include <thread>

#include "stack.hpp"

#include "catch.hpp"

TEST_CASE("stack<fn<int>>", "[fn]") {

    {
        // mut
        stack<fn<int()>> a;
        a.push(fn<int()>([] { return 1; }));
        a.push(fn<int()>([] { return 2; }));
        REQUIRE(a.pop()() == 2);
        REQUIRE(a.pop()() == 1);
        REQUIRE_FALSE(a.pop());
    }
    
    {
        // const
        stack<fn<int()>> z;
        stack<fn<int()>> const& a = z;
        a.push(fn<int()>([] { return 1; }));
        a.push(fn<int()>([] { return 2; }));
        auto b = a.take();
        REQUIRE(b.pop()() == 2);
        REQUIRE(b.pop()() == 1);
        REQUIRE_FALSE(b.pop());
    }
    {
        stack<fn<int()>> a;
        for (int i = 0; i != 10; ++i) {
            a.push(fn<int()>([i] { return i; }));
        }
        int j = 9;
        for (auto& b : a) {
            REQUIRE(fn<int()>(b.try_clone())() == j--);
        }
        a.reverse();
        j = 0;
        for (auto& b : a) {
            REQUIRE(fn<int()>(b.try_clone())() == j++);
        }
        REQUIRE(a.size() == 10);
        {
            auto i = a.begin();
            REQUIRE(fn<int()>(i->try_clone())() == 0);
            ++i;
            REQUIRE(fn<int()>(i->try_clone())() == 1);
            auto f = a.erase(i);
            REQUIRE(f() == 1);
            REQUIRE(fn<int()>(i->try_clone())() == 2);
            REQUIRE(a.pop()() == 0);
            REQUIRE(a.pop()() == 2);
            i = a.begin();
            REQUIRE(fn<int()>(i->try_clone())() == 3);
            ++i;
            REQUIRE(fn<int()>(i->try_clone())() == 4);
            a.insert(i, fn<int()>([]{ return 1000; }));
            REQUIRE(fn<int()>(i->try_clone())() == 1000);
            REQUIRE(a.pop()() == 3);
            REQUIRE(a.pop()() == 1000);
            REQUIRE(a.pop()() == 4);
        }
    }
}


TEST_CASE("stack", "[stack]") {
    
    stack<int> a;
    
    auto N = 10'000;
    auto M = 16;
    
    std::vector<std::thread> t;
    std::mutex m;
    std::vector<int> x;
    for (int i = 0; i != M; ++i) {
        t.emplace_back([&, i]() {
            for (int j = 0; j != N; ++j) {
                a.push(j + i * N);
            }
            std::vector<int> y;
            for (int j = 0; j != N * M; ++j) {
                int k = 0;
                if (a.try_pop(k))
                    y.emplace_back(k);
            }
            m.lock();
            x.insert(x.end(), y.begin(), y.end());
            m.unlock();
        });
    }
    for (auto&& s : t)
        s.join();
    
    // we popped as many as we pushed
    REQUIRE(x.size() == N * M);
    
    // each pushed value was popped exactly once
    std::sort(x.begin(), x.end());
    x.erase(std::unique(x.begin(), x.end()), x.end());
    REQUIRE(x.size() == N * M);
    REQUIRE(x.front() == 0);
    REQUIRE(x.back() == N * M - 1);
    
    // all nodes were destroyed
    //REQUIRE(Accountant::get() == 0);
    
}
