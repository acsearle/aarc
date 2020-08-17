//
//  counted.cpp
//  aarc
//
//  Created by Antony Searle on 16/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include "counted.hpp"

#include "catch.hpp"

static_assert(sizeof(counted<char*>) == 8);
static_assert(alignof(counted<int*>) == 8);

static_assert(counted<char*>::TAG == 0);
static_assert(counted<u64*>::TAG == 7);

// requirements for std::atomic
static_assert(std::is_trivially_copyable<counted<u64*>>::value);
static_assert(std::is_copy_constructible<counted<u64*>>::value);
static_assert(std::is_move_constructible<counted<u64*>>::value);
static_assert(std::is_copy_assignable<counted<u64*>>::value);
static_assert(std::is_move_assignable<counted<u64*>>::value);

// is lock free
static_assert(std::atomic<counted<u64*>>::is_always_lock_free);

template<typename T>
char const* fmtb(T x) {
    thread_local static char s[65] = { '0', 'b', 0};
    char* p = s + (sizeof(x) << 3);
    *p = 0;
    for (; p-- != s + 2; x >>= 1)
        *p = x & 1 ? '1' : '0';
    return s;
}

template<typename T>
char const* fmtb(T* x) {
    return fmtb(reinterpret_cast<std::uintptr_t>(x));
}


/*
expected.cnt = n;
desired.cnt = n - 1;
 expected.raw = n - 1;
 desired.raw = n - 2;
 */

/*
template<typename Integral>
bool ispow2(Integral n) {
    return !(n & (n - 1));
}
 */

template<typename T>
bool healthy(counted<T const*> p) {
    using U = counted<T const*>;
    return (u64) p & ((u64) p + U::INC) & U::CNT;
}


// acquire shared ownership of the pointee of an atomic counted pointer
//
// returns units of ownership gained.  these must be released.
// return 0 if the pointer is null
// after the call, expected is the current value
// you must call release with the returned value NOT the value of expected.cnt
// the returned value is not always the change in expected.cnt (replenish path)

template<typename T>
[[nodiscard]] u64 atomic_acquire(atomic<counted<T const*>> const& p,
                                 counted<T const*>& expected,
                                 std::memory_order failure = std::memory_order_relaxed) {
    do if (auto n = atomic_compare_acquire_weak(p,
                                                expected,
                                                std::memory_order_relaxed)) {
        return n;
    } while (expected.ptr);
    return 0;
}
    
template<typename T>
[[nodiscard]] u64 atomic_compare_acquire_weak(atomic<counted<T const*>> const& p,
                                              counted<T const*>& expected,
                                              std::memory_order failure = std::memory_order_relaxed) {
    using C = counted<T const*>;
    constexpr auto MAX = counted<T const*>::MAX;
    C desired = expected;
    if (expected.ptr) {
        if (__builtin_expect(expected.cnt > 1, true)) {
            desired = expected - 1;
            if (p.compare_exchange_weak(expected,
                                        desired,
                                        std::memory_order_acquire,
                                        failure)) {
                if (__builtin_expect(healthy(desired), true)) {
                    expected = desired;
                    return 1; // <-- fast path completes
                } else { // <-- time to replenish the local count
                    expected = desired;
                    expected->acquire(MAX - 1); // <-- get more weight from global count
                    do if (p.compare_exchange_weak(expected,
                                                   desired = expected + (MAX - expected.cnt),
                                                   std::memory_order_release,
                                                   failure)) {
                        if (__builtin_expect(expected.cnt == 1, false)) // <-- was entirely depleted with possible waiters
                            reinterpret_cast<atomic<u64> const&>(p).notify_all();
                        using std::swap;
                        swap(expected, desired);
                        return desired.cnt;
                    } while (expected.ptr == desired.ptr); // <-- while the pointer bits are unchanged
                    desired->release(MAX); // <-- pointer changed under us
                    return 0;
                }
            }
            return 0; // <-- quit after one try
        } else {
            reinterpret_cast<atomic<u64> const&>(p).wait(expected, failure); // <-- if we don't wait here the caller becomes a spinlock which is worse?
        }
    }
    expected = p.load(failure); // <-- meet the failure requirements though we did not call compare_exchange
    return 0;
}



template<typename T>
[[nodiscard]] u64 atomic_compare_acquire_strong(atomic<counted<T const*>> const& p,
                                               counted<T const*>& expected,
                                               std::memory_order failure = std::memory_order_relaxed) {
    using C = counted<T const*>;
    constexpr auto MAX = counted<T const*>::MAX;
    C desired = expected;
    printf("%llx\n%llx\n", (u64) expected, (u64) desired);
    printf("expected.ptr %p\n", (T const*) expected.ptr);
    printf("desired.ptr %p\n", (T const*) desired.ptr);
    printf("    === %d\n", expected.ptr == desired.ptr);
    printf("    === %llx\n", (u64) expected - (u64) desired);

    while (expected.ptr && (expected.ptr == desired.ptr)) {
        if (__builtin_expect(expected.cnt > 1, true)) {
            desired = expected - 1;
            if (p.compare_exchange_weak(expected,
                                        desired,
                                        std::memory_order_acquire,
                                        failure)) {
                if (__builtin_expect(healthy(desired), true)) {
                    expected = desired;
                    return 1; // <-- fast path completes
                } else { // <-- count is a power of two, perform housekeeping
                    expected = desired;
                    expected->acquire(MAX - 1); // <-- get more weight from global count
                    do if (p.compare_exchange_weak(expected,
                                                   desired = expected + (MAX - expected.cnt),
                                                   std::memory_order_release,
                                                   failure)) {
                        if (__builtin_expect(expected.cnt == 1, false)) // <-- we fixed an exhausted counter
                            reinterpret_cast<atomic<u64> const&>(p).notify_all();        // <-- notify potential waiters
                        using std::swap;
                        swap(expected, desired);
                        return desired.cnt;
                    } while (expected.ptr == desired.ptr); // <-- while the pointer bits are unchanged
                    // we successfully acquired the pointer but it changed while we were repairing its counter
                    desired->release(MAX);
                    return 0;
                }
            }
        } else {
            p.wait(expected, failure);
            expected = p.load(failure);
        }
        printf("%llx\n%llx\n", (u64) expected, (u64) desired);
    };
    return 0;
}



TEST_CASE("counted") {
        
    u64 x;
    
    counted<u64*> p;
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
    
    std::atomic<counted<u64 const*>> t;
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
    
    REQUIRE_FALSE(healthy(counted<u64 const*>{nullptr}));
    
    const atomic<counted<counter const*>> q(counted<counter const*>{10, new counter{10}, 0});

    auto w = q.load(std::memory_order_relaxed);
    printf("q = %s\n", fmtb((u64) w));

    printf("w %llx\n", (u64) w);
    
    auto n = atomic_compare_acquire_strong(q, w);
    REQUIRE(n == 1); // <-- acquire normally
    n = atomic_compare_acquire_strong(q, w);
    REQUIRE(n == 8); // <-- we repaired counter
    REQUIRE(w.ptr->count.load(std::memory_order_relaxed) == 9 + counted<counter const*>::MAX);
    REQUIRE(w.cnt == counted<counter const*>::MAX);
    
    
    
    

    
}
