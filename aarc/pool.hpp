//
//  pool.hpp
//  aarc
//
//  Created by Antony Searle on 10/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef pool_hpp
#define pool_hpp

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

#include "fn.hpp"
#include "stack.hpp"



struct pool { /* lock free greedy unbalanced */
    
    alignas(64) stack<fn<void()>> _stack;
    alignas(64) atomic<bool> _cancelled;
    std::vector<std::thread> _threads;

    pool() {
        auto n = std::thread::hardware_concurrency();
        _threads.reserve(n);
        while (_threads.size() != n)
            _threads.emplace_back(&pool::_run, this);
    }
    
    void _run() const {
        while (!_cancelled.load(std::memory_order_acquire)) {
            auto s = _stack.take();
            if (s.empty()) {
                _stack.wait();
            } else {
                s.reverse();
                while (!s.empty())
                    s.pop()();
            }
        }
    }
    
    void _cancel() const {
        _cancelled.store(true, std::memory_order_release);
        _stack.notify_all();
    }
    
    ~pool() {
        _cancel();
        while (!_threads.empty()) {
            _threads.back().join();
            _threads.pop_back();
        }
    }
    
    static pool const& _get() {
        static pool p;
        return p;
    }
    
    void _submit_one(fn<void()> f) const {
        if (_stack.push(std::move(f)))
            _stack.notify_one();
    }
    
    void _submit_many(stack<fn<void()>> s) const {
        if (_stack.splice(std::move(s)))
            _stack.notify_one();
    }

    static void submit_one(fn<void()> f) {
        _get()._submit_one(std::move(f));
    }
    
    static void submit_many(stack<fn<void()>> s) {
        _get()._submit_many(std::move(s));
    }
        
}; // struct pool





#endif /* pool_hpp */
