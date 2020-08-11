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


#include "pool.hpp"

struct reactor {
    
    // simple, portable reactor using select and std::mutex
        
    // todo: if we buffer the inputs and splice them in once per loop we
    // only need to hold the locks very briefly
    
    // todo: there is some threshold at which batch submission to the
    // priority_queue and rebuilding the heap (3N) beats indiviudally adding
    // M elements (M ln(N))
    
    // undefined behaviour when two waiters on the same fd event (served in
    // reverse order but the first task will race to e.g. read all the data
    // before the second task sees it)
    
    // single thread that performs all file descriptor and timed waits
    std::thread _thread;
    
    // mutex held when not in select
    std::mutex _mutex;
    
    // one-time cancellation
    bool _cancelled;
    
    // recently-added waiters buffer
    using LIST = std::list<std::pair<int, std::function<void()>>>;
    LIST _readers_buf;
    LIST _writers_buf;
    LIST _excepters_buf;

    // recently-added timers buffer
    using PAIR = std::pair<std::chrono::time_point<std::chrono::steady_clock>, std::function<void()>>;
    std::stack<PAIR, std::vector<PAIR>> _timers_buf;

    // notification file descriptors
    int _pipe[2]; // <-- mutex protects write end of pipe, but not read end
    std::size_t _notifications;

    reactor()
    : _cancelled{false}
    , _notifications{0} {
        if (pipe(_pipe) != 0)
            (void) perror(strerror(errno)), abort();
        _thread = std::thread(&reactor::_run, this);
    }
    
    void _notify() {
        unsigned char c{0};
        if (write(_pipe[1], &c, 1) != 1)
            (void) perror(strerror(errno)), abort();
        ++_notifications;
    }
    
    void _when_able(int fd, std::function<void()> f, LIST& target) {
        LIST tmp;
        tmp.emplace_front(fd, std::move(f));
        auto lock = std::unique_lock(_mutex);
        target.splice(target.end(), tmp);
        _notify();
    }
    
    void when_readable(int fd, std::function<void()> f) {
        _when_able(fd, std::move(f), _readers_buf);
    }

    void when_writeable(int fd, std::function<void()> f) {
        _when_able(fd, std::move(f), _writers_buf);
    }
    
    void when_exceptional(int fd, std::function<void()> f) {
        _when_able(fd, std::move(f), _excepters_buf);
    }
        
    template<typename TimePoint>
    void when(TimePoint&& t, std::function<void()> f) {
        auto lock = std::unique_lock{_mutex};
        _timers_buf.emplace(std::forward<TimePoint>(t),
                            std::move(f));
        _notify();
    }
    
    template<typename Duration>
    void after(Duration&& t, std::function<void()> f) {
        when(std::chrono::steady_clock::now() + std::forward<Duration>(t),
             std::move(f));
    }

    void _run() {
        
        struct cmp_first {
            bool operator()(PAIR const& a,
                            PAIR const& b) {
                return a.first > b.first; // <-- reverse order puts earliest at top of priority queue
            }
        };
        
        LIST readers;
        LIST writers;
        LIST excepters;
        std::priority_queue<PAIR, std::vector<PAIR>, cmp_first> timers;
        
        std::list<std::function<void()>> pending;
        std::vector<char> buf;
        
        int count = 0; // <-- the number of events observed by select
        
        fd_set readset;
        FD_ZERO(&readset);
        
        fd_set writeset;
        FD_ZERO(&writeset);
        fd_set* pwriteset = nullptr;
        
        fd_set exceptset;
        FD_ZERO(&exceptset);
        fd_set* pexceptset = nullptr;
        
        timeval timeout;
        timeval *ptimeout = nullptr;
        
        std::size_t outstanding = 0; // <-- single-byte write notifications we have synchronized with but not yet cleared
        
        for (;;) {
            
            {
                auto lock = std::unique_lock{_mutex}; // <-- briefly lock to update state
                
                if (_cancelled)
                    break;
                outstanding += std::exchange(_notifications, 0);
                readers.splice(readers.cbegin(), _readers_buf); // <-- splicing avoids allocation in critical section
                writers.splice(writers.cbegin(), _writers_buf); // <-- reverse order of submission since young are more likely to fire soon
                excepters.splice(excepters.cbegin(), _excepters_buf);
                while (!_timers_buf.empty()) {
                    timers.push(std::move(_timers_buf.top()));
                    _timers_buf.pop();
                }
            }   // <-- unlock

            if (count && FD_ISSET(_pipe[0], &readset)) {
                // we risk clearing notifications before we have observed their
                // effects, so only read as many notifications (bytes) as we
                // know have been sent
                assert(outstanding > 0); // <-- else why is pipe readable?
                buf.reserve(std::max(outstanding, buf.capacity())); // <-- undefined behavior?
                ssize_t r = read(_pipe[0], buf.data(), outstanding);
                if (r <= 0)
                    (void) perror(strerror(errno)), abort();
                assert(r <= outstanding);
                outstanding -= r;
                --count;
            } else {
                FD_SET(_pipe[0], &readset);
            }
            int maxfd = _pipe[0];

            auto process = [&](LIST& list, fd_set* set) -> fd_set* {
                for (auto i = list.begin(); i != list.end(); ) {
                    int fd = i->first;
                    if (count && FD_ISSET(fd, set)) {
                        FD_CLR(fd, set);
                        pending.emplace_back(std::move(i->second));
                        i = list.erase(i);
                        --count;
                    } else {
                        assert(!FD_ISSET(fd, set)); // <-- detects undercount
                        FD_SET(i->first, set);
                        maxfd = std::max(maxfd, i->first);
                        ++i;
                    }
                }
                return list.empty() ? nullptr : set;
            };
            
            process(readers, &readset);
            pwriteset = process(writers, &writeset);
            pexceptset = process(excepters, &exceptset);
            
            assert(count == 0); // <-- detects overcount
            
            auto now = std::chrono::steady_clock::now();
            
            while ((!timers.empty()) && (timers.top().first <= now)) {
                pending.emplace_back(std::move(timers.top().second));
                timers.pop();
            }
            if (!timers.empty()) {
                auto usecs = std::chrono::duration_cast<std::chrono::
                    microseconds>(timers.top().first - now).count();
                timeout.tv_usec = (int) (usecs % 1'000'000);
                timeout.tv_sec = usecs / 1'000'000;
                ptimeout = &timeout;
            } else {
                ptimeout = nullptr;
            }
                        
            if (!pending.empty())
                pool::submit_many(std::move(pending));

            count = select(maxfd + 1, &readset, pwriteset, pexceptset, ptimeout);
            
            if (count == -1) {
                perror(strerror(errno));
                abort();
            }
            
        } // for(;;)
        
    } // run
    
    static reactor& get() {
        static reactor r;
        return r;
    }
    
    void _cancel() {
        auto lock = std::unique_lock(_mutex);
        _cancelled = true;
        _notify();
    }
    
    ~reactor() {
        _cancel();
        _thread.join();
        close(_pipe[1]);
        close(_pipe[0]);
    }
    
};

#endif /* reactor_hpp */
