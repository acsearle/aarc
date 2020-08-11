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
    
    // undefined behaviour when two waiters on the same fd event (served in
    // order but the first task will race to e.g. read all the data before the
    // second task sees it)
    
    // single thread that performs all file descriptor and timed waits
    std::thread _thread;
    
    // mutex held when not in select
    std::mutex _mutex;
    bool _cancelled;
    
    // lists of waiters
    using L = std::list<std::pair<int, std::function<void()>>>;
    L _readers;
    L _writers;
    L _excepters;

    struct cmp_first {
        template<typename A, typename B>
        bool operator()(A&& a, B&& b) { return a.first < b.first; }
    };
    using P = std::pair<std::chrono::time_point<std::chrono::steady_clock>, std::function<void()>>;
    std::priority_queue<P, std::vector<P>, cmp_first> _timers;

    // notification file descriptors
    int _pipe[2];
    std::size_t _notifications;

    reactor()
    : _cancelled{false} {
        auto r = pipe(_pipe);
        assert(r == 0);
        _thread = std::thread(&reactor::_run, this);
    }
    
    void _notify() {
        unsigned char c{0};
        auto r = write(_pipe[1], &c, 1);
        assert(r == 1);
        ++_notifications;
    }
    
    void _when_able(int fd, std::function<void()> f, L& target) {
        L tmp;
        tmp.emplace_back(fd, std::move(f));
        auto lock = std::unique_lock(_mutex);
        target.splice(target.end(), tmp);
        _notify();
    }
    
    void when_readable(int fd, std::function<void()> f) {
        _when_able(fd, std::move(f), _readers);
    }

    void when_writeable(int fd, std::function<void()> f) {
        _when_able(fd, std::move(f), _writers);
    }
    
    void when_exceptional(int fd, std::function<void()> f) {
        _when_able(fd, std::move(f), _excepters);
    }
        
    template<typename TimePoint>
    void when(TimePoint&& t, std::function<void()> f) {
        auto lock = std::unique_lock{_mutex};
        _timers.emplace(std::forward<TimePoint>(t), std::move(f));
        _notify();
    }
    
    template<typename Duration>
    void after(Duration&& t, std::function<void()> f) {
        when(std::chrono::steady_clock::now() + std::forward<Duration>(t), std::move(f));
    }

    void _run() {
        
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
        
        for (;;) {
            
            auto lock = std::unique_lock{_mutex};

            if (_cancelled)
                break;

            if (count && FD_ISSET(_pipe[0], &readset)) {
                buf.resize(std::max(_notifications, buf.size()));
                [[maybe_unused]] ssize_t r = read(_pipe[0], buf.data(), _notifications);
                assert(r > 0);
                --count;
            } else {
                FD_SET(_pipe[0], &readset);
            }
            int maxfd = _pipe[0];

            auto process = [&](L& list, fd_set* set) -> fd_set* {
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
            
            process(_readers, &readset);
            pwriteset = process(_writers, &writeset);
            pexceptset = process(_excepters, &exceptset);
            
            assert(count == 0);
            
            auto now = std::chrono::steady_clock::now();
            
            while ((!_timers.empty()) && (_timers.top().first <= now)) {
                pending.emplace_back(std::move(_timers.top().second));
                _timers.pop();
            }
            if (!_timers.empty()) {
                auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(_timers.top().first - now).count();
                timeout.tv_usec = (int) (usecs % 1'000'000);
                timeout.tv_sec = usecs / 1'000'000;
                ptimeout = &timeout;
            } else {
                ptimeout = nullptr;
            }
            
            lock.unlock();
            
            if (!pending.empty())
                pool::submit_many(std::move(pending));

            count = select(maxfd + 1, &readset, pwriteset, pexceptset, ptimeout);
            
            if (count == -1) {
                assert(false);
                count = 0;
                FD_ZERO(&readset);
                FD_ZERO(&writeset);
                FD_ZERO(&exceptset);
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
