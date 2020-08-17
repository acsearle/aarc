//
//  corrode.cpp
//  aarc
//
//  Created by Antony Searle on 8/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include <experimental/coroutine>
#include <future>
#include <map>
#include <list>
#include <thread>
#include <utility>
#include <vector>

#include "atomic_wait.hpp"
#include "corrode.hpp"
#include "catch.hpp"
#include "pool.hpp"
#include "reactor.hpp"


TEST_CASE("await-forever", "[await]") {
    
    bool prefix = false;
    [&]() -> void {
        prefix = true;
        co_await forever;
        REQUIRE(false);
    }();
    REQUIRE(prefix);
    
}

TEST_CASE("await-transfer", "[transfer]") {

    std::promise<std::thread::id> p;
    auto f = [&]() -> void {
        co_await transfer; // continue on pool thread
        p.set_value(std::this_thread::get_id());
    };
    f();
    REQUIRE(p.get_future().get() != std::this_thread::get_id());
}


TEST_CASE("await-read", "[await]") {
    
    int p[2];
    pipe(p);
    
    std::promise<char> c;
    
    auto f = [&]() -> void {
        char b;
        ssize_t r = co_await async_read(p[0], &b, 1);
        assert(r == 1);
        c.set_value(b);
    };
    f();
    
    std::this_thread::sleep_for(std::chrono::seconds{1});

    char b{1};
    write(p[1], &b, 1);

    REQUIRE(c.get_future().get() == 1);
    
    close(p[1]);
    close(p[0]);
    
}

TEST_CASE("await-future", "[await]") {

    printf("0\n");
    []() -> void {
        printf("a\n");
        int i = co_await []() -> future<int> {
            printf("b\n");
            co_await transfer;
            printf("c\n");
            co_return 7;
        }();
        printf("d (%d)\n", i);
    }();
    printf("e\n");
    std::this_thread::sleep_for(std::chrono::seconds{1});
    printf("f\n");
}

void reader(int fd, int n, std::promise<void>& p) {
    co_await transfer;
    char c;
    while (n) {
        n -= co_await async_read(fd, &c, 1);
    }
    p.set_value();
}

void writer(int fd, int n) {
    co_await transfer;
    char c;
    while (n) {
        n -= co_await async_write(fd, &c, 1);
    }
}

TEST_CASE("stress", "[await]") {
    
    std::promise<void> f;
    
    int p[2];
    pipe(p);
    int n = 5'000;
    reader(p[0], n, f);
    writer(p[1], n);
    
    f.get_future().get();
    
    
}

