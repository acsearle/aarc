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
    [&]() -> void {
        co_await transfer; // continue on pool thread
        p.set_value(std::this_thread::get_id());
    }();
    REQUIRE(p.get_future().get() != std::this_thread::get_id());
}


struct async_read {
    
    int _fd;
    void* _buf;
    size_t _count;
    ssize_t _return_value;
    
    async_read(int fd, void* buf, size_t count)
    : _fd(fd)
    , _buf(buf)
    , _count(count) {
    }

    void _execute() {
        _return_value = read(_fd, _buf, _count);
        assert(_return_value > 0);
    }

    bool await_ready() {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(_fd, &readfds);
        timeval t{0, 0};
        return ((select(_fd + 1, &readfds, nullptr, nullptr, &t) == 1)
                && ((void) _execute(), true));
    }
    
    void await_suspend(std::experimental::coroutine_handle<> handle) {
        reactor::get().when_readable(_fd, [=]() mutable {
            _execute();
            handle();
        });
    }
    
    ssize_t await_resume() {
        return _return_value;
    }
    
};

TEST_CASE("await-read", "[await]") {
    
    int p[2];
    pipe(p);
    
    std::promise<char> c;
    
    [&]() -> void {
        char b;
        ssize_t r = co_await async_read(p[0], &b, 1);
        assert(r == 1);
        c.set_value(b);
    }();
    
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



void foo() {
    printf("hello\n");
    co_return;
}

struct promise_lazy;
struct lazy;

struct promise_lazy {
    
    std::experimental::coroutine_handle<> continuation;
    
    struct final_await {
        bool await_ready() { return false; }
        auto await_suspend(std::experimental::coroutine_handle<promise_lazy> h) {
            // h is now the end of the function and does nothing
            return h.promise().continuation;
        }
        void await_resume() {};
    };
    
    lazy get_return_object();
    auto initial_suspend() { return std::experimental::suspend_always{}; }
    void return_void() {}
    auto final_suspend() { return final_await{}; }
    void unhandled_exception() {}
};

struct lazy {
    promise_lazy* ptr;
    using promise_type = promise_lazy;
    bool await_ready() { return false; }
    template<typename U>
    auto await_suspend(std::experimental::coroutine_handle<U> u) {
        ptr->continuation = u;
        return std::experimental::coroutine_handle<promise_type>::from_promise(*ptr);
    }
    void await_resume() {}
};

lazy promise_lazy::get_return_object() { return lazy{this}; }




lazy bar() {
    printf("inside bar\n");
    co_return;
}

lazy car() {
    printf("inside car\n");
    co_return;
}

lazy foo2() {
    printf("before bar\n");
    co_await bar();
    printf("after bar\n");
}

lazy foo2a() {
    printf("before car\n");
    co_await car();
    printf("after car\n");
}


void foo3() {
    auto x = foo2();
    printf("before foo2\n");
    co_await x;
    printf("after foo2\n");
    printf("before foo2a\n");
    co_await foo2a();
    printf("after foo2a\n");
}



TEST_CASE("foo") {
    auto x = foo3;
    printf("proof of laziness?\n");
    x();
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

struct promise_void {
    void get_return_object() {}
    std::experimental::suspend_never initial_suspend() { return std::experimental::suspend_never{}; }
    void return_void() {}
    std::experimental::suspend_never final_suspend() { return std::experimental::suspend_never{}; }
    void unhandled_exception() {}
};

namespace std::experimental {
template<typename... Args> struct coroutine_traits<void, Args...> {
    using promise_type = promise_void;
};
}

template<typename T>
struct promise;

template<typename T>
struct future {
    using promise_type = promise<T>;
    std::future<T> _future;
    
    //T get() { return _future.get(); } // blocking
    
    bool await_ready() {
        return _future.wait_for(std::chrono::seconds::zero()) == std::future_status::ready;
    }
    
    template<typename U>
    void await_suspend(std::experimental::coroutine_handle<U> h) {
        std::thread([h]() mutable {
            h.resume();
        }).detach();
    }
    
    T await_resume() {
        return _future.get();
    }
    
    
};

template<typename T>
struct promise {
    std::promise<T> _promise;
    future<T> get_return_object() {
        return future<T>{_promise.get_future()};
    }
    auto initial_suspend() { return std::experimental::suspend_never{}; }
    void return_value(T x) { _promise.set_value(std::move(x)); }
    auto final_suspend() { return std::experimental::suspend_never{}; }
    void unhandled_exception() { _promise.set_exception(std::current_exception()); }
};


future<int> foo() {
    printf("enter foo\n");
    co_await std::chrono::seconds(1);
    printf("return from foo\n");
    co_return 7;
}

void bar2() {
    int x = co_await foo();
    printf("%d\n", x);
}

TEST_CASE("df") {
    bar2();
    //printf("%d\n", foo().get());
}
*/


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

struct promise {
    void get_return_object() {}
    std::experimental::suspend_never initial_suspend() { return std::experimental::suspend_never{}; }
    void return_void() {}
    std::experimental::suspend_never final_suspend() { return std::experimental::suspend_never{}; }
    void unhandled_exception() {}
};

namespace std::experimental {
template<typename... Args> struct coroutine_traits<void, Args...> {
    using promise_type = promise;
};
}

void boo() {
    co_await std::chrono::seconds(1);
    printf("boo!\n");
}

struct await_read {
    int _fd;
    void* _buf;
    size_t _count;
    ssize_t ret;
    await_read(int fd, void* buf, size_t count)
    : _fd(fd), _buf(buf), _count(count) {
    }
    bool await_ready() { return false; }
    template<typename T>
    void await_suspend(std::experimental::coroutine_handle<T> h) {
        std::thread([h, this] () mutable {
            ret = read(_fd, _buf, _count);
            h.resume();
        }).detach();
    }
    ssize_t await_resume() {
        return ret;
    }
};

await_read async_read(int fd, void* buf, size_t count) {
    return await_read(fd, buf, count);
}

void bar(int fd) {
    char c = '-';
    auto r = co_await async_read(fd, &c, 1);
    printf("%c (read %td)\n", c, r);
}


template<typename Rep, typename Period>
struct transfer {
    bool await_ready() { return false; }
    template<typename T>
    void await_suspend(std::experimental::coroutine_handle<T> h) {
        std::thread([h] () mutable {
            h.resume();
        }).detach();
    }
    void await_resume() {}
};

TEST_CASE("corrode") {
    boo();
    printf("relax...\n");
    
    int pipefd[2];
    pipe(pipefd);
    bar(pipefd[0]);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    write(pipefd[1], "x", 1);

}

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
