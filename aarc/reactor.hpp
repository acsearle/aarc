//
//  reactor.hpp
//  aarc
//
//  Created by Antony Searle on 10/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef reactor_hpp
#define reactor_hpp

#include <unistd.h>
#include <sys/select.h>

#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <queue>
#include <stack>
#include <thread>

#include "stack.hpp"
#include "pool.hpp"

struct reactor {
    
    // portable(?) lock-free reactor using select
        
    // todo: there is some threshold at which batch submission to the
    // priority_queue and rebuilding the heap (3N) beats individually adding
    // M elements (M ln(N))
    
    // undefined behaviour when two waiters on the same fd event (served in
    // reverse order but the first task will race to e.g. read all the data
    // before the second task sees it)
            
    // buffers for recently-added waiters and timers
    alignas(64) stack<fn<void()>> _readers_buf;
    alignas(64) stack<fn<void()>> _writers_buf;
    alignas(64) stack<fn<void()>> _excepters_buf;
    alignas(64) stack<fn<void()>> _timers_buf;
    alignas(64) atomic<std::uint64_t> _cancelled_and_notifications;

    // single thread that waits on select
    std::thread _thread;

    // notification pipe
    int _pipe[2];
    static constexpr std::uint64_t CANCELLED_BIT = 0x1000'0000'0000'0000;

    reactor();    
    ~reactor();
    
    void _notify() const {
        auto old = _cancelled_and_notifications.fetch_or(1, std::memory_order_release);
        if (!(old & 1)) {
            unsigned char c{0};
            if (write(_pipe[1], &c, 1) != 1) // <-- multiple writers are possible
                (void) perror(nullptr), abort();
        }
    }
    
    void _when_able(int fd, fn<void()> f, stack<fn<void()>> const& target) const {
        f->_fd = fd;
        target.push(std::move(f));
        _notify();
    }
    
    void when_readable(int fd, fn<void()> f) const {
        _when_able(fd, std::move(f), _readers_buf);
    }

    void when_writeable(int fd, fn<void()> f) const {
        _when_able(fd, std::move(f), _writers_buf);
    }
    
    void when_exceptional(int fd, fn<void()> f) const {
        _when_able(fd, std::move(f), _excepters_buf);
    }
        
    template<typename TimePoint>
    void when(TimePoint&& t, fn<void()> f) const {
        f->_t = t;
        _timers_buf.push(std::move(f));
        _notify();
    }
    
    template<typename Duration>
    void after(Duration&& t, std::function<void()> f) const {
        when(std::chrono::steady_clock::now() + std::forward<Duration>(t),
             std::move(f));
    }

    void _run() const;
    
    static reactor const& get() {
        static reactor r;
        return r;
    }
    
    void _cancel() const {
        _cancelled_and_notifications.fetch_or(CANCELLED_BIT,
                                              std::memory_order_release);
        _notify();
    }
        
};

#endif /* reactor_hpp */
