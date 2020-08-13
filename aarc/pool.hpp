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



struct pool {
    
    alignas(64) atomic<stack<fn<void()>>> _stack;
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
    
    void _submit_many(atomic<stack<fn<void()>>> s) const {
        if (_stack.splice(std::move(s)))
            _stack.notify_one();
    }

    static void submit_one(fn<void()> f) {
        _get()._submit_one(std::move(f));
    }
    
    static void submit_many(atomic<stack<fn<void()>>> s) {
        _get()._submit_many(std::move(s));
    }
        
}; // struct pool





#if 0
struct pool_monitor {
    
    std::mutex _mutex;
    std::condition_variable _ready;
    std::vector<std::thread> _threads;
    std::list<fn<void>> _queue;
    bool _cancelled;
    std::size_t _waiters;
    
    pool_monitor() {
        auto n = std::thread::hardware_concurrency();
        _threads.reserve(n);
        while (_threads.size() != n)
            _threads.emplace_back(&pool_monitor::run, this);
    }
    
    void run() {
        auto lock = std::unique_lock{_mutex};
        while (!_cancelled) {
            if (_queue.empty()) {
                ++_waiters;
                _ready.wait(lock); // <-- unlocked, suspended while waiting
                --_waiters;
            } else {
                {
                    auto f{std::move(_queue.front())};
                    _queue.pop_front();
                    lock.unlock();
                    f(); // <-- unlocked while executing
                }
                lock.lock();
            }
        }
    }
    
    void _cancel() {
        auto lock = std::unique_lock{_mutex};
        if (!_cancelled) {
            _cancelled = true;
            decltype(_queue) tmp;
            tmp.swap(_queue);
            lock.unlock();
            _ready.notify_all();
        }   // <-- tmp destroys all pending jobs
    }
    
    ~pool_monitor() {
        _cancel();
        while (!_threads.empty()) {
            _threads.back().join();
            _threads.pop_back();
        }
        assert(_waiters == 0);
    }
    
    static pool& _get() {
        static pool p;
        return p;
    }
    
    template<typename Callable>
    void _submit_one(Callable&& f) {
        auto lock = std::unique_lock{_mutex};
        if (!_cancelled) {
            _queue.emplace_back(std::forward<Callable>(f));
            if (_waiters) {
                lock.unlock();
                _ready.notify_one();
            }
        }
    }
    
    void _submit_many(std::list<fn<void>>&& q) {
        auto lock = std::unique_lock{_mutex};
        if (!_cancelled) {
            auto n = std::min(q.size(), _waiters);
            if (n) {
                _queue.splice(_queue.end(), std::move(q));
                lock.unlock();
                //if (n > 1)
                //    _ready.notify_all();
                //else
                //    _ready.notify_one();
                while (n--)
                    _ready.notify_one();
            }
        } else {
            lock.unlock();
            q.clear();
        }
    }

    template<typename Callable>
    static void submit_one(Callable&& f) {
        _get()._submit_one(std::forward<Callable>(f));
    }
    
    static void submit_many(std::list<fn<void>>&& q) {
        _get()._submit_many(std::move(q));
    }
        
}; // struct pool
#endif

#endif /* pool_hpp */
