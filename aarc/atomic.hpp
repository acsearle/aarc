//
//  atomic.hpp
//  aarc
//
//  Created by Antony Searle on 12/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef atomic_hpp
#define atomic_hpp

#include <atomic>

#include "atomic_wait.hpp"

template<typename T>
class atomic {
    
    union {
        T _value;
        mutable std::atomic<T> _atomic;
    };
    
public:
    
    atomic() = default;
    
    atomic(T x) : _value(x) {}
    
    atomic(atomic const&) = delete;
    atomic(atomic& other) : _value(other._value) {}
    atomic(atomic&& other) : _value(other._value) {}
    
    ~atomic() = default;
        
    atomic& operator=(T x) & { _value = x; return *this; }
    
    atomic& operator=(atomic const&) & = delete;
    atomic& operator=(atomic& other) & { _value = other._value; return *this; }
    atomic& operator=(atomic&& other) & { _value = other._value; return *this; }
    
    void swap(T& x) & { using std::swap; swap(_value, x); }
    
    // implicit conversion to T enables ++, etc.
    
    operator T&() { return _value; }
    explicit operator bool() { return _value; }
    
    // const-qualified
    
    T load(std::memory_order) = delete; // <-- provide deleted mutable overloads to trap misuse of const methods
    T load(std::memory_order order) const {
        return _atomic.load(order);
    }
    
    T store(T, std::memory_order) = delete;
    void store(T x, std::memory_order order) const {
        _atomic.store(x, order);
    }
    
    T exchange(T, std::memory_order) = delete;
    T exchange(T x, std::memory_order order) const {
        return _atomic.exchange(x, order);
    }
    
    bool compare_exchange_weak(T&, T, std::memory_order, std::memory_order) = delete;
    bool compare_exchange_weak(T& expected, T desired, std::memory_order success, std::memory_order failure) const {
        return _atomic.compare_exchange_weak(expected, desired, success, failure);
    }

    bool compare_exchange_strong(T&, T, std::memory_order, std::memory_order) = delete;
    bool compare_exchange_strong(T& expected, T desired, std::memory_order success, std::memory_order failure) const {
        return _atomic.compare_exchange_strong(expected, desired, success, failure);
    }
    
    void wait(T, std::memory_order) noexcept = delete;
    void wait(T old, std::memory_order order) const noexcept {
        std::atomic_wait_explicit(&_atomic, old, order);
    }
    
    void notify_one() noexcept = delete;
    void notify_one() const noexcept {
        std::atomic_notify_one(&_atomic);
    }
    
    void notify_all() noexcept = delete;
    void notify_all() const noexcept {
        std::atomic_notify_all(&_atomic);
    }
    
    T fetch_add(T, std::memory_order) = delete;
    T fetch_add(T x, std::memory_order order) const {
        return _atomic.fetch_add(x, order);
    }
    
    T fetch_sub(T, std::memory_order) = delete;
    T fetch_sub(T x, std::memory_order order) const {
        return _atomic.fetch_sub(x, order);
    }

    T fetch_and(T, std::memory_order) = delete;
    T fetch_and(T x, std::memory_order order) const {
        return _atomic.fetch_and(x, order);
    }

    T fetch_or(T, std::memory_order) = delete;
    T fetch_or(T x, std::memory_order order) const {
        return _atomic.fetch_or(x, order);
    }

    T fetch_xor(T, std::memory_order) = delete;
    T fetch_xor(T x, std::memory_order order) const {
        return _atomic.fetch_xor(x, order);
    }

    // escape hatch
    
    T& unsafe_reference() = delete;
    T& unsafe_reference() const {
        return const_cast<T&>(_value);
    }

};

template<typename T>
void swap(atomic<T>& a, T& b) {
    a.swap(b);
}

#endif /* atomic_hpp */
