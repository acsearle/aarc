//
//  corrode.hpp
//  aarc
//
//  Created by Antony Searle on 8/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef corrode_hpp
#define corrode_hpp

#include <experimental/coroutine>



//  forever
//
//  an awaitable that never calls (and immediately destroys) its continuation

inline constexpr struct {
    bool await_ready() const { return false; }
    void await_suspend(std::experimental::coroutine_handle<> handle) const {
        handle.destroy();
    }
    void await_resume() const { std::terminate(); };
} forever;



//  promise for a void-returning coroutine
//
//  the coroutine body is eagerly evaluated until a co_await schedules the
//  continuation elsewhere
//
//      void foo() {
//          ssize_t n = co_await async_read(fd, buf, n);
//          printf("read %td\n", n);
//      }

struct eager_task_promise {
    
    void get_return_object() {}
    auto initial_suspend() { return std::experimental::suspend_never{}; }
    void return_void() {}
    auto final_suspend() { return std::experimental::suspend_never{}; }
    void unhandled_exception() { std::terminate(); }
    
};

namespace std::experimental {

template<typename... Args>
struct coroutine_traits<void, Args...> { using promise_type = eager_task_promise; };

}

template<typename T = void>
struct future {
    
    struct promise_type {
        
        std::experimental::coroutine_handle<> _continuation;
        T _value;
        
        auto get_return_object() {
            return future<T>{
                std::experimental::coroutine_handle<promise_type>::from_promise(*this)
            };
        }
        auto initial_suspend() { return std::experimental::suspend_always{}; } // <-- do we really want to be lazy?
        void return_value(T x) { _value = std::move(x); }
        
        struct final_awaitable {
            
            bool await_ready() { return false; }
            auto await_suspend(std::experimental::coroutine_handle<promise_type> handle) {
                return handle.promise()._continuation;
            }
            void await_resume() {};
        };
        
        auto final_suspend() { return final_awaitable{}; }
        void unhandled_exception() { std::terminate(); }
        
    };
    
    std::experimental::coroutine_handle<promise_type> _continuation;
    
    bool await_ready() {
        return _continuation.done();
    }
    
    auto await_suspend(std::experimental::coroutine_handle<> continuation) {
        _continuation.promise()._continuation = continuation;
        return _continuation;
    }
    
    T await_resume() {
        return _continuation.promise()._value;
    }
    
};


// fixme: refactor out common parts
template<>
struct future<void> {
    
    struct promise_type {
        
        std::experimental::coroutine_handle<> _continuation;
        
        auto get_return_object() {
            return future<void>{
                std::experimental::coroutine_handle<promise_type>::from_promise(*this)
            };
        }
        auto initial_suspend() { return std::experimental::suspend_always{}; }
        void return_void() {};
        
        struct final_awaitable {
            
            bool await_ready() { return false; }
            auto await_suspend(std::experimental::coroutine_handle<promise_type> handle) {
                return handle.promise()._continuation;
            }
            void await_resume() {};
        };
        
        auto final_suspend() { return final_awaitable{}; }
        void unhandled_exception() { std::terminate(); }
        
    };
    
    std::experimental::coroutine_handle<promise_type> _continuation;
    
    bool await_ready() {
        return _continuation.done();
    }
    
    auto await_suspend(std::experimental::coroutine_handle<> continuation) {
        _continuation.promise()._continuation = continuation;
        return _continuation;
    }
    
    void await_resume() {}
        
};


#endif /* corrode_hpp */
