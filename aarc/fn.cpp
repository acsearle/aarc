//
//  fn.cpp
//  aarc
//
//  Created by Antony Searle on 12/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include "fn.hpp"

#include "catch.hpp"

TEST_CASE("fn", "[fn]") {

    {
        fn<void> a;
        REQUIRE_FALSE(a);
    }
    {
        bool b = false;
        auto a = fn<bool>::from([&] {
            return b = true;
        });
        REQUIRE(a);
        REQUIRE_FALSE(b);
        REQUIRE(a());
        REQUIRE_FALSE(a);
        REQUIRE(b);
    }
    
    {
        int i = 0;
        int j = 1;
        auto a = fn<void>::from([&i, j]() mutable {
            j = (i += j);
        });
        fn<void> b;
        REQUIRE(a);
        REQUIRE_FALSE(b);
        b = std::move(a);
        REQUIRE_FALSE(a);
        REQUIRE(b);
        a.swap(b);
        REQUIRE(a);
        REQUIRE_FALSE(b);
        b = a.try_clone();
        REQUIRE(a);
        REQUIRE(b);
        a();
        REQUIRE(i == 1);
        REQUIRE_FALSE(a);
        b();
        REQUIRE(i == 2);
        REQUIRE_FALSE(b);
    }
   

    
}

TEST_CASE("atomic<stack<fn<int>>>", "[fn]") {

    {
        // mut
        atomic<stack<fn<int>>> a;
        a.push(fn<int>::from([] { return 1; }));
        a.push(fn<int>::from([] { return 2; }));
        REQUIRE(a.pop()() == 2);
        REQUIRE(a.pop()() == 1);
        REQUIRE_FALSE(a.pop());
    }
    
    {
        // const
        atomic<stack<fn<int>>> z;
        atomic<stack<fn<int>>> const& a = z;
        a.push(fn<int>::from([] { return 1; }));
        a.push(fn<int>::from([] { return 2; }));
        auto b = a.take();
        REQUIRE(b.pop()() == 2);
        REQUIRE(b.pop()() == 1);
        REQUIRE_FALSE(b.pop());
    }
}