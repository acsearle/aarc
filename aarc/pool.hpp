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

struct pool {
    
    std::mutex _mutex;
    std::condition_variable _ready;
    std::vector<std::thread> _threads;
    std::list<std::function<void()>> _queue;
    bool _cancelled;
    std::size_t _waiters;
    
    pool() {
        auto n = std::thread::hardware_concurrency();
        _threads.reserve(n);
        for (decltype(n) i = 0; i != n; ++i)
            _threads.emplace_back(&pool::run, this);
    }
    
    void run() {
        auto lock = std::unique_lock{_mutex};
        while (!_cancelled) {
            if (_queue.empty()) {
                ++_waiters;
                _ready.wait(lock);
                --_waiters;
            } else {
                auto f{std::move(_queue.front())};
                _queue.pop_front();
                lock.unlock();
                f();
                f = nullptr; // call destructor while unlocked
                // allow exceptions to call terminate
                lock.lock();
            }
        }
    }
    
    ~pool() {
        _cancel();
        for (auto&& t : _threads)
            t.join();
    }
    
    static pool& get() {
        static pool p;
        return p;
    }
    
    template<typename Callable>
    void _submit(Callable&& f) {
        auto lock = std::unique_lock{_mutex};
        if (!_cancelled) {
            _queue.emplace_back(std::forward<Callable>(f));
            if (_waiters) {
                lock.unlock();
                _ready.notify_one();
            }
        }
    }
    
    template<typename Callable>
    static void submit(Callable&& f) {
        get()._submit(std::forward<Callable>(f));
    }
    
    void _submit_many(std::list<std::function<void()>>&& q) {
        auto lock = std::unique_lock{_mutex};
        if (!_cancelled) {
            _queue.splice(_queue.end(), std::move(q));
            if (_waiters) {
                if (std::min(_waiters, _queue.size()) > 1) {
                    lock.unlock();
                    _ready.notify_all();
                } else {
                    lock.unlock();
                    _ready.notify_one();
                }
            }
        }
    }

    static void submit_many(std::list<std::function<void()>>&& q) {
        get()._submit_many(std::move(q));
    }

    void _cancel() {
        auto lock = std::unique_lock{_mutex};
        if (!_cancelled) {
            _cancelled = true;
            decltype(_queue) tmp;
            using std::swap;
            swap(tmp, _queue);
            lock.unlock();
            _ready.notify_all();
            // tmp destroys all pending jobs
        }
    }
    
    template<typename Callable>
    static void cancel() {
        get()._cancel();
    }
        
}; // struct pool

#endif /* pool_hpp */
