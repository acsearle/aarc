//
//  dual.cpp
//  aarc
//
//  Created by Antony Searle on 13/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include <iostream>
#include <thread>
#include <deque>

#include "atomic.hpp"
#include "dual.hpp"
#include "fn.hpp"
#include "y.hpp"

#include "catch.hpp"

// a lock-free dual atomic data structure that is either a queue of tasks,
// a stack of waiters, or empty
//
// when a task is pushed, it is matched with the youngest waiter, or enqueued
// if there are no waiters.  when a thread pops, it is matched with the oldest
// task, or becomes the youngest waiter
//
// tasks are handled in order, and that order is multi-thread well-defined
//
// if task submission is delayed until the end of the current job (as in
// asio::dispatch), we can gain efficiency

struct dual {
    
    static constexpr u64 CNT = detail::CNT;
    static constexpr u64 PTR = detail::PTR;
    static constexpr u64 TAG = detail::TAG;
    static constexpr u64 INC = detail::INC;
    static constexpr u64 LOW = 0x0000'0000'0000'FFFF;

    inline thread_local static std::deque<fn<void()>> _continuations;
    
    // extract pointer
    static detail::node<void()> const* ptr(u64 x) {
        assert(x & PTR);
        return reinterpret_cast<detail::node<void()> const*>(x & PTR);
    }
    
    static detail::node<void()>* mptr(u64 x) {
        assert(x & PTR);
        return reinterpret_cast<detail::node<void()>*>(x & PTR);
    }
    
    // extract local count
    static u64 cnt(u64 x) { return (x >> 48) + 1; }
    
    // check for pointer-tag equality, ignoring counter
    static bool cmp(u64 a, u64 b) { return !((a ^ b) & ~CNT); }
    
    alignas(64) atomic<u64> _head;
    alignas(64) atomic<u64> _tail;
            
    dual() {
        auto p = new detail::node<void()>;
        p->_next = 0;
        p->_count = 0x0000'0000'00002'0000;
        auto v = CNT | reinterpret_cast<u64>(p);
        _head = v;
        _tail = v;
    }
    
    dual(dual const&) = delete;
    
    /*
    dual(dual&& other)
    : _head{std::exchange(other._head, 0)}
    , _tail{std::exchange(other._tail, 0)} {
    }
     */
    
    ~dual() {
        // no calls to push, pop etc. are active but it is possible that the
        // nodes are still retained elswehere so we must destroy them properly
        
        // nodes that are retained aren't permitted to mutate _next (including
        // reusing the nodes in another container) unless they have established
        // unique ownership, i.e.
        //     ptr(a)->_count.load(memory_order_acquire) == cnt(a)
        
        // advance tail
        u64 a, b;
        for (;;) {
            a = _tail;
            b = mptr(a)->_next;
            if (!(b & ~TAG) || (b & TAG))
                break;
            _tail = b;
            ptr(a)->release(cnt(a));
            ptr(b)->release(1); // <-- coalesce this somehow?
        }
        // advance head
        for (;;) {
            a = _head;
            b = mptr(a)->_next;
            if (!(b & ~TAG) || (b & TAG))
                break;
            _head = b;
            ptr(a)->release(cnt(a));
            ptr(b)->erase_and_release(1);
        }
        // drain stack
        while ((a = mptr(_tail)->_next)) {
            mptr(_tail)->_next = mptr(mptr(_tail)->_next)->_next;
            ptr(a)->release(cnt(a));
        }
        assert(ptr(_head) == ptr(_tail));
        ptr(_head)->release(cnt(_head) + cnt(_tail));
    }
    
    
    // bitwise idioms:
    //
    //               p & PTR <=> ptr(p) != nullptr
    //           p ^ q & PTR <=> ptr(p) != ptr(q)
    //     p & (p - 1) & CNT <=> cnt(p) == 2^n + 1
    //              p &  CNT <=> cnt(p) > 1
    //              p & ~CNT <=> cnt(p & ~CNT) == 1
    //              p |  CNT <=> cnt(p |  CNT) == 0x1'0000
    //              p -  INC <=> cnt(p -  INC) == cnt(p) - 1

    static std::pair<u64, u64> _acquire(atomic<u64> const& p, u64 expected) {
        for (;;) {
            assert(expected & PTR); // <-- nonnull pointer bits
            if (__builtin_expect(expected & CNT, true)) { // <-- nonzero counter bits
                u64 desired = expected - INC;
                if (p.compare_exchange_weak(expected, desired, std::memory_order_acquire, std::memory_order_relaxed)) {
                    if (__builtin_expect(expected & desired & CNT, true)) {
                        return {desired, 1}; // <-- fast path completes
                    } else { // <-- counter is a power of two
                        expected = desired;
                        ptr(expected)->_count.fetch_add(LOW, std::memory_order_relaxed);
                        do if (p.compare_exchange_weak(expected, desired = expected | CNT, std::memory_order_release, std::memory_order_relaxed)) {
                            if (__builtin_expect((expected & CNT) == 0, false)) // <-- we fixed an exhausted counter
                                p.notify_all();        // <-- notify potential waiters
                            return{desired, cnt(expected)};
                        } while (!((expected ^ desired) & PTR)); // <-- while the pointer bits are unchanged
                        ptr(desired)->release(1 + LOW); // <-- start over
                    }
                }
            } else { // <-- the counter is zero
                p.wait(expected, std::memory_order_relaxed); // <-- until counter may have changed
                expected = p.load(std::memory_order_relaxed);
            }
        }
    }
    
    static std::pair<u64, u64> _acquire_specific(atomic<u64> const& p, u64 const specific) {
        assert(specific & PTR);
        u64 expected = specific;
        do {
            if (__builtin_expect(expected & CNT, true)) {
                u64 desired = expected - INC;
                if (p.compare_exchange_weak(expected, desired, std::memory_order_acquire, std::memory_order_relaxed)) {
                    if (__builtin_expect(expected & desired & CNT, true)) {
                        assert(!((desired ^ specific) & PTR));
                        return {desired, 1}; // <-- fast path completes
                    } else { // <-- count is a power of two, perform housekeeping
                        expected = desired;
                        ptr(specific)->_count.fetch_add(LOW, std::memory_order_relaxed);
                        do if (p.compare_exchange_weak(expected, desired = expected | CNT, std::memory_order_release, std::memory_order_relaxed)) {
                            if (__builtin_expect((expected & CNT) == 0, false)) // <-- we fixed an exhausted counter
                                p.notify_all();        // <-- notify potential waiters
                            assert(!((desired ^ specific) & PTR));
                            return{desired, cnt(expected)};
                        } while (!((expected ^ specific) & PTR)); // <-- while the pointer bits are unchanged
                        ptr(specific)->release(1 + LOW); // <-- give up
                    }
                }
            } else {
                p.wait(expected, std::memory_order_relaxed);
                expected = p.load(std::memory_order_relaxed);
            }
        } while (!((expected ^ specific) & PTR));
        return {expected, 0};
    }
    
    
    
    u64 _pop_promise_or_push_item(u64 z) const {
        
        if (z) {
            
            assert(!(z & ~PTR));

            mptr(z)->_next = 0;
            mptr(z)->_promise = 0;
            mptr(z)->_count = 0x0000'0000'0002'0000;
            z |= 0xFFFE'0000'0000'0000;
            assert(mptr(z)->_count == 2 * cnt(z) + 2);
            
            // over the lifetime of the node,
            //
            //           weight    FFFF is assigned to _tail
            //           weight       1 is assigned to the thread that writes it to _tail
            //           weight    FFFF is assigned to _head
            //           weight       1 is assigned to the thread that writes it to _head
            //                    -----
            //     total weight   20000 is written to the count
            //     local weight-1  FFFE is written to the handle
            //
            // a thread that does not want to retain the node after writing it
            // to _head or _tail (as when eagerly advancing _tail) can add its
            // weight to the write without overflowing the counter

        }
                
        u64 a; // <-- old value of _tail
        u64 b; // <-- new value of _tail
        u64 c; // <-- old value of _tail->_next
        u64 d; // <-- new value of _tail->_next
        u64 e; // <-- old value of _tail->_next->_next
        
        u64 m = 0; // <-- how much of _tail we own
        u64 n = 0;
    
    _load_tail:
        a = _tail.load(std::memory_order_relaxed);
    _acquire_tail:
        assert(m == 0);
        std::tie(b, m) = _acquire(_tail, a);
        a = b;
    _load_next:
        assert(m > 0);
        c = ptr(a)->_next.load(std::memory_order_acquire);
    _classify_next:
        if (c == 0)     // <-- end of queue
            goto _push;
        if (!(c & TAG)) // <-- queue node
            goto _swing_tail;
        if (c & TAG)    // <-- stack node
            goto _acquire_next;
        __builtin_trap();
        
        
    _push: // add the new node (or, if there is no new node, we failed to try_pop a stack node)
        if (z && !ptr(a)->_next.compare_exchange_strong(c, z, std::memory_order_acq_rel, std::memory_order_relaxed))
            goto _classify_next;
        ptr(a)->release(m);
        assert(n == 0);
        return 0;
        
        
    _swing_tail: // move stale tail forwards
        if (!_tail.compare_exchange_weak(b, c, std::memory_order_release, std::memory_order_relaxed))
            goto _swing_tail_failed;
        if (__builtin_expect(!(b & CNT) && (c & CNT), false))  // <-- we happened to fix a counter
            _tail.notify_all();
        ptr(a)->release(m + cnt(b));
        a = c;
        b = c;
        m = 1;
        goto _load_next;

    _swing_tail_failed:
        if ((a ^ b) & PTR)
            goto _swing_tail_failed_due_to_pointer_change;
        goto _swing_tail;

    _swing_tail_failed_due_to_pointer_change:
        ptr(a)->release(m);
        m = 0;
        a = b;
        goto _acquire_tail;
        
        
    _acquire_next: // pop stack node
        assert(m);
        std::tie(c, n) = _acquire_specific(ptr(a)->_next, c);
        if (n == 0)
            goto _classify_next;
        e = ptr(c)->_next.load(std::memory_order_relaxed);
    _pop_next:
        if (!ptr(a)->_next.compare_exchange_weak(d = c, e, std::memory_order_acquire, std::memory_order_relaxed))
            goto _pop_next_failed;
        ptr(a)->release(m);
        assert(c + (n << 48) > c);
        return c + (n << 48);
    
    _pop_next_failed:
        if ((c ^ d) & PTR)
            goto _pop_next_failed_due_to_pointer_change;
        goto _pop_next;
        
    _pop_next_failed_due_to_pointer_change:
        ptr(std::exchange(c, d))->release(std::exchange(n, 0));
        goto _classify_next;
        
    };
    
    [[nodiscard]] u64 _pop_item_or_push_promise(u64 z = 0) const {
        
        if (z) {
            assert(!(z & ~PTR));
            mptr(z)->_next = 0;
            mptr(z)->_count = 0x0000'0001'0000;
            mptr(z)->_promise = 0;
            z |= 0xFFFE'0000'0000'0000;
            assert(mptr(z)->_count == cnt(z) + 1); // <-- submitter retains 1
        }

        u64  a; // <-- old value of _head
        u64  b; // <-- new value of _head
        u64  c; // <-- old value of _head->_next
        
        u64 m = 0;
        
    _load_head:
        a = _head.load(std::memory_order_relaxed);
    _acquire_head:
        assert(m == 0);
        std::tie(b, m) = _acquire(_head, a);
        a = b;
    _load_next:
        assert(a & PTR);
        c = ptr(a)->_next.load(std::memory_order_acquire);
    _classify_next:
        assert(((c & PTR) && !(c & TAG)) == ((c & ~PTR) == 0xFFFE'0000'0000'0000));
        if ((c & PTR) && !(c & TAG))
            goto _swing_head;
    _push: // <-- update _head->_next to push a stack node (or, if there is no stack node provided, try_pop has failed)
        if (z) {
            z = (z & ~TAG) | ((c & TAG) < TAG ? (c & TAG) + 1 : TAG); // <-- use tag bits to track stack depth why not
            assert(z & TAG);
            mptr(z)->_next = c;
            if (!ptr(a)->_next.compare_exchange_strong(c, z, std::memory_order_acq_rel, std::memory_order_relaxed))
                goto _classify_next;
        }
        ptr(a)->release(m);
        m = 0;
        return 0;
                        
    _swing_head: // <-- advance head to claim a queue node
        if (!_head.compare_exchange_weak(b, c, std::memory_order_release, std::memory_order_relaxed))
            goto _swing_head_failed;
        if (__builtin_expect(!(b & CNT) && (c & CNT), false)) // <-- we happened to fix a counter
            _head.notify_all();
        ptr(a)->release(cnt(b) + m);
        return (c & ~CNT);
        
    _swing_head_failed:
        if ((a ^ b) & PTR)
            goto _swing_head_failed_due_to_pointer_change;
        goto _swing_head;
        
    _swing_head_failed_due_to_pointer_change:
        ptr(a)->release(m);
        m = 0;
        a = b;
        goto _acquire_head;
        
    }
    
    // try_push fails if no threads are waiting
    bool try_push(fn<void()>& x) const {
        assert(x._value & PTR);
        u64 waiter = _pop_promise_or_push_item(0); // aka try_pop_waiter
        if (waiter) {
            assert(waiter & PTR);
            [[maybe_unused]] u64 n = waiter & TAG; // <-- there were n waiters (saturating count)
            ptr(waiter)->_promise.store(std::exchange(x._value, 0), std::memory_order_release);
            ptr(waiter)->_promise.notify_one();
            ptr(waiter)->release(cnt(waiter));
        }
        return (bool) waiter;
    }
    
    void push(fn<void()> x) const {
        assert(x._value & PTR);
        u64 waiter = _pop_promise_or_push_item(x._value);
        if (waiter) {
            assert(waiter & PTR);
            [[maybe_unused]] u64 n = waiter & TAG; // <-- there were n waiters (saturating count)
            ptr(waiter)->_promise.store(std::exchange(x._value, 0), std::memory_order_release);
            ptr(waiter)->_promise.notify_one();
            ptr(waiter)->release(cnt(waiter));
        } else {
            x._value = 0; // <-- we gave up ownership
        }
    }
    
    // if result is nonzero it MUST be erased and released
    //
    //    if (u64 task = x.try_pop())
    //        mptr(task)->mut_call_and_erase_and_release
    //
    [[nodiscard]] u64 try_pop() const {
        return _pop_item_or_push_promise(0);
    }
    
    bool try_pop_and_call() const {
        u64 task = _pop_item_or_push_promise(0);
        if (task) {
            assert(task & PTR);
            mptr(task)->mut_call_and_erase_and_release(cnt(task));
        }
        return task;
    }
    
    void pop_and_call() const {
        // a node containing a promise
        std::unique_ptr<detail::node<void()>> promise{new detail::node<void()>};
        u64 task = _pop_item_or_push_promise((u64) promise.get());
        if (task) {
            mptr(task)->mut_call_and_erase_and_release(cnt(task));
        } else {
            detail::node<void()> const* ptr = promise.release(); // <-- now managed by queue
            ptr->_promise.wait(0, std::memory_order_relaxed);
            task = ptr->_promise.load(std::memory_order_acquire);
            ptr->release(1);
            assert(task);
            mptr(task)->mut_call_and_erase_and_delete();
        }
    }
    
    [[noreturn]] void pop_and_call_forever() const {
        // a node containing a promise
        std::unique_ptr<detail::node<void()>> promise{new detail::node<void()>};
        for (;;) {
            u64 task = _pop_item_or_push_promise((u64) promise.get());
            if (task) {
                mptr(task)->mut_call_and_erase_and_release(cnt(task));
            } else {
                detail::node<void()> const* ptr = promise.release(); // <-- now managed by queue
                promise.reset(new detail::node<void()>);
                ptr->_promise.wait(0, std::memory_order_relaxed);
                task = ptr->_promise.load(std::memory_order_acquire);
                ptr->release(1);
                assert(task);
                mptr(task)->mut_call_and_erase_and_delete();
            }
        }
    }

    [[noreturn]] void pop_and_call_forever_with_dispatch() const {
        std::unique_ptr<detail::node<void()>> promise{new detail::node<void()>};
        for (;;) {
            while (!_continuations.empty()) {
                while (_continuations.size() > 1) {
                    push(std::move(_continuations.front()));
                    _continuations.pop_front();
                }
                assert(_continuations.size() == 1);
                if (u64 f = try_pop()) {
                    push(std::move(_continuations.front()));
                    _continuations.pop_front();
                    mptr(f)->mut_call_and_erase_and_release(cnt(f));
                } else {
                    fn<void()> g = std::move(_continuations.front());
                    _continuations.pop_front();
                    g();
                }
            }
            assert(_continuations.empty() && promise);
            u64 f = _pop_item_or_push_promise((u64) promise.get());
            if (f) {
                mptr(f)->mut_call_and_erase_and_release(cnt(f));
            } else {
                detail::node<void()> const* ptr = promise.release(); // <-- now managed by queue
                ptr->_promise.wait(0, std::memory_order_relaxed);
                u64 g = ptr->_promise.load(std::memory_order_acquire);
                ptr->release(1);
                mptr(g)->mut_call_and_erase_and_delete();
                promise.reset(new detail::node<void()>);
            }
        }
    }

};


TEST_CASE("dual", "[dual]") {
    printf("extant: %llu\n", detail::node<void()>::_extant.load(std::memory_order_relaxed));

    {
    int z = 0;
    
    dual a;
    std::thread b;
    a.push([&] { z = 1; });
    REQUIRE(z == 0);
    a.pop_and_call();
    REQUIRE(z == 1);
    b = std::thread([&] { a.pop_and_call(); });
    a.push([&] { z = 2; });
    a.push([&] { z = 3; });
    b.join();
    REQUIRE(z == 2);
    a.pop_and_call();
    REQUIRE(z == 3);
    }
    printf("extant: %llu\n", detail::node<void()>::_extant.load(std::memory_order_relaxed));

}

TEST_CASE("dual-multi", "[dual]") {
    
    printf("extant: %llu\n", detail::node<void()>::_extant.load(std::memory_order_relaxed));
    {
    dual d;
    auto n = std::thread::hardware_concurrency();
    std::vector<std::thread> t;
    const atomic<u64> a{0};
    
    // make some workers
    for (decltype(n) i = 0; i != n; ++i) {
        t.emplace_back([&] {
            try {
                d.pop_and_call_forever();
            } catch (...) {
                // interpret any exceptions as end worker
            }
        });
    }
    
    // submit tasks to each flip one bit of a u64
    for (int i = 0; i != 64; ++i) {
        d.push([&a, i] {
            a.fetch_xor(1ull << i, std::memory_order_relaxed);
            a.notify_one();
        });
    }
    // wait until all bits are set (which requires all jobs to have run once or
    // an odd number of times)
    while (auto b = ~a.load(std::memory_order_relaxed)) {
        a.wait(~b, std::memory_order_relaxed);
    }
    REQUIRE(true);

    // submit kill jobs
    for (decltype(n) i = 0; i != n; ++i) {
        d.push([] { throw 0; });
    }
    
    // join threads
    while (!t.empty()) {
        t.back().join();
        t.pop_back();
    }
    }
    printf("extant: %llu\n", detail::node<void()>::_extant.load(std::memory_order_relaxed));
    
}

TEST_CASE("dual-exhaust", "[dual]") {
    {
        printf("extant: %llu\n", detail::node<void()>::_extant.load(std::memory_order_relaxed));
        
        dual d;
        auto n = std::thread::hardware_concurrency();
        std::vector<std::thread> t;
        const atomic<u64> a{0};
        
        // make some workers
        for (decltype(n) i = 0; i != n; ++i) {
            t.emplace_back([&] {
                try {
                    d.pop_and_call_forever_with_dispatch();
                } catch (...) {
                    // interpret exceptions as quit signal
                }
            });
        }
        
        
        std::atomic<u64> z{0};
        std::atomic<u64> y{0};
        
        y.fetch_add(1, std::memory_order_relaxed);
        d.push([&d, n, &z, &y] {
            for (decltype(n) i = 0; i != 8; ++i) {
                int gen = 0x0'FF;
                Y([&d, n, gen, &z, &y](auto& self) mutable -> void {
                    z.fetch_add(1, std::memory_order_relaxed);
                    if (gen) {
                        gen >>= 1;
                        y.fetch_add(1, std::memory_order_relaxed);
                        dual::_continuations.emplace_back(self);
                        y.fetch_add(1, std::memory_order_relaxed);
                        dual::_continuations.emplace_back(self);
                    } else {
                        // submit kill jobs
                        for (decltype(n) i = 0; i != n; ++i) {
                            y.fetch_add(1, std::memory_order_relaxed);
                            d.push([&z, &y] {
                                z.fetch_add(1, std::memory_order_relaxed);
                                ; throw 0; });
                        }
                    }
                })();
            }
        });
        
        // join threads
        while (!t.empty()) {
            t.back().join();
            t.pop_back();
        }
        printf("submitted %llu jobs\n", y.load(std::memory_order_relaxed));
        printf("executed  %llu jobs\n", z.load(std::memory_order_relaxed));
    }
    printf("extant: %llu\n", detail::node<void()>::_extant.load(std::memory_order_relaxed));
    
}

// try_push (fails if no waiter found)
// try_pop
// defer - to local queue, process last job oneself if try_pop fails (or if we
// know the queue is empty because pushing next-to-last-job returned a waiter
