//
//  quack.hpp
//  aarc
//
//  Created by Antony Searle on 25/7/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef quack_hpp
#define quack_hpp

#include <atomic>
#include <future>
#include <mutex>
#include <queue>
#include <stack>
#include <thread>


struct quack2 {
    
    // prevent thundering herd by explicitly assigning jobs to waiting threads
    
    struct cancelled {};
    
    std::mutex _mutex;
    bool _done = false;

    std::queue<std::function<void()>> _queue;
    std::stack<std::promise<std::function<void()>>> _stack;
    
    void push(std::function<void()> fun) {
        assert(fun);
        auto lock = std::unique_lock{_mutex};
        if (_done) {
            assert(_queue.empty());
            assert(_stack.empty());
            throw cancelled{};
        } else if (!_stack.empty()) {
            assert(_queue.empty());
            // if there are waiters, assign work to youngest
            _stack.top().set_value(std::move(fun));
            _stack.pop();
        } else {
            // else push work to back of queue
            _queue.push(std::move(fun));
        }
    }
    
    std::function<void()> pop() {
        auto lock = std::unique_lock{_mutex};
        if (_done) {
            assert(_queue.empty());
            assert(_stack.empty());
            throw cancelled{};
        } else if (!_queue.empty()) {
            assert(_stack.empty());
            // if there is work, take the oldest
            auto fun{std::move(_queue.front())};
            _queue.pop();
            return fun;
        } else {
            // else put promise on top of stack and wait on its future
            _stack.emplace();
            auto fut{_stack.top().get_future()};
            lock.unlock();
            return fut.get();
        }
    }
    
    std::function<void()> try_pop() {
        auto lock = std::unique_lock{_mutex};
        if (_done || _queue.empty()) {
            return std::function<void()>{};
        } else {
            auto fun{std::move(_queue.front())};
            _queue.pop();
            return fun;
        }
    }

    void cancel() {
        auto lock = std::unique_lock{_mutex};
        _done = true;
        auto tmp{std::move(_stack)};
        lock.unlock();
        while (!tmp.empty()) {
            tmp.top().set_exception(std::make_exception_ptr(cancelled{}));
            tmp.pop();
        }
    }
    
    ~quack2() {
        // destruction while there are waiters indicates a race
        assert(_stack.empty());
    }
    
};


struct quack {
    
    std::atomic<std::uintptr_t> _head;
    std::atomic<std::uintptr_t> _tail;
    
    struct node {
        std::atomic<std::intptr_t> _count;
    };
    
    struct stack_node : node {
        std::uintptr_t _next;
    };
    
    struct queue_node : node{
        std::atomic<std::uintptr_t> _next;
    };
    
    constexpr static std::uintptr_t M = 0x0000'FFFF'FFFF'FFF0;
    constexpr static std::uintptr_t I = 0x0001'0000'0000'0000;
    
    void push() {
        
        std::uintptr_t a;
        queue_node* b;
        std::uintptr_t c;
        stack_node* d;
        std::uintptr_t e = 0xFFFF'0000'0000'0001 | (std::uintptr_t) new queue_node{{0x0000'0000'0001'0000}, 0};

        
        
        a = _tail.load(std::memory_order_relaxed);
        for (;;) {
            if (_tail.compare_exchange_weak(a, a - I, std::memory_order_acquire, std::memory_order_relaxed)) {
                b = (queue_node*) (a & M);
                c = b->_next.load(std::memory_order_relaxed);
                for (;;) {
                    if (c & 2) {
                        // pop stack node
                        if (b->_next.compare_exchange_weak(c, c - I, std::memory_order_acquire, std::memory_order_relaxed)) {
                            d = (stack_node*) (c & M);
                            if (b->_next.compare_exchange_weak(c, d->_next, std::memory_order_acquire, std::memory_order_relaxed)) {
                                return;
                            }
                        }
                    } else if (c & 1) {
                        // swing queue tail
                        a -= I;
                        if (_tail.compare_exchange_weak(a, c, std::memory_order_release, std::memory_order_relaxed)) {
                            a = c;
                            // jump out
                        }
                        
                    } else {
                        
                        // push queue node
                        if (b->_next.compare_exchange_weak(c, e, std::memory_order_release, std::memory_order_relaxed)) {
                            // cleanup
                            return;
                        }
                        
                    }
                }
            }
        }
        
    }
    
    
    void pop() {
        
        std::uintptr_t a, c;
        queue_node* b;

        a = _head.load(std::memory_order_relaxed);
        for (;;) {
            if (_head.compare_exchange_weak(a, a - I, std::memory_order_acquire, std::memory_order_relaxed)) {
                b = (queue_node*) (a & M);
                c = b->_next.load(std::memory_order_relaxed);
                if (c & 1) {
                    // pop queue node
                } else {
                    // push stack node
                }
            }
        }
        
    }

};



#include "catch.hpp"

TEST_CASE("quack") {
    
    quack2 a;
    a.push([](){});
    a.pop();
    
}



#endif /* quack_hpp */
