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
        
        /*
        auto n = atomic_compare_acquire_strong(&q, &w);
        REQUIRE(n == 1); // <-- acquire normally
        n = atomic_compare_acquire_strong(&q, &w);
        REQUIRE(n == 8); // <-- we repaired counter
        REQUIRE(atomic_load(&w.ptr->count, std::memory_order_relaxed) == 9 + CountedPtr<counter>::MAX);
        REQUIRE(w.cnt == CountedPtr<counter const*>::MAX);
         */

    }
    
} // namespace aarc
