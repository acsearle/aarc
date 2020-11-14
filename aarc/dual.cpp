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
#include "stack.hpp"
#include "counted.hpp"

#include <catch2/catch.hpp>

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

using namespace aarc;

struct dual {
    
    inline thread_local static std::deque<fn<void()>> _continuations;
    
    alignas(64) mutable CountedPtr<detail::node<void()>> _head;
    alignas(64) mutable CountedPtr<detail::node<void()>> _tail;
            
    dual() {
        auto p = new detail::node<void()>;
        p->_next = 0;
        auto MAX = CountedPtr<detail::node<void()>>::MAX;
        p->_count = MAX * 2;
        _head = CountedPtr<detail::node<void()>>(MAX, p, 0);
        _tail = _head;
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
        CountedPtr<detail::node<void()>> a, b;
        for (;;) {
            a = _tail;
            b = a->_next;
            if (!b.ptr || b.tag)
                break;
            _tail = b;
            a->release(a.cnt);
            b->release(1); // <-- coalesce this somehow?
        }
        // advance head
        for (;;) {
            a = _head;
            b = a->_next;
            if (!b.ptr || b.tag)
                break;
            _head = b;
            a->release(a.cnt);
            b->erase_and_release(1);
        }
        // drain stack
        while ((a = _tail->_next).ptr) {
            _tail->_next = _tail->_next->_next;
            a->release(a.cnt);
        }
        assert(_head.ptr == _tail.ptr);
        _head->release(_head.cnt + _tail.cnt);
    }
    
    
    // bitwise idioms:
    //
    //               p & PTR <=> ptr(p) != nullptr
    //         (p ^ q) & PTR <=> ptr(p) != ptr(q)
    //     p & (p - 1) & CNT <=> cnt(p) == 2^n + 1
    //              p &  CNT <=> cnt(p) > 1
    //              p & ~CNT <=> cnt(p & ~CNT) == 1
    //              p |  CNT <=> cnt(p |  CNT) == 0x1'0000
    //              p -  INC <=> cnt(p -  INC) == cnt(p) - 1

    /*
    static std::pair<u64, u64> _acquire(u64& p, u64 expected) {
        for (;;) {
            assert(expected & PTR); // <-- nonnull pointer bits
            if (__builtin_expect(expected & CNT, true)) { // <-- nonzero counter bits
                u64 desired = expected - INC;
                if (atomic_compare_exchange_weak(&p, &expected, desired, std::memory_order_acquire, std::memory_order_relaxed)) {
                    if (__builtin_expect(expected & desired & CNT, true)) {
                        return {desired, 1}; // <-- fast path completes
                    } else { // <-- counter is a power of two
                        expected = desired;
                        atomic_fetch_add(&ptr(expected)->_count, LOW, std::memory_order_relaxed);
                        do if (atomic_compare_exchange_weak(&p, &expected, desired = expected | CNT, std::memory_order_release, std::memory_order_relaxed)) {
                            if (__builtin_expect((expected & CNT) == 0, false)) // <-- we fixed an exhausted counter
                                atomic_notify_all(&p);        // <-- notify potential waiters
                            return{desired, cnt(expected)};
                        } while (!((expected ^ desired) & PTR)); // <-- while the pointer bits are unchanged
                        ptr(desired)->release(1 + LOW); // <-- start over
                    }
                }
            } else { // <-- the counter is zero
                atomic_wait(&p, expected, std::memory_order_relaxed); // <-- until counter may have changed
                expected = atomic_load(&p, std::memory_order_relaxed);
            }
        }
    }
     */
    
    /*
    static std::pair<u64, u64> _acquire_specific(u64& p, u64 const specific) {
        assert(specific & PTR);
        u64 expected = specific;
        do {
            if (__builtin_expect(expected & CNT, true)) {
                u64 desired = expected - INC;
                if (atomic_compare_exchange_weak(&p, &expected, desired, std::memory_order_acquire, std::memory_order_relaxed)) {
                    if (__builtin_expect(expected & desired & CNT, true)) {
                        assert(!((desired ^ specific) & PTR));
                        return {desired, 1}; // <-- fast path completes
                    } else { // <-- count is a power of two, perform housekeeping
                        expected = desired;
                        atomic_fetch_add(&ptr(specific)->_count, LOW, std::memory_order_relaxed);
                        do if (atomic_compare_exchange_weak(&p, &expected, desired = expected | CNT, std::memory_order_release, std::memory_order_relaxed)) {
                            if (__builtin_expect((expected & CNT) == 0, false)) // <-- we fixed an exhausted counter
                                atomic_notify_all(&p);        // <-- notify potential waiters
                            assert(!((desired ^ specific) & PTR));
                            return{desired, cnt(expected)};
                        } while (!((expected ^ specific) & PTR)); // <-- while the pointer bits are unchanged
                        ptr(specific)->release(1 + LOW); // <-- give up
                    }
                }
            } else {
                atomic_wait(&p, expected, std::memory_order_relaxed);
                expected = atomic_load(&p, std::memory_order_relaxed);
            }
        } while (!((expected ^ specific) & PTR));
        return {expected, 0};
    }
    */
        
    CountedPtr<detail::node<void()>> _pop_promise_or_push_item(CountedPtr<detail::node<void()>> z) const {
        
        if (z.ptr) {
            
            assert(z.tag == 0);
            assert(z.cnt == 1);

            z->_next = 0;
            z->_promise = 0;
            auto MAX = CountedPtr<detail::node<void()>>::MAX;
            z->_count = MAX * 2;
            z.cnt = MAX - 1;
            assert(z->_count == 2 * z.cnt + 2);
            
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
        
        using P = CountedPtr<detail::node<void()>>;
        
        P a; // <-- old value of _tail
        P b; // <-- new value of _tail
        P c; // <-- old value of _tail->_next
        P d; // <-- new value of _tail->_next
        P e; // <-- old value of _tail->_next->_next
        
        u64 m = 0; // <-- how much of _tail we own
        u64 n = 0;
    
    _load_tail:
        a = atomic_load(&_tail, std::memory_order_relaxed);
    _acquire_tail:
        assert(m == 0);
        m = atomic_acquire(&_tail, &a);
        b = a;
    _load_next:
        assert(m > 0);
        c = atomic_load(&a->_next, std::memory_order_acquire);
    _classify_next:
        if (c.ptr == 0)     // <-- end of queue
            goto _push;
        else if (c.tag) // <-- stack node
            goto _acquire_next;
        else // <-- queue node
            goto _swing_tail;
        
    _push: // add the new node (or, if there is no new node, we failed to try_pop a stack node)
        if (z.ptr && !atomic_compare_exchange_strong(&a->_next,
                                                     &c,
                                                     z,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_relaxed))
            goto _classify_next;
        a->release(m);
        assert(n == 0);
        return 0;
        
        
    _swing_tail: // move stale tail forwards
        if (!atomic_compare_exchange_weak(&_tail, &b, c, std::memory_order_release, std::memory_order_relaxed))
            goto _swing_tail_failed;
        if (__builtin_expect((b.cnt == 1) && (c.cnt > 1), false))  // <-- we happened to fix a counter
            atomic_notify_all(&_tail);
        a->release(m + b.cnt);
        a = c;
        b = c;
        m = 1;
        goto _load_next;

    _swing_tail_failed:
        if (a.ptr != b.ptr)
            goto _swing_tail_failed_due_to_pointer_change;
        goto _swing_tail;

    _swing_tail_failed_due_to_pointer_change:
        a->release(m);
        m = 0;
        a = b;
        goto _acquire_tail;
        
        
    _acquire_next: // pop stack node
        assert(m);
        // std::tie(c, n) = _acquire_specific(ptr(a)->_next, c);
        n = atomic_compare_acquire_strong(&a->_next, &c);
        if (n == 0)
            goto _classify_next;
        e = atomic_load(&c->_next, std::memory_order_relaxed);
    _pop_next:
        d = c;
        if (!atomic_compare_exchange_weak(&a->_next,
                                          &d,
                                          e,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed))
            goto _pop_next_failed;
        a->release(m);
        assert(c.cnt + n <= P::MAX);
        c.cnt += n;
        return c;
    
    _pop_next_failed:
        if (c.ptr != d.ptr)
            goto _pop_next_failed_due_to_pointer_change;
        else
            goto _pop_next;
        
    _pop_next_failed_due_to_pointer_change:
        c->release(n);
        c = d;
        n = 0;
        goto _classify_next;
        
    };
    
    
    
    [[nodiscard]] CountedPtr<detail::node<void()>>
    _pop_item_or_push_promise(CountedPtr<detail::node<void()>> z = 0) const {
        
        using P = CountedPtr<detail::node<void()>>;
        
        if (z.ptr) {
            // assert(!(z & ~PTR));
            assert(z.tag == 0);
            assert(z.cnt == 1);
            z->_next = 0;
            z->_count = P::MAX;
            z->_promise = 0;
            z.cnt = P::MAX - 1;
            assert(z->_count == z.cnt + 1); // <-- submitter retains 1
        }

        P  a; // <-- old value of _head
        P  b; // <-- new value of _head
        P  c; // <-- old value of _head->_next
        
        u64 m = 0;
        
    _load_head:
        a = atomic_load(&_head, std::memory_order_relaxed);
    _acquire_head:
        assert(m == 0);
        m = atomic_acquire(&_head, &a);
        b = a;
    _load_next:
        assert(a.ptr);
        c = atomic_load(&a->_next, std::memory_order_acquire);
    _classify_next:
        //assert((c.ptr && !c.tag) == (c.cnt == P::MAX - 1));
        if (c.ptr && !c.tag)
            goto _swing_head;
    _push: // <-- update _head->_next to push a stack node (or, if there is no stack node provided, try_pop has failed)
        if (z.ptr) {
            z.tag = std::min<u64>(c.tag + 1, P::TAG); // <-- use tag bits to track stack depth why not
            assert(z.tag);
            z->_next = c;
            if (!atomic_compare_exchange_strong(&a->_next,
                                                &c,
                                                z,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed))
                goto _classify_next;
        }
        a->release(m);
        m = 0;
        return 0;
                        
    _swing_head: // <-- advance head to claim a queue node
        if (!atomic_compare_exchange_weak(&_head, &b, c, std::memory_order_release, std::memory_order_relaxed))
            goto _swing_head_failed;
        if (__builtin_expect((b.cnt == 1) && (c.cnt > 1), false)) // <-- we happened to fix a counter
            atomic_notify_all(&_head);
        a->release(b.cnt + m);
        c.cnt = 1;
        return c;
        
    _swing_head_failed:
        if (a.ptr != b.ptr)
            goto _swing_head_failed_due_to_pointer_change;
        goto _swing_head;
        
    _swing_head_failed_due_to_pointer_change:
        a->release(m);
        m = 0;
        a = b;
        goto _acquire_head;
        
    }
    
    // try_push fails if no threads are waiting
    bool try_push(fn<void()>& x) const {
        assert(x._value.ptr);
        CountedPtr<detail::node<void()>> waiter = _pop_promise_or_push_item(nullptr); // aka try_pop_waiter
        if (waiter.ptr) {
            assert(waiter.ptr);
            [[maybe_unused]] u64 n = waiter.tag; // <-- there were n waiters (saturating count)
            atomic_store(&waiter->_promise, std::exchange(x._value, nullptr), std::memory_order_release);
            atomic_notify_one(&waiter->_promise);
            waiter->release(waiter.cnt);
        }
        return (bool) waiter.ptr;
    }
    
    
    void push(fn<void()> x) const {
        assert(x._value.ptr);
        CountedPtr<detail::node<void()>> waiter = _pop_promise_or_push_item(x._value);
        if (waiter.ptr) {
            assert(waiter.ptr);
            [[maybe_unused]] u64 n = waiter.tag; // <-- there were n waiters (saturating count)
            atomic_store(&waiter->_promise, std::exchange(x._value, 0), std::memory_order_release);
            atomic_notify_one(&waiter->_promise);
            waiter->release(waiter.cnt);
        } else {
            x._value = 0; // <-- we gave up ownership
        }
    }
    
    // if result is nonzero it MUST be erased and released
    //
    //    if (u64 task = x.try_pop())
    //        mptr(task)->mut_call_and_erase_and_release
    //
    [[nodiscard]] CountedPtr<detail::node<void()>> try_pop() const {
        return _pop_item_or_push_promise(0);
    }
    
    bool try_pop_and_call() const {
        auto task = _pop_item_or_push_promise(nullptr);
        if (task) {
            assert(task.ptr);
            task->mut_call_and_erase_and_release(task.cnt);
        }
        return (bool) task;
    }
    
    void pop_and_call() const {
        // a node containing a promise
        std::unique_ptr<detail::node<void()>> promise{new detail::node<void()>};
        auto task = _pop_item_or_push_promise(promise.get());
        if (task) {
            task->mut_call_and_erase_and_release(task.cnt);
        } else {
            detail::node<void()> const* ptr = promise.release(); // <-- now managed by queue
            atomic_wait(&ptr->_promise, 0, std::memory_order_relaxed);
            task = atomic_load(&ptr->_promise, std::memory_order_acquire);
            ptr->release(1);
            assert(task);
            task->mut_call_and_erase_and_delete();
        }
    }
    
    [[noreturn]] void pop_and_call_forever() const {
        // a node containing a promise
        std::unique_ptr<detail::node<void()>> promise{new detail::node<void()>};
        for (;;) {
            auto task = _pop_item_or_push_promise(promise.get());
            if (task) {
                task->mut_call_and_erase_and_release(task.cnt);
            } else {
                detail::node<void()> const* ptr = promise.release(); // <-- now managed by queue
                promise.reset(new detail::node<void()>);
                atomic_wait(&ptr->_promise, 0, std::memory_order_relaxed);
                task = atomic_load(&ptr->_promise, std::memory_order_acquire);
                ptr->release(1);
                assert(task);
                task->mut_call_and_erase_and_delete();
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
                if (auto f = try_pop()) {
                    push(std::move(_continuations.front()));
                    _continuations.pop_front();
                    f->mut_call_and_erase_and_release(f.cnt);
                } else {
                    fn<void()> g = std::move(_continuations.front());
                    _continuations.pop_front();
                    g();
                }
            }
            assert(_continuations.empty() && promise);
            auto f = _pop_item_or_push_promise(promise.get());
            if (f) {
                f->mut_call_and_erase_and_release(f.cnt);
            } else {
                detail::node<void()> const* ptr = promise.release(); // <-- now managed by queue
                atomic_wait(&ptr->_promise, 0, std::memory_order_relaxed);
                auto g = atomic_load(&ptr->_promise, std::memory_order_acquire);
                ptr->release(1);
                g->mut_call_and_erase_and_delete();
                promise.reset(new detail::node<void()>);
            }
        }
    }

};



TEST_CASE("dual", "[dual]") {
    printf("extant: %llu\n", atomic_load(&detail::node<void()>::_extant, std::memory_order_relaxed));

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
    printf("extant: %llu\n", atomic_load(&detail::node<void()>::_extant, std::memory_order_relaxed));

}

TEST_CASE("dual-multi", "[dual]") {
    
    printf("extant: %llu\n", atomic_load(&detail::node<void()>::_extant, std::memory_order_relaxed));
    {
    dual d;
    auto n = std::thread::hardware_concurrency();
    std::vector<std::thread> t;
    u64 a{0};
    
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
            atomic_fetch_xor(&a, 1ull << i, std::memory_order_relaxed);
            atomic_notify_one(&a);
        });
    }
    // wait until all bits are set (which requires all jobs to have run once or
    // an odd number of times)
    while (auto b = ~atomic_load(&a, std::memory_order_relaxed)) {
        atomic_wait(&a, ~b, std::memory_order_relaxed);
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
    printf("extant: %llu\n", atomic_load(&detail::node<void()>::_extant, std::memory_order_relaxed));
    
}

TEST_CASE("dual-exhaust", "[dual]") {
    {
        printf("extant: %llu\n", atomic_load(&detail::node<void()>::_extant, std::memory_order_relaxed));
        
        dual d;
        auto n = std::thread::hardware_concurrency();
        std::vector<std::thread> t;
        u64 a{0};
        
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
                Y([&d, n, gen, &z, &y](auto& xxx) mutable -> void {
                    z.fetch_add(1, std::memory_order_relaxed);
                    if (gen) {
                        gen >>= 1;
                        y.fetch_add(1, std::memory_order_relaxed);
                        dual::_continuations.emplace_back(xxx);
                        y.fetch_add(1, std::memory_order_relaxed);
                        dual::_continuations.emplace_back(xxx);
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
    printf("extant: %llu\n", atomic_load(&detail::node<void()>::_extant, std::memory_order_relaxed));
    
}

// try_push (fails if no waiter found)
// try_pop
// defer - to local queue, process last job oneself if try_pop fails (or if we
// know the queue is empty because pushing next-to-last-job returned a waiter


struct pool_dual : dual {
    
    std::vector<std::thread> _threads;
    
    pool_dual() {
        auto n = std::thread::hardware_concurrency();
        std::vector<std::thread> t;
        for (decltype(n) i = 0; i != n; ++i) {
            _threads.emplace_back([this] {
                try {
                    pop_and_call_forever_with_dispatch();
                } catch (...) {
                    // no rethrow
                }
            });
        }
    }
    
    ~pool_dual() {
        // submit kill jobs
        for (std::size_t i = 0; i != _threads.size(); ++i)
            push([] { throw 0; });
        // join threads as they finish
        while (!_threads.empty()) {
            _threads.back().join();
            _threads.pop_back();
        }
    }
    
    static pool_dual const& _get() {
        static pool_dual p;
        return p;
    }
    
};

void pool_submit_one(fn<void()> f) {
    pool_dual::_get().push(std::move(f));
}

void pool_submit_many(stack<fn<void()>> s) {
    pool_dual const& r = pool_dual::_get();
    s.reverse();
    while (!s.empty()) {
        r.push(s.pop());
    }
}
