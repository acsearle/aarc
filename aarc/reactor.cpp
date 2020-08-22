//
//  reactor.cpp
//  aarc
//
//  Created by Antony Searle on 10/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include "reactor.hpp"

#include "catch.hpp"

reactor::reactor()
: _cancelled_and_notifications{0} {
    if (pipe(_pipe) != 0) {
        perror(strerror(errno));
        abort();
    }
    _thread = std::thread(&reactor::_run, this);
}

reactor::~reactor() {
    _cancel();
    _thread.join();
    close(_pipe[1]);
    close(_pipe[0]);
}

void reactor::_run() const {
    
    struct cmp_t {
        bool operator()(const fn<void()>& a,
                        const fn<void()>& b) {
            return a->_t > b->_t; // <-- reverse order puts earliest at top of priority queue
        }
    };
    
    stack<fn<void()>> readers;
    stack<fn<void()>> writers;
    stack<fn<void()>> excepters;
    std::priority_queue<fn<void()>, std::vector<fn<void()>>, cmp_t> timers;
    
    stack<fn<void()>> pending;
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
    
    std::uint64_t outstanding = 0; // <-- single-byte write notifications we have synchronized with but not yet cleared
    
    for (;;) {
        
        {
            // establish an ordering between this read and the writes that preceeded notifications
            auto old = _cancelled_and_notifications.fetch_and(CANCELLED_BIT,
                                                              std::memory_order_acquire);
            if (old & CANCELLED_BIT)
                break;
            outstanding += old;
            assert(outstanding >= old);
        }
        
        readers.splice(_readers_buf.take());
        writers.splice(_writers_buf.take());
        excepters.splice(_excepters_buf.take());
        
        {
            auto stale = _timers_buf.take();
            while (!stale.empty())
                timers.push(stale.pop());
        }
        
        if (count && FD_ISSET(_pipe[0], &readset)) {
            // we risk clearing notifications before we have observed their
            // effects, so only read as many notifications (bytes) as we
            // know have been sent
            assert(outstanding > 0); // <-- else why is pipe readable?
            buf.reserve(std::max<std::size_t>(outstanding, buf.capacity())); // <-- undefined behavior?
            ssize_t r = read(_pipe[0], buf.data(), outstanding);
            if (r < 0)
                (void) perror(strerror(errno)), abort();
            assert(r <= outstanding);
            outstanding -= r;
            //printf("outstanding notifications %llu\n", outstanding);
            --count;
        } else {
            FD_SET(_pipe[0], &readset);
        }
        int maxfd = _pipe[0];

        auto process = [&](stack<fn<void()>>& list, fd_set* set) -> fd_set* {
            for (auto i = list.begin(); i != list.end(); ) {
                int fd = i->_fd;
                if (count && FD_ISSET(fd, set)) {
                    FD_CLR(fd, set);
                    pending.push(list.erase(i));
                    --count;
                } else {
                    assert(!FD_ISSET(fd, set)); // <-- detects undercount
                    FD_SET(fd, set);
                    maxfd = std::max(maxfd, fd);
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
        
        while ((!timers.empty()) && (timers.top()->_t <= now)) {
            pending.push(std::move(const_cast<fn<void()>&>(timers.top())));
            timers.pop();
        }
        if (!timers.empty()) {
            auto usecs = std::chrono::duration_cast<std::chrono::
                microseconds>(timers.top()->_t - now).count();
            timeout.tv_usec = (int) (usecs % 1'000'000);
            timeout.tv_sec = usecs / 1'000'000;
            ptimeout = &timeout;
        } else {
            ptimeout = nullptr;
        }
                    
        if (!pending.empty())
            pool::submit_many(std::move(pending));

        count = select(maxfd + 1, &readset, pwriteset, pexceptset, ptimeout);

        if (count == -1)
            (void) perror(strerror(errno)), abort();
        
    }
    
}
