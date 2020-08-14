//
//  dual.cpp
//  aarc
//
//  Created by Antony Searle on 13/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include <iostream>
#include <thread>

#include "atomic.hpp"
#include "dual.hpp"
#include "fn.hpp"

#include "catch.hpp"

// a lock-free dual atomic data structure that is either a queue of tasks,
// a stack of waiters, or empty
//
// when a task is pushed, it is matched with the youngest waiter, or enqueued
// if there are no waiters.  when a thread pops, it is matched with the oldest
// task, or becomes the youngest waiter

struct dual {
    
    static constexpr u64 CNT = detail::CNT;
    static constexpr u64 PTR = detail::PTR;
    static constexpr u64 TAG = detail::TAG;
    static constexpr u64 INC = detail::INC;
    static constexpr u64 LOW = 0x0000'0000'0000'FFFF;
    
    alignas(64) atomic<u64> _head;
    alignas(64) atomic<u64> _tail;
        
    // extract pointer
    static detail::node<void()> const* ptr(u64 x) { assert(x & PTR); return reinterpret_cast<detail::node<void()> const*>(x & PTR); }
    static detail::node<void()>* mptr(u64 x) { assert(x & PTR); return reinterpret_cast<detail::node<void()>*>(x & PTR); }
    
    // extract local count
    static u64 cnt(u64 x) { return (x >> 48) + 1; }
    
    // check for pointer-tag equality, ignoring counter
    static bool cmp(u64 a, u64 b) { return !((a ^ b) & ~CNT); }
    
    dual() {
        auto p = new detail::node<void()>;
        p->_next = 0;
        p->_count = 0x0000'0000'00002'0000;
        auto v = CNT | reinterpret_cast<u64>(p);
        _head = v;
        _tail = v;
    }
    
    u64 _push(u64 z) const {
        
        assert(mptr(z));
        assert(mptr(z)->_next == 0);
        assert(mptr(z)->_count == (cnt(z) + 1) * 2);
        
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
        assert(a & PTR); // <-- _tail is never null (points to sentinel if empty)
        if (__builtin_expect(!(a & CNT), false))
            goto _wait_tail;
        assert(m == 0);
        assert(n == 0);
        b = a - INC;
        if (!_tail.compare_exchange_weak(a, b, std::memory_order_acquire, std::memory_order_relaxed))
            goto _acquire_tail;
        m = 1;
        if (__builtin_expect(!(a & b & CNT), false))
            goto _replenish_tail;
    _load_next:
        assert(m > 0);
        c = ptr(a)->_next.load(std::memory_order_acquire);
    _classify_next:
        if (c == 0)                              // <-- end of queue
            goto _push;
        if (!(c & TAG)) // <-- queue node
            goto _swing_tail;
        if (c & TAG)                             // <-- stack node
            goto _acquire_next;
        __builtin_trap();
        
        
    _push: // add the new node
        if (!ptr(a)->_next.compare_exchange_strong(c, z, std::memory_order_acq_rel, std::memory_order_acquire))
            goto _classify_next;
        ptr(a)->release(m);
        assert(n == 0);
        return 0;
        
        
    _swing_tail: // move stale tail forwards
        if (!_tail.compare_exchange_weak(b, c, std::memory_order_release, std::memory_order_relaxed))
            goto _swing_tail_failed;
        if (__builtin_expect(!(b & CNT) && (c & CNT), false))  // <-- we happend to fix a counter
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
        assert(c & CNT);
        d = c - INC;
        assert(m);
        if (!ptr(a)->_next.compare_exchange_weak(c, d, std::memory_order_acquire, std::memory_order_relaxed))
            goto _classify_next;
        n = 1;
    _load_next_next:
        assert(n);
        e = ptr(c)->_next.load(std::memory_order_relaxed);
    _swing_next:
        if (!ptr(a)->_next.compare_exchange_weak(d, e, std::memory_order_acquire, std::memory_order_relaxed))
            goto _swing_next_failed;
        ptr(a)->release(m);
        assert(n == 1);
        assert(d + INC > d);
        return d + INC;
    
    _swing_next_failed:
        if ((c ^ d) & PTR)
            goto _swing_next_failed_due_to_pointer_change;
        goto _swing_next;
        
    _swing_next_failed_due_to_pointer_change:
        ptr(c)->release(n);
        n = 0;
        c = d;
        goto _classify_next;
        
    _wait_tail:
        // the local count has been exhausted and we can't proceed until another
        // thread either replenishes it or swings tail
        _tail.wait(a, std::memory_order_relaxed);
        goto _load_tail;
        
        
    _replenish_tail:
        // when the local count crosses a power of two boundary, we try to
        // replenish it from the global count
        ptr(a)->_count.fetch_add(LOW, std::memory_order_relaxed);
        m += LOW;
    _attempt_replenish_tail:
        if (!_tail.compare_exchange_weak(b, CNT | b, std::memory_order_release, std::memory_order_relaxed))
            goto _replenish_failed;
        m -= LOW - (b >> 48);
        if (!(b & CNT)) // <-- we fixed an exhausted counter
            _tail.notify_all();
        b |= CNT;
        goto _load_next;
    _replenish_failed:
        if ((a ^ b) & PTR)
            goto _attempt_replenish_tail;
    _replenish_failed_due_to_pointer_change:
        ptr(a)->release(m);
        m = 0;
        a = b;
        goto _acquire_tail;
        
        
    };
    
    u64 _pop(u64 z) const {
        
        assert(z & CNT);
        assert(z & PTR);
        assert(z & TAG);
        // check that we have kept some ownership of the node
        assert(mptr(z)->_count > cnt(z));

        u64  a; // <-- old value of _head
        u64  b; // <-- new value of _head
        u64& c  // <-- old value of _head->_next
            = mptr(z)->_next;
        
        u64 m = 0;
        
    _load_head:
        a = _head.load(std::memory_order_relaxed);
    _acquire_head:
        assert(a & PTR); // <-- head can never be null
        assert(m == 0);
        if (!(a & CNT))
            goto _wait_head;
        b = a - INC;
        if (!_head.compare_exchange_weak(a, b, std::memory_order_acquire, std::memory_order_relaxed))
            goto _acquire_head;
        m = 1;
        if (!(a & b & CNT))
            goto _replenish_head;
    _load_next:
        assert(a & PTR);
        c = ptr(a)->_next.load(std::memory_order_acquire);
    _classify_next:
        if ((c & ~PTR) == 0xFFFE'0000'0000'0000)
            goto _swing_head;
    _push: // <-- update _head->_next to push a stack node
        if (!ptr(a)->_next.compare_exchange_strong(c, z, std::memory_order_acq_rel, std::memory_order_acquire))
            goto _classify_next;
        ptr(a)->release(m);
        m = 0;
        return 0;
                        
    _swing_head: // <-- advance head to claim a queue node
        if (!_head.compare_exchange_weak(b, c, std::memory_order_release, std::memory_order_relaxed))
            goto _swing_head_failed;
        if (__builtin_expect(!(b & CNT) && (c & CNT), false)) // <-- we happend to fix a counter
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

        
    _wait_head:
        // the local count has been exhausted and we can't proceed until another
        // thread either replenishes it or replaces _head
        _head.wait(a, std::memory_order_relaxed);
        goto _load_head;

        
    _replenish_head:
        // when the local count crosses a power of two boundary, we try to
        // replenish it from the global count
        ptr(a)->_count.fetch_add(LOW, std::memory_order_relaxed);
        m += LOW;
    _attempt_replenish_head:
        if (!_head.compare_exchange_weak(b, CNT | b, std::memory_order_release, std::memory_order_relaxed))
            goto _replenish_failed;
        m -= LOW - (b >> 48);
        if (!(b & CNT)) // <-- we fixed an exhausted counter
            _head.notify_all();
        b |= CNT;
        goto _load_next;
    
    _replenish_failed:
        if (cmp(a, b))
            goto _attempt_replenish_head;
    _replenish_failed_due_to_pointer_or_tag_change:
        ptr(a)->release(m);
        m = 0;
        a = b;
        goto _acquire_head;
        
    }
    
    
    void push(fn<void()> x) const {

        // over the lifetime of the queue node,
        //     weight 0xFFFF is placed in _tail
        //     weight 0x0001 is awarded to the placer
        //     weight 0xFFFF is placed in _head
        //     weight 0x0001 is awarded to the placer
        
        assert(!(x._value & ~PTR));
        x._value &= PTR;
        assert(x);
        x._value |= 0xFFFE'0000'0000'0000;
        x->_next = 0;
        x->_count = 0x0000'0000'0002'0000;
        x->_promise = 0;
        u64 c = _push(x._value);
        if (c) {
            //assert(ptr(c)->_promise.load(std::memory_order_relaxed) == 0);
            ptr(c)->_promise.store(x._value, std::memory_order_release);
            ptr(c)->_promise.notify_one();
            ptr(c)->release(cnt(c));
        }
        x._value = 0;
    }
    
    void pop() const {
        auto a = new detail::node<void()>;
        a->_next = 0;
        a->_count = 0x0000'0000'0001'0001;
        a->_promise = 0;
        u64 b = reinterpret_cast<u64>(a) | 0xFFFF'0000'0000'0001;
        assert(a->_count == cnt(b) + 1);
        u64 c = _pop(b);
        if (c) {
            delete ptr(b);
            mptr(c)->mut_call_and_erase_and_release(cnt(c));
        } else {
            ptr(b)->_promise.wait(0, std::memory_order_relaxed);
            c = ptr(b)->_promise.load(std::memory_order_acquire);
            assert(c & PTR);
            ptr(b)->release(1);
            mptr(c)->mut_call_and_erase_and_delete();
        }
    }
    
    [[noreturn]] void pop_forever() const {
                               
        std::unique_ptr<detail::node<void()>> // <-- for exception safety
            a;                 // <-- the node containing our promise
        u64 b;                 // <-- the counted ptr to same
        u64 c;                 // <-- the counted ptr to a task we popped
        
    _construct_promise:
        a.reset(new detail::node<void()>);
        a->_next = 0;
        a->_count = 0x0000'0000'0001'0001;
        a->_promise = 0;
        b = reinterpret_cast<u64>(a.get());
        b |= 0xFFFF'0000'0000'0001;
        assert(a->_count == cnt(b) + 1);
    _submit_promise:
        c = _pop(b);
        if (!c)
            goto _wait_on_promise;
        mptr(c)->mut_call_and_erase_and_release(cnt(c));
        goto _submit_promise;
        
    _wait_on_promise:
        /* yesdiscard */ a.release();
        ptr(b)->_promise.wait(0, std::memory_order_relaxed);
        c = ptr(b)->_promise.load(std::memory_order_acquire);
        assert(c);
        ptr(b)->release(1);
        mptr(c)->mut_call_and_erase_and_delete();
        goto _construct_promise;
    }
    
    
    
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
            ptr(b)->release(1);
        }
        while ((a = mptr(_tail)->_next)) {
            mptr(_tail)->_next = mptr(mptr(_tail)->_next)->_next;
            ptr(a)->release(cnt(a));
        }
        assert(ptr(_head) == ptr(_tail));
        ptr(_head)->release(cnt(_head) + cnt(_tail));
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
    a.pop();
    REQUIRE(z == 1);
    b = std::thread([&] { a.pop(); });
    a.push([&] { z = 2; });
    a.push([&] { z = 3; });
    b.join();
    REQUIRE(z == 2);
    a.pop();
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
                d.pop_forever();
            } catch (...) {
                // interpret exceptions as quit signal
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


template<typename F>
struct _y_combinator {
    F _f;
    
    template<typename... Args>
    decltype(auto) operator()(Args&&... args) {
        return _f(*this, std::forward<Args>(args)...);
    }
    
};

template<typename F>
auto y_combinator(F&& f) {
    return _y_combinator<std::decay_t<F>>{std::forward<F>(f)};
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
                d.pop_forever();
            } catch (...) {
                // interpret exceptions as quit signal
            }
        });
    }
            
    for (decltype(n) i = 0; i != 8; ++i) {
        int gen = 0x1FFFFF;
        y_combinator([&d, n, gen](auto& self) mutable -> void {
            //std::cout << std::this_thread::get_id() << '\n';
            if (gen--)
                d.push(std::move(self));
            else
                // submit kill jobs
                for (decltype(n) i = 0; i != n; ++i) {
                    d.push([] { throw 0; });
                }
        })();
    }
    
    // join threads
    while (!t.empty()) {
        t.back().join();
        t.pop_back();
    }
    }
    printf("extant: %llu\n", detail::node<void()>::_extant.load(std::memory_order_relaxed));
    
}
