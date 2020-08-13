//
//  dual.cpp
//  aarc
//
//  Created by Antony Searle on 13/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include "atomic.hpp"
#include "dual.hpp"

#include "catch.hpp"

// a lock-free dual atomic data structure that is either a queue of tasks,
// a stack of waiters, or empty
//
// when a task is pushed, it is matched with the youngest waiter, or enqueued
// if there are no waiters.  when a thread pops, it is matched with the oldest
// task, or becomes the youngest waiter

struct dual {
    
    using u64 = std::uint64_t;
    
    static constexpr u64 CNT = 0xFFFF'0000'0000'0000;
    static constexpr u64 PTR = 0x0000'FFFF'FFFF'FFF0;
    static constexpr u64 TAG = 0x0000'0000'0000'000F;
    static constexpr u64 INC = 0x0001'0000'0000'0000;

    alignas(64) atomic<u64> _head;
    alignas(64) atomic<u64> _tail;
    
    struct node {
        
        atomic<u64> _next;
        atomic<u64> _count;
        int _payload;
        
        void release(u64 n) const {
            auto m = _count.fetch_sub(n, std::memory_order_release);
            assert(m >= n);
            if (m == n) {
                m = _count.load(std::memory_order_acquire);
                assert(m == 0);
                delete this;
            }
        }
        
    };
    
    static node const* ptr(u64 x) { assert(x & PTR); return reinterpret_cast<node const*>(x & PTR); }
    static node* mptr(u64 x) { assert(x & PTR); return reinterpret_cast<node*>(x & PTR); }
    static u64 cnt(u64 x) { return (x >> 48) + 1; }
    
    dual() : dual(CNT | reinterpret_cast<u64>(new node{0, 0x0000'0000'00002'0000})) {}
    explicit dual(u64 x) : _head(x), _tail(x) {}
    
    u64 _push(u64 z) const {
        
        assert((z & ~PTR) == 0xFFFE'0000'0000'0000);
        assert(ptr(z));
        assert(ptr(z)->_next.load(std::memory_order_relaxed) == 0);
        assert(ptr(z)->_count.load(std::memory_order_relaxed) == 0x2'0000);
        
        u64 a; // <-- old value of _tail
        u64 b; // <-- new value of _tail
        u64 c; // <-- old value of _tail->_next
        u64 d; // <-- new value of _tail->_next
        u64 e; // <-- old value of _tail->_next->_next
    
    _load_tail:
        a = _tail.load(std::memory_order_relaxed);
        
    _acquire_tail:
        assert(a & CNT);
        b = a - INC;
        if (!_tail.compare_exchange_weak(a, b, std::memory_order_acquire, std::memory_order_relaxed))
            goto _acquire_tail;
    
    _load_next:
        assert(b == a - INC);
        c = ptr(a)->_next.load(std::memory_order_acquire);
    _classify_next:
        if (c == 0)                              // <-- end of queue
            goto _push;
        if ((c & ~PTR) == 0xFFFE'0000'0000'0000) // <-- queue node
            goto _swing_tail;
        if (c & TAG)                             // <-- stack node
            goto _acquire_next;
        assert(!"invalid state");
        
        
    _push: // add the new node
        if (!ptr(a)->_next.compare_exchange_strong(c, z, std::memory_order_release, std::memory_order_acquire))
            goto _classify_next;
        // tail is now stale
        return 0;
        
        
    _swing_tail: // move stale tail forwards
        if (!_tail.compare_exchange_weak(b, c, std::memory_order_release, std::memory_order_relaxed))
            goto _swing_tail_failed;
        ptr(a)->release(cnt(a));
        a = c + INC;
        b = c;
        goto _load_next;

    _swing_tail_failed:
        if ((a ^ b) & ~CNT)
            goto _swing_tail_failed_due_to_pointer_or_tag_change;
        a = b + INC;
        assert(b < a);
        goto _swing_tail;

    _swing_tail_failed_due_to_pointer_or_tag_change:
        ptr(a)->release(1);
        a = b;
        goto _acquire_tail;
        
        
    _acquire_next: // pop stack node
        assert(c & CNT);
        d = c - INC;
        if (!ptr(a)->_next.compare_exchange_weak(c, d, std::memory_order_acquire, std::memory_order_acquire))
            goto _classify_next;
    _load_next_next:
        e = ptr(c)->_next.load(std::memory_order_acquire);
    _swing_next:
        if (!ptr(a)->_next.compare_exchange_weak(d, e, std::memory_order_release, std::memory_order_acquire))
            goto _swing_next_failed;
        ptr(a)->release(1);
        return c;
    
    _swing_next_failed:
        if ((c ^ d) & ~CNT)
            goto _swing_next_failed_due_to_pointer_or_tag_change;
        c = d + INC;
        assert(d < c);
        goto _swing_next;
        
    _swing_next_failed_due_to_pointer_or_tag_change:
        ptr(c)->release(1);
        c = d;
        goto _classify_next;
        
    };
    
    u64 _pop(u64 z) const {
        
        assert(z & CNT);
        assert(z & PTR);
        assert(z & TAG);
        assert(ptr(z)->_count.load(std::memory_order_relaxed) == ((z >> 48) + 1));
        
        u64  a; // <-- old value of _head
        u64  b; // <-- new value of _head
        u64& c  // <-- old value of _head->_next
            = mptr(z)->_next;
        
    _load_head:
        a = _head.load(std::memory_order_relaxed);
    _acquire_head:
        assert(a & CNT);
        b = a - INC;
        if (!_head.compare_exchange_weak(a, b, std::memory_order_acquire, std::memory_order_relaxed))
            goto _acquire_head;
    _load_next:
        assert(a & PTR);
        c = ptr(a)->_next.load(std::memory_order_acquire);
    _classify_next:
        if ((c & ~PTR) == 0xFFFE'0000'0000'0000)
            goto _swing_head;
    _push: // <-- update _head->_next to push a stack node
        if (!ptr(a)->_next.compare_exchange_strong(c, z, std::memory_order_release, std::memory_order_acquire))
            goto _classify_next;
        ptr(a)->release(1);
        return 0;
                        
    _swing_head: // <-- advance head to claim a queue node
        if (!_head.compare_exchange_weak(b, c, std::memory_order_release, std::memory_order_relaxed))
            goto _swing_head_failed;
        ptr(a)->release(cnt(a));
        return (c & ~CNT);
        
    _swing_head_failed:
        if ((a ^ b) & ~CNT)
            goto _swing_head_failed_due_to_pointer_or_tag_change;
        a = b + INC;
        assert(b < a);
        goto _swing_head;
        
    _swing_head_failed_due_to_pointer_or_tag_change:
        ptr(a)->release(1);
        a = b;
        goto _acquire_head;
        
    }
    
    
    void push(int x) const {

        // over the lifetime of the queue node,
        //     weight 0xFFFF is placed in _tail
        //     weight 0x0001 is awarded to the placer
        //     weight 0xFFFF is placed in _head
        //     weight 0x0001 is awarded to the placer
        
        auto a = new node{0, 0x0000'0000'0002'0000, x};
        u64 b = 0xFFFE'0000'0000'0000 | (u64) a;
        u64 c = _push(b);
        if (c) {
            // we popped a stack node instead
            delete ptr(b);
            ptr(c)->release(cnt(c));
        } else {
            // we pushed the queue node
        }
    }
    
    int pop() const {
        
        node* a = new node{0, 0x0000'0000'0001'0000, -1};
        u64 b = 0xFFFF'0000'0000'0001 | (u64) a;
        u64 c = _pop(b);
        if (c) {
            assert(cnt(c) == 1); // <-- the node is still the sentinel, we share it
            delete ptr(b);
            int z = ptr(c)->_payload;
            ptr(c)->release(cnt(c));
            return z;
        } else {
            // <-- we pushed the stack node
            return -2;
        }
    }
    
};


TEST_CASE("dual", "[dual]") {
    
    dual a;
    a.push( 7);
    REQUIRE(a.pop() ==  7);
    REQUIRE(a.pop() == -2); // <-- will consume 8
    a.push( 8);             // <-- matched with unfulfilled pop
    a.push( 9);
    REQUIRE(a.pop() ==  9);
    REQUIRE(a.pop() == -2); // <-- will consume 11
    REQUIRE(a.pop() == -2); // <-- will consume 10
    a.push(10);             // <-- matched with unfulfilled pop
    a.push(11);             // <-- matched with unfulfilled pop
    a.push(12);
    a.push(13);
    a.push(14);
    REQUIRE(a.pop() == 12);
    REQUIRE(a.pop() == 13);
    REQUIRE(a.pop() == 14);
    REQUIRE(a.pop() == -2); // <-- unfulfilled
    
}
