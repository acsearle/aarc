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
    int n = 5000'000;
    reader(p[0], n, f);
    writer(p[1], n);
    
    f.get_future().get();
    
    
}


void foo() {
    printf("hello\n");
    co_return;
}


/*
template<typename Rep, typename Period>
struct await_duration {
    std::chrono::duration<Rep, Period> _t;
    explicit await_duration(std::chrono::duration<Rep, Period> t) : _t(t) {}
    bool await_ready() { return false; }
    template<typename T>
    void await_suspend(std::experimental::coroutine_handle<T> h) {
        std::thread([h, t=std::move(_t)] () mutable {
            std::this_thread::sleep_for(t);
            h.resume();
        }).detach();
    }
    void await_resume() {}
};

template<typename Rep, typename Period>
await_duration<Rep, Period> operator co_await(std::chrono::duration<Rep, Period> t) {
    return await_duration<Rep, Period>(std::move(t));
}


template<typename Rep, typename Period>
struct await_duration {
    std::chrono::duration<Rep, Period> _t;
    explicit await_duration(std::chrono::duration<Rep, Period> t) : _t(t) {}
    bool await_ready() { return false; }
    template<typename T>
    void await_suspend(std::experimental::coroutine_handle<T> h) {
        std::thread([h, t=std::move(_t)] () mutable {
            std::this_thread::sleep_for(t);
            h.resume();
        }).detach();
    }
    void await_resume() {}
};

template<typename Rep, typename Period>
await_duration<Rep, Period> operator co_await(std::chrono::duration<Rep, Period> t) {
    return await_duration<Rep, Period>(std::move(t));
}

template<typename Clock, typename Duration>
struct await_time_point {
    std::chrono::time_point<Clock, Duration> _t;
    explicit await_time_point(std::chrono::time_point<Clock, Duration> t) : _t(std::move(t)) {}
    bool await_ready() { return false; }
    template<typename T>
    void await_suspend(std::experimental::coroutine_handle<T> h) {
        std::thread([h, t=std::move(_t)] () mutable {
            std::this_thread::sleep_until(t);
            h.resume();
        }).detach();
    }
    void await_resume() {}
};

template<typename T>
struct await_value {
    T _x;
    explicit await_value(T x) : _x(std::move(x)) {}
    bool await_ready() { return false; }

    template<typename U>
    bool await_suspend(std::experimental::coroutine_handle<U> h) {
        printf("suspended!\n");
        return false;
    }
    T await_resume() { return _x; }
};

void foo() {
    int x = co_await await_value(7);
    printf("%d\n", x);
}

TEST_CASE("foo") {
    foo();
}



template<typename T>
struct future {
    T _x;
    bool _ready;
    
    future() : _ready(false) {}
    
    bool await_ready() { return _ready; }
    
    template<typename U>
    bool await_suspend(std::experimental::coroutine_handle<U> h) {
        h.resume();
    }
    
    T await_resume() { return _x; }
    
};


future<int> barre() {
    
}
*/
