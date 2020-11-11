//
//  counted.cpp
//  aarc
//
//  Created by Antony Searle on 16/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include "counted.hpp"

#include <catch2/catch.hpp>

namespace aarc {
    
    static_assert(sizeof(CountedPtr<char>) == 8);
    static_assert(alignof(CountedPtr<int>) == 8);
    
    static_assert(CountedPtr<char>::TAG == 0);
    static_assert(CountedPtr<u64>::TAG == 7);
    
    // requirements for std::atomic
    static_assert(std::is_trivially_copyable<CountedPtr<u64>>::value);
    static_assert(std::is_copy_constructible<CountedPtr<u64>>::value);
    static_assert(std::is_move_constructible<CountedPtr<u64>>::value);
    static_assert(std::is_copy_assignable<CountedPtr<u64>>::value);
    static_assert(std::is_move_assignable<CountedPtr<u64>>::value);
    
    // is lock free
    static_assert(std::atomic<CountedPtr<u64>>::is_always_lock_free);
    
    template<typename T>
    char const* fmtb(T x) {
        thread_local char s[65] = { '0', 'b', 0};
        char* p = s + (sizeof(x) << 3);
        *p = 0;
        for (; p-- != s + 2; x >>= 1) {
            *p = x & 1 ? '1' : '0';
        }
        return s;
    }
    
    template<typename T>
    char const* fmtb(T* x) {
        return fmtb(reinterpret_cast<std::uintptr_t>(x));
    }
    
    
    template<typename T>
    bool healthy(CountedPtr<T> p) {
        using U = CountedPtr<T>;
        return (u64) p & ((u64) p + U::INC) & U::CNT;
    }
    
    
    // acquire shared ownership of the pointee of an atomic counted pointer,
    // whatever it may be
    //
    // on input, expected is a hint of the current value of target
    // on output, expected is the current value of target
    //
    // returns units of ownership gained.  these must be released.
    // return 0 if the pointer is null
    // after the call, expected is the current value
    // you must call release with the returned value NOT the value of expected.cnt
    // the returned value is not always the change in expected.cnt (replenish path)
    
    template<typename T>
    [[nodiscard]] u64 atomic_acquire(CountedPtr<T>* target,
                                     CountedPtr<T>* expected,
                                     std::memory_order failure = std::memory_order_relaxed) {
        assert(target)
        assert(expected);
        do if (auto n = atomic_compare_acquire_weak(target, expected, failure))
            return n;
        while (expected->ptr);
        return 0;
    }
    
    // attempt to acquire shared ownership of target if the pointer bits are as
    // expected; spurious failure is permitted
    
    template<typename T>
    [[nodiscard]] u64 atomic_compare_acquire_weak(CountedPtr<T>* target,
                                                  CountedPtr<T>* expected,
                                                  std::memory_order failure = std::memory_order_relaxed) {
        assert(target)
        assert(expected);
        using C = CountedPtr<T>;
        constexpr auto MAX = CountedPtr<T>::MAX;
        if (expected->ptr) {
            if (__builtin_expect(expected->cnt > 1, true)) {
                C desired = *expected - 1;
                if (atomic_compare_exchange_weak(target, expected,
                                            desired,
                                            std::memory_order_acquire,
                                            failure)) {
                    *expected = desired;
                    if (__builtin_expect(healthy(desired), true))
                        return 1; // <-- fast path completes
                    // <-- time to replenish the local count
                    (**expected).acquire(MAX - 1); // <-- get more weight from global count
                    do {
                        desired = *expected + (MAX - expected->cnt);
                        if (atomic_compare_exchange_weak(target,
                                                         expected,
                                                         desired,
                                                         std::memory_order_release,
                                                         failure)) {
                            if (__builtin_expect(expected->cnt == 1, false)) // <-- was entirely depleted with possible waiters ("impossible")
                                atomic_notify_all(&target);
                            using std::swap;
                            swap(*expected, desired);
                            return desired.cnt;
                        }
                    } while (expected->ptr == desired.ptr); // <-- while the pointer bits are unchanged
                    (*desired).release(MAX); // <-- pointer changed under us
                }
                return 0; // <-- quit after one try
            } else {
                atomic_wait(target, *expected, failure); // <-- if we don't wait here the caller becomes a spinlock which is worse?
            }
        }
        expected = atomic_load(target, failure); // <-- meet the failure requirements though we did not call compare_exchange
        return 0;
    }
    
    
    // acquire shared ownership of the target if it the pointer bits are as
    // expected
    
    template<typename T>
    [[nodiscard]] u64 atomic_compare_acquire_strong(CountedPtr<T>* target,
                                                    CountedPtr<T>* expected,
                                                    std::memory_order failure = std::memory_order_relaxed) {
        assert(target);
        assert(expected);
        using C = CountedPtr<T>;
        C desired = *expected;
        if (!expected->ptr) {
            *expected = atomic_load(target, failure);
            return 0;
        }
        while (expected->ptr && (expected->ptr == desired.ptr)) {
            if (__builtin_expect(expected->cnt > 1, true)) {
                desired = *expected - 1;
                if (atomic_compare_exchange_weak(target,
                                                 expected,
                                                 desired,
                                                 std::memory_order_acquire,
                                                 failure)) {
                    *expected = desired;
                    if (__builtin_expect(healthy(desired), true))
                        return 1; // <-- fast path completes
                    // <-- count is a power of two, perform housekeeping
                    (**expected).acquire(C::MAX - 1); // <-- get more weight from global count
                    do {
                        desired = *expected + (C::MAX - expected->cnt);
                        if (atomic_compare_exchange_weak(target,
                                                         expected,
                                                         desired,
                                                         std::memory_order_release,
                                                         failure)) {
                            if (__builtin_expect(expected->cnt == 1, false)) // <-- we fixed an exhausted counter
                                atomic_notify_all(target);        // <-- notify potential waiters
                            using std::swap;
                            swap(*expected, desired);
                            return desired.cnt;
                        }
                    } while (expected->ptr == desired.ptr); // <-- while the pointer bits are unchanged
                    (*desired).release(C::MAX); // <-- the pointer changed under us
                    return 0;
                }
                // exchange failed, try again
            } else {
                // locked, wait
                atomic_wait(target, *expected, failure);
                *expected = atomic_load(target, failure);
            }
        };
        return 0;
    }
    
    
    
    TEST_CASE("counted") {
        
        u64 x;
        
        CountedPtr<u64> p;
        p.cnt = 7;
        p.ptr = &x;
        p.tag = 3;
        
        REQUIRE(((u64) p) == (((u64) 6 << 47) | 3 | (u64) &x));
        
        REQUIRE(p.cnt == 7);
        REQUIRE(p.ptr == &x);
        REQUIRE(p.tag == 3);
        
        auto [a, b, c] = p.destructure();
        REQUIRE(a == 7);
        REQUIRE(b == &x);
        REQUIRE(c == 3);
        
        p.tag = 1;
        REQUIRE(p.tag == 1);
        REQUIRE(p.ptr == &x);
        u64 z;
        p.ptr = &z;
        REQUIRE(p.ptr == &z);
        REQUIRE(p.tag == 1);
        
        std::atomic<CountedPtr<u64>> t;
        REQUIRE(t.is_lock_free());
        
        z = 99;
        REQUIRE(*p == 99);
        *p = 101;
        REQUIRE(z == 101);
        
        printf("0x%0.16llx\n", (u64) &x);
        printf("0x%0.16llx\n", (u64) p);
        printf("%s\n", fmtb(&x));
        printf("%s\n", fmtb((u64) p));
        printf("%s\n", fmtb(754));
        
        REQUIRE_FALSE(healthy(CountedPtr<u64>{nullptr}));
        
        CountedPtr<counter> q(CountedPtr<counter>{10, new counter{10}, 0});
        
        auto w = atomic_load(&q, std::memory_order_relaxed);
        printf("q = %s\n", fmtb((u64) w));
        
        printf("w %llx\n", (u64) w);
        
        auto n = atomic_compare_acquire_strong(&q, &w);
        REQUIRE(n == 1); // <-- acquire normally
        n = atomic_compare_acquire_strong(&q, &w);
        REQUIRE(n == 8); // <-- we repaired counter
        REQUIRE(atomic_load(&w.ptr->count, std::memory_order_relaxed) == 9 + CountedPtr<counter>::MAX);
        REQUIRE(w.cnt == CountedPtr<counter const*>::MAX);

    }
    
} // namespace aarc
