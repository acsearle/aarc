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

struct fn {
    
    struct base {
        base() : _fd(0) {}
        base* _next;
        base* _prev;
        union {
            int _fd;
            std::chrono::time_point<std::chrono::steady_clock> _t;
        };
        virtual ~base() = default;
        virtual void operator()() = 0;
    };
    
    std::unique_ptr<base> _ptr;
    
    template<typename T>
    struct derived : base {
        
        T _payload;
        
        template<typename... Args>
        derived(Args&&... args)
        : base()
        , _payload(std::forward<Args>(args)...) {
        }
        
        virtual ~derived() override final = default;
        virtual void operator()() override final {
            _payload();
        }
    };
    
    template<typename T, typename... Args>
    static fn from(Args&&... args) {
        return fn{new derived<T>(std::forward<Args>(args)...)};
    }
    
    void operator()() {
        (*_ptr)();
    }
    
};

struct reactor {
    
    // simple, portable reactor using select and std::mutex
    
    // todo: replace C<A, std::function<void()>> with intrusive linked list nodes
    
    // todo: if we buffer the inputs and splice them in once per loop we
    // only need to hold the locks very briefly
    
    // todo: there is some threshold at which batch submission to the
    // priority_queue and rebuilding the heap (3N) beats indiviudally adding
    // M elements (M ln(N))
    
    // undefined behaviour when two waiters on the same fd event (served in
    // order but the first task will race to e.g. read all the data before the
    // second task sees it)
    
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
        tmp.emplace_back(fd, std::move(f));
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
        _timers_buf.emplace(std::forward<TimePoint>(t), std::move(f));
        _notify();
    }
    
    template<typename Duration>
    void after(Duration&& t, std::function<void()> f) {
        when(std::chrono::steady_clock::now() + std::forward<Duration>(t), std::move(f));
    }

    void _run() {
        
        struct cmp_first {
            bool operator()(PAIR const& a, PAIR const& b) { return a.first < b.first; }
        };
        
        LIST readers;
        LIST writers;
        LIST excepters;
        std::priority_queue<PAIR, std::vector<PAIR>, cmp_first> timers;
        
        std::list<std::function<void()>> pending;
        std::vector<char> buf;
        
        int count = 0;
        
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(_pipe[0], &readset);
        
        fd_set writeset;
        FD_ZERO(&writeset);
        fd_set* pwriteset = nullptr;
        
        fd_set exceptset;
        FD_ZERO(&exceptset);
        fd_set* pexceptset = nullptr;
        
        timeval timeout;
        timeval *ptimeout = nullptr;
        
        std::size_t notifications_observed;
        
        for (;;) {
            
            {
                auto lock = std::unique_lock{_mutex}; // <-- briefly lock to update state
                
                if (_cancelled)
                    break;
                
                readers.splice(readers.cend(), _readers_buf); // <-- avoid allocations
                writers.splice(writers.cend(), _writers_buf);
                excepters.splice(excepters.cend(), _excepters_buf);
                while (!_timers_buf.empty()) {
                    timers.push(std::move(_timers_buf.top()));
                    _timers_buf.pop();
                }
                notifications_observed = std::exchange(_notifications, 0);
            }   // <-- unlock

            if (count && FD_ISSET(_pipe[0], &readset)) {
                buf.reserve(std::max(notifications_observed, buf.capacity()));
                [[maybe_unused]] ssize_t r = read(_pipe[0], buf.data(), notifications_observed);
                assert(r > 0);
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
                        assert(!FD_ISSET(fd, set));
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
            
            assert(count == 0);
            
            auto now = std::chrono::steady_clock::now();
            
            while ((!timers.empty()) && (timers.top().first <= now)) {
                pending.emplace_back(std::move(timers.top().second));
                timers.pop();
            }
            if (!timers.empty()) {
                auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(timers.top().first - now).count();
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
