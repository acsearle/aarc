//
//  counted.hpp
//  aarc
//
//  Created by Antony Searle on 16/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef counted_hpp
#define counted_hpp

#include "atomic.hpp"

using u64 = std::uint64_t;

template<typename T>
inline constexpr std::size_t _TAG = alignof(T) - 1;

template<>
inline constexpr std::size_t _TAG<void> = 0;


template<typename T>
struct counted;

// a counter, a pointer, and a tag are packed into a pointer-sized struct
// suitable for use in a lock_free atomic
//
// the packing relies on 17 unused bits at the top of the pointer (the user
// address space is 47 bit on current architectures) and the bottom (depending
// on alignment of the pointee)
//
// relies on implementation-defined and platform-specific behaviors

template<typename T>
struct counted<T*> {
        
    static constexpr u64 TAG = _TAG<T>;
    static constexpr u64 SHF = 47;
    static constexpr u64 CNT = (~((u64) 0)) << SHF;
    static constexpr u64 PTR = ~CNT & ~TAG;
    static constexpr u64 MAX = (CNT >> SHF) + 1;
    static constexpr u64 INC = ((u64) 1) << SHF;
        
    struct _cnt_t {
        
        u64 raw;
        
        operator u64() const { return (raw >> SHF) + 1; }
        explicit operator bool() const = delete; // always true
        _cnt_t operator++(int) { _cnt_t old{*this}; ++*this; return old; }
        _cnt_t operator--(int) { _cnt_t old{*this}; --*this; return old; }
        _cnt_t& operator++() { return *this += 1; }
        _cnt_t& operator--() { return *this -= 1; }
        bool operator!() = delete; // always false
        bool operator==(_cnt_t n) { return !(*this != n); }
        bool operator!=(_cnt_t n) { return (raw ^ n.raw) & CNT; }
        _cnt_t& operator=(u64 n) { raw = (raw & ~CNT) | ((n - 1) << SHF); return *this; }
        _cnt_t& operator+=(u64 n) { raw += n << SHF; return *this; }
        _cnt_t& operator-=(u64 n) { raw -= n << SHF; return *this; }
        
    }; // _cnt_t
    
    struct _ptr_t {
        
        u64 raw;

        operator T*() const { return (T*) (raw & PTR); }
        explicit operator bool() const { return raw & PTR; }
        T* operator->() const { return (T*) (raw & PTR); }
        bool operator!() const { return !(raw & PTR); }
        T& operator*() const { return *(T*)(raw & PTR); }
        bool operator==(_ptr_t p) { return !((raw ^ p.raw) & PTR); }
            bool operator!=(_ptr_t p) { return (raw ^ p.raw) & PTR; }
        _ptr_t& operator=(T* p) { assert(!(~PTR & (u64) p)); raw = (raw & ~PTR) | ((u64) p); return *this; }
        
    }; // _ptr_t
    
    struct _tag_t {
        
        u64 raw;
        
        operator u64() const { return raw & TAG; }
        explicit operator bool() const { return raw & TAG; }
        bool operator!() const { return !(raw & TAG); }
        u64 operator~() const { return (~raw) & TAG; }
        bool operator==(_tag_t x) { return !(*this != x); }
        bool operator!=(_tag_t x) { return (raw ^ x.raw) & TAG; }
        _tag_t& operator=(u64 x) { raw = (raw & ~TAG) | (x & TAG); return *this; }
        _tag_t& operator&=(u64 x) { raw &= x | ~TAG; return *this; }
        _tag_t& operator^=(u64 x) { raw ^= x & TAG; return *this; }
        _tag_t& operator|=(u64 x) { raw |= x & TAG; return *this; }
        
    }; // _tag_t
    
    union {
                    
        _cnt_t cnt; // <-- a 17 bit, 1-based counter
        _ptr_t ptr; // <-- a pointer to T
        _tag_t tag; // <-- log2(alignof(T)) tag bits
        
        u64 raw;
                
    };

    struct unpacked {
        
        u64 cnt;
        T*  ptr;
        u64 tag;
        
    }; // unpacked
    
    unpacked destructure() const {
        return unpacked { (u64) cnt, (T*) ptr, (u64) tag };
    }
    
    counted() = default;
    
    explicit counted(u64 x) : raw{x} {}
    counted(std::nullptr_t) : raw{0} {}
    counted(unpacked p) : counted(p.cnt, p.ptr, p.tag) {}
    counted(u64 n, T* p, u64 t)
    : raw{
        ((n - 1) << SHF)
        | ((u64) p)
        | (t & TAG)
    } {
    }
        
    explicit operator bool() const = delete;
    bool operator==(counted p) { return raw == p.raw; }
    bool operator!=(counted p) { return raw != p.raw; }
    counted& operator=(unpacked p) { return *this = counted(p); }
    counted& operator=(std::nullptr_t) { raw = 0; return *this; }

    // pointer operators apply to the pointer
    T* operator->() const { return this->ptr; }
    T& operator*() const { return *this->ptr; }
    
    // arithmetic operators apply to the count
    counted operator++(int) { counted old{*this}; ++*this; return old; }
    counted operator--(int) { counted old{*this}; --*this; return old; }
    counted& operator++() { ++cnt; return *this; }
    counted& operator--() { ++cnt; return *this; }
    counted operator+(u64 n) { counted tmp{*this}; return tmp += n; }
    counted operator-(u64 n) { counted tmp{*this}; return tmp -= n; }
    counted& operator+=(u64 n) { cnt += n; return *this; }
    counted& operator-=(u64 n) { cnt -= n; return *this; }

    // bitwise operators apply to the tag
    counted operator~() { return counted{raw ^ TAG}; }
    counted operator&(u64 n) { counted tmp{*this}; return tmp &= n; }
    counted operator^(u64 n) { counted tmp{*this}; return tmp ^= n; }
    counted operator|(u64 n) { counted tmp{*this}; return tmp |= n; }
    counted& operator&=(u64 n) { tag &= n; return *this; }
    counted& operator^=(u64 n) { tag ^= n; return *this; }
    counted& operator|=(u64 n) { tag |= n; return *this; }
    
    counted saturated_copy() {
        return counted{raw | CNT};
    }

};

struct counter {
        
    atomic<u64> count;
    
    u64 release(u64 n) const {
        assert(n > 0);
        auto m = count.fetch_sub(n, std::memory_order_release);
        assert(m >= n);
        if (m == n) {
            // synchronize with other releases
            [[maybe_unused]] auto z = count.load(std::memory_order_acquire);
            assert(z == 0);
            delete this; // <-- how to reuse this logic and delete as right type?
        }
        return m - n;
    }
    
    u64 acquire(u64 n) const {
        assert(n > 0); // <-- else no-op
        auto m = count.fetch_add(n, std::memory_order_relaxed);
        assert(m); // <-- else unowned
        return m + n;
    }
    
};


#endif /* counted_hpp */