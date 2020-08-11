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
    
    // todo: replace C<A, std::function<void()>> with intrusive linked list nodes
    //
    
    // single thread that performs all file descriptor and timed waits
    std::thread _thread;
    
    // mutex held when not in select
    std::mutex _mutex;
    bool _cancelled;
    
    // lists of waiters
    std::map<int, std::function<void()>> _readers;
    std::map<int, std::function<void()>> _writers;
    std::map<int, std::function<void()>> _excepters;

    struct cmp_first {
        template<typename A, typename B>
        bool operator()(A&& a, B&& b) { return a.first < b.first; }
    };
    using P = std::pair<std::chrono::time_point<std::chrono::steady_clock>, std::function<void()>>;
    std::priority_queue<P, std::vector<P>, cmp_first> _timers;

    // notification file descriptors
    int _pipe[2];

    reactor()
    : _cancelled{false} {
        auto r = pipe(_pipe);
        assert(r == 0);
        _thread = std::thread(&reactor::run, this);
    }
    
    void notify() {
        unsigned char c{0};
        auto r = write(_pipe[1], &c, 1);
        assert(r == 1);
    }
    
    void when_readable(int fd, std::function<void()> f) {
        auto lock = std::unique_lock(_mutex);
        _readers.emplace(fd, std::move(f));
        notify();
    }

    void when_writeable(int fd, std::function<void()> f) {
        auto lock = std::unique_lock(_mutex);
        _writers.emplace(fd, std::move(f));
        notify();
    }
    
    void when_exceptional(int fd, std::function<void()> f) {
        auto lock = std::unique_lock(_mutex);
        _excepters.emplace(fd, std::move(f));
        notify();
    }
    
    template<typename Duration>
    void after(Duration&& t, std::function<void()> f) {
        auto lock = std::unique_lock{_mutex};
        _timers.emplace(std::chrono::steady_clock::now() + std::forward<Duration>(t), std::move(f));
        notify();
    }
    
    template<typename TimePoint>
    void when(TimePoint&& t, std::function<void()> f) {
        auto lock = std::unique_lock{_mutex};
        _timers.emplace(std::forward<TimePoint>(t), std::move(f));
        notify();
    }

    void run() {
        
        std::list<std::function<void()>> pending;
        auto now = std::chrono::steady_clock::now();
        auto lock = std::unique_lock(_mutex);

        while (!_cancelled) {
            
            fd_set readfds; FD_ZERO(&readfds);
            FD_SET(_pipe[0], &readfds);
            int nfds = _pipe[0];
            for (auto& p : _readers) {
                FD_SET(p.first, &readfds);
                nfds = std::max(nfds, p.first);
            }
                        
            fd_set writefds;
            fd_set* pwritefds = nullptr;
            if (!_writers.empty()) {
                FD_ZERO(&writefds);
                pwritefds = &writefds;
                for (auto& p : _writers) {
                    FD_SET(p.first, &writefds);
                    nfds = std::max(nfds, p.first);
                }
            }

            fd_set exceptfds;
            fd_set* pexceptfds = nullptr;
            if (!_excepters.empty()) {
                FD_ZERO(&exceptfds);
                pexceptfds = &exceptfds;
                for (auto& p : _excepters) {
                    FD_SET(p.first, &exceptfds);
                    nfds = std::max(nfds, p.first);
                }
            }

            timeval* timeout = nullptr;
            timeval t0{0, 0};
            if (!_timers.empty()) {
                timeout = &t0;
                if (_timers.top().first > now) {
                    auto d = std::chrono::duration_cast<std::chrono::microseconds>(_timers.top().first - now);
                    t0.tv_usec = (int) (d.count() % 1'000'000);
                    t0.tv_sec = d.count() / 1'000'000;
                }
            }
            
            lock.unlock();
            
            if (!pending.empty())
                pool::submit_many(std::move(pending));
                        
            auto r = select(nfds + 1, &readfds, pwritefds, nullptr, timeout);
            if (r == -1)
                perror(strerror(errno));
                        
            lock.lock();
            
            if (_cancelled)
                break;
                        
            if (FD_ISSET(_pipe[0], &readfds)) {
                constexpr int N = 256;
                unsigned char buf[N];
                ssize_t r = read(_pipe[0], buf, N);
                // printf("(cleared %td notifications)\n", r);
                assert(r > 0);
            }
                        
            for (auto i = _readers.begin(); i != _readers.end(); ) {
                if (FD_ISSET(i->first, &readfds)) {
                    pending.push_back(std::move(i->second));
                    i = _readers.erase(i);
                } else {
                    ++i;
                }
            }

            for (auto i = _writers.begin(); i != _writers.end(); ) {
                if (FD_ISSET(i->first, &writefds)) {
                    pending.push_back(std::move(i->second));
                    i = _writers.erase(i);
                } else {
                    ++i;
                }
            }
            
            for (auto i = _excepters.begin(); i != _excepters.end(); ) {
                if (FD_ISSET(i->first, &exceptfds)) {
                    pending.push_back(std::move(i->second));
                    i = _excepters.erase(i);
                } else {
                    ++i;
                }
            }
            
            now = std::chrono::steady_clock::now();
            
            while (!_timers.empty() && _timers.top().first <= now) {
                pending.push_back(std::move(_timers.top().second));
                _timers.pop();
            }
            
        } // while(!_cancelled)
        
    } // run
    
    static reactor& get() {
        static reactor r;
        return r;
    }
    
    void cancel() {
        auto lock = std::unique_lock(_mutex);
        _cancelled = true;
        notify();
    }
    
    ~reactor() {
        cancel();
        _thread.join();
        close(_pipe[1]);
        close(_pipe[0]);
    }
    
};

#endif /* reactor_hpp */
