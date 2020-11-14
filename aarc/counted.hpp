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
#include "common.hpp"

#define self (*this)

namespace aarc {
    
    using namespace rust;
        
    // a counter, a pointer, and a tag are packed into a pointer-sized struct
    // suitable for use in a lock_free atomic
    //
    // the packing relies on 17 unused bits at the top of the pointer (the user
    // address space is 47 bit on current architectures) and the bottom (depending
    // on alignment of the pointee)
    //
    // relies on implementation-defined and platform-specific behaviors
    //
    // the standard permits us to inspect the common initial sequence of members of
    // a union, so the type-punning is permitted
    
    template<typename T>
    union CountedPtr {
        
        static constexpr u64 TAG = alignof(T) - 1;      // low bits mask
        static constexpr u64 SHF = 47;
        static constexpr u64 CNT = (~((u64) 0)) << SHF; // high bits mask
        static constexpr u64 PTR = ~CNT & ~TAG;         // middle bits mask
        static constexpr u64 MAX = (CNT >> SHF) + 1;
        static constexpr u64 INC = ((u64) 1) << SHF;
        
        class _cnt_t {
            
            friend union CountedPtr;
            
            u64 raw;
            
        public:
            
            operator u64() const { return (raw >> SHF) + 1; }
            constexpr explicit operator bool() const { return true; }
            _cnt_t operator++(int) { _cnt_t old{self}; self.raw += INC; return self; }
            _cnt_t operator--(int) { _cnt_t old{self}; self.raw -= INC; return self; }
            _cnt_t& operator++() { self.raw += INC; return self; }
            _cnt_t& operator--() { self.raw -= INC; return self; }
            constexpr bool operator!() const { return false; }
            bool operator==(_cnt_t n) const { return !(self != n); }
            bool operator!=(_cnt_t n) const { return (raw ^ n.raw) & CNT; }
            _cnt_t& operator=(u64 n) { raw = (raw & ~CNT) | ((n - 1) << SHF); return self; }
            _cnt_t& operator+=(u64 n) { raw += n << SHF; return self; }
            _cnt_t& operator-=(u64 n) { raw -= n << SHF; return self; }
            
        }; // _cnt_t
        
        class _ptr_t {
            
            friend union CountedPtr;
            
            u64 raw;
            
        public:
            
            operator T*() const { return (T*) (raw & PTR); }
            explicit operator bool() const { return raw & PTR; }
            T* operator->() const { return (T*) (raw & PTR); }
            bool operator!() const { return !(raw & PTR); }
            T& operator*() const { return *(T*)(raw & PTR); }
            bool operator==(_ptr_t p) const { return !(self != p); }
            bool operator!=(_ptr_t p) const { return (raw ^ p.raw) & PTR; }
            _ptr_t& operator=(T* p) { assert(!(~PTR & (u64) p)); raw = (raw & ~PTR) | ((u64) p); return self; }
            
        }; // _ptr_t
        
        class _tag_t {
            
            friend union CountedPtr;
            u64 raw;
            
        public:
            
            operator u64() const { return raw & TAG; }
            explicit operator bool() const { return raw & TAG; }
            bool operator!() const { return !(raw & TAG); }
            u64 operator~() const { return (~raw) & TAG; }
            bool operator==(_tag_t x) const { return !(self != x); }
            bool operator!=(_tag_t x) const { return (raw ^ _raw(&x)) & TAG; }
            _tag_t& operator=(u64 x) { raw = (raw & ~TAG) | (x & TAG); return self; }
            _tag_t& operator&=(u64 x) { raw &= x | ~TAG; return self; }
            _tag_t& operator^=(u64 x) { raw ^= x & TAG; return self; }
            _tag_t& operator|=(u64 x) { raw |= x & TAG; return self; }
            
        }; // _tag_t
        
        
        // type-punning union provides different views of the same u64
        
        _cnt_t cnt; // <-- a 17 bit, 1-based counter
        _ptr_t ptr; // <-- a pointer to T
        _tag_t tag; // <-- log2(alignof(T)) tag bits
        u64 raw;
                
        struct unpacked {
            
            u64 cnt;
            T*  ptr;
            u64 tag;
            
        }; // unpacked
        
        unpacked destructure() const {
            return unpacked { (u64) self.cnt, (T*) self.ptr, (u64) self.tag };
        }
        
        CountedPtr() = default;
        CountedPtr(int x) : raw(x) {}
        CountedPtr(u64 x) : raw(x) {}
        CountedPtr(std::nullptr_t) : raw(0) {}
        CountedPtr(T* p) : raw((u64) p) {}
        CountedPtr(unpacked p) : CountedPtr(p.cnt, p.ptr, p.tag) {}
        CountedPtr(u64 n, T* p, u64 t) {
            ptr.raw = (((n - 1) << SHF)
                       | ((u64) p)
                       | (t & TAG)
                       );
            assert(n == cnt);
            assert(p == ptr);
            assert(t == tag);
        }
        
        explicit operator u64&() { return ptr.raw; }
        explicit operator u64 const&() const { return ptr.raw; }
        explicit operator bool() const { return self.ptr; }
        bool operator==(CountedPtr p) const { return (u64) *this == (u64) p; }
        bool operator!=(CountedPtr p) const { return !(*this != p); }
        CountedPtr& operator=(unpacked p) { return *this = CountedPtr(p); }
        CountedPtr& operator=(std::nullptr_t) { self.raw = 0; return self; }
        
        // pointer operators apply to the pointer
        T* operator->() const { return this->ptr; }
        T& operator*() const { return *this->ptr; }
        
        // arithmetic operators apply to the count
        CountedPtr operator++(int) { CountedPtr old{self}; ++self; return old; }
        CountedPtr operator--(int) { CountedPtr old{self}; --self; return old; }
        CountedPtr& operator++() { ++cnt; return self; }
        CountedPtr& operator--() { ++cnt; return self; }
        CountedPtr operator+(u64 n) const { CountedPtr tmp{self}; return tmp += n; }
        CountedPtr operator-(u64 n) const { CountedPtr tmp{self}; return tmp -= n; }
        CountedPtr& operator+=(u64 n) { cnt += n; return self; }
        CountedPtr& operator-=(u64 n) { cnt -= n; return self; }
        
        // bitwise operators apply to the tag
        CountedPtr operator~()const  { return CountedPtr{(u64) *this ^ TAG}; }
        CountedPtr operator&(u64 n) const { CountedPtr tmp{self}; return tmp &= n; }
        CountedPtr operator^(u64 n) const { CountedPtr tmp{self}; return tmp ^= n; }
        CountedPtr operator|(u64 n) const { CountedPtr tmp{self}; return tmp |= n; }
        CountedPtr& operator&=(u64 n) { tag &= n; return self; }
        CountedPtr& operator^=(u64 n) { tag ^= n; return self; }
        CountedPtr& operator|=(u64 n) { tag |= n; return self; }
        
    }; // union CountedPtr
    

    // standard atomic functions - of limited use?
    
    template<typename T>
    CountedPtr<T> atomic_load(CountedPtr<T>* target,
                              std::memory_order order) {
        return CountedPtr<T>(atomic_load(&target->raw,
                                         order));
    }

    template<typename T>
    void atomic_store(CountedPtr<T>* target,
                      CountedPtr<std::type_identity_t<T>> desired,
                      std::memory_order order) {
        atomic_store(&target->raw,
                     desired.raw,
                     order);
    }

    template<typename T>
    CountedPtr<T> atomic_exchange(CountedPtr<T>* target,
                                  CountedPtr<std::type_identity_t<T>> desired,
                                  std::memory_order order) {
        return CountedPtr<T>(atomic_exchange(&target->raw,
                                             desired.raw,
                                             order));
    }

    template<typename T>
    bool atomic_compare_exchange_weak(CountedPtr<T>* target,
                                      CountedPtr<std::type_identity_t<T>>* expected,
                                      CountedPtr<std::type_identity_t<T>> desired,
                                      std::memory_order success,
                                      std::memory_order failure) {
        return atomic_compare_exchange_weak(&target->raw,
                                            &expected->raw,
                                            desired.raw,
                                            success,
                                            failure);
    }
    
    template<typename T>
    bool atomic_compare_exchange_strong(CountedPtr<T>* target,
                                        CountedPtr<std::type_identity_t<T>>* expected,
                                        CountedPtr<std::type_identity_t<T>> desired,
                                        std::memory_order success,
                                        std::memory_order failure) {
        return atomic_compare_exchange_strong(&target->raw,
                                              &expected->raw,
                                              desired.raw,
                                              success,
                                              failure);
    }
    
    template<typename T>
    void atomic_wait(CountedPtr<T>* target,
                     CountedPtr<std::type_identity_t<T>> old,
                     std::memory_order order) {
        return atomic_wait(&target->raw,
                           old.raw,
                           order);
    }

    template<typename T>
    void atomic_notify_one(CountedPtr<T>* target) {
        return atomic_notify_one(&target->raw);
    }

    template<typename T>
    void atomic_notify_all(CountedPtr<T>* target) {
        return atomic_notify_all(&target->raw);
    }
    
    //
    // Counted
    //
    
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
        assert(target);
        assert(expected);
        do if (auto n = atomic_compare_acquire_weak(target, expected, failure))
            return n;
        while (expected->ptr);
        return 0;
    }
    
    template<typename T>
    bool healthy(CountedPtr<T> p) {
        using U = CountedPtr<T>;
        return (u64) p & ((u64) p + U::INC) & U::CNT;
    }
    
    // attempt to acquire shared ownership of target if the pointer bits are as
    // expected; spurious failure is permitted
    
    template<typename T>
    [[nodiscard]] u64 atomic_compare_acquire_weak(CountedPtr<T>* target,
                                                  CountedPtr<T>* expected,
                                                  std::memory_order failure = std::memory_order_relaxed) {
        assert(target);
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
        *expected = atomic_load(target, failure); // <-- meet the failure requirements though we did not call compare_exchange
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
    

} // namespace aarc



namespace aarc {
    
    // Countable
    //
    // increase_strong_count
    // decrease_strong_count
    //
    // Successible?
    
    struct counter {
        
        mutable u64 count;
        
        u64 release(u64 n) const {
            assert(n > 0);
            auto m = atomic_fetch_sub(&count, n, std::memory_order_release);
            assert(m >= n);
            if (m == n) {
                // synchronize with other releases
                [[maybe_unused]] auto z = atomic_load(&count, std::memory_order_acquire);
                assert(z == 0);
                delete this; // <-- how to reuse this logic and delete as right type?
            }
            return m - n;
        }
        
        u64 acquire(u64 n) const {
            assert(n > 0); // <-- else no-op
            auto m = atomic_fetch_add(&count, n, std::memory_order_relaxed);
            assert(m); // <-- else unowned
            return m + n;
        }
        
    }; // union CountedPtr
    
    
    
} // namespace aarc

#endif /* counted_hpp */
