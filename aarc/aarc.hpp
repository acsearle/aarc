//
//  aarc.hpp
//  aarc
//
//  Created by Antony Searle on 9/7/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef aarc_hpp
#define aarc_hpp

#include <atomic>
#include <cassert>
#include <cstdint>
#include <utility>
#include <iostream>
#include <cinttypes>

#include "accountant.hpp"
#include "maybe.hpp"

template<typename T>
struct Arc {
    
    struct Inner {
        std::atomic<std::int64_t> _strong;
        T _payload;
    };

    std::uint64_t _value;
    
    static constexpr std::uint64_t MASK = 0x0000'FFFF'FFFF'FFFF;
    static constexpr std::uint64_t INC  = 0x0001'0000'0000'0000;
    
    Arc()
    : _value(0) {
    }
    
    explicit Arc(std::uint64_t x)
    : _value{x} {
    }
    
    Arc(Arc&& x)
    : _value(std::exchange(x._value, 0)) {
    }
    
    Arc(Arc const& x);
    
    ~Arc() {
        auto p = reinterpret_cast<Inner*>(_value & MASK);
        auto n = (_value >> 48) + 1;
        if (p && p->_strong.fetch_sub(n, std::memory_order_release) == n) {
            p->_strong.load(std::memory_order_acquire);
            delete p;
        }
    }
    
    void swap(Arc& x) {
        using std::swap;
        swap(_value, x._value);
    }
    
    Arc& operator=(Arc&& x) {
        Arc(std::move(x)).swap(*this);
        return *this;
    }
    
    Arc& operator=(Arc const& x) {
        Arc(x).swap(*this);
        return *this;
    }
    
    T* operator->() const {
        assert(_value);
        return &(reinterpret_cast<Inner*>(_value & MASK)->_payload);
    }
    
    T& operator*() const {
        assert(_value);
        reinterpret_cast<Inner*>(_value & MASK)->_payload;
    }
    
};

template<typename T>
struct Aarc {
    
    std::atomic<std::uint64_t> _value;
    
    Aarc()
    : _value{0} {
    }

    explicit Aarc(Arc<T> x)
    : _value{std::exchange(x._value, 0)} {
    }
    
    ~Aarc() {
        Arc<T>{_value.load(std::memory_order_relaxed)};
    }

    Arc<T> load() {
        std::uint64_t expected = _value.load(std::memory_order_relaxed);
        std::uint64_t desired = 0;
        
        for (;;) {
            if (!expected) {
                return Arc<T>{};
            } else {
                // nonzero value
            }
        }
        
        do {
            desired = expected - Arc<T>::INC;
        } while (_value.compare_exchange_weak(expected,
                                              desired,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed));
        return Arc<T>{expected & Arc<T>::MASK};
    }
    
    Arc<T> exchange(Arc<T> desired) {
        desired._value = _value.exchange(desired._value,
                                         std::memory_order_acq_rel);
        return desired;
    }
    
    void store(Arc<T> desired) {
        exchange(std::move(desired));
    }
    
    bool compare_exchange_weak(Arc<T>& expected, Arc<T> desired) {
        std::uint64_t x = _value.load(std::memory_order_relaxed);
        std::uint64_t d = 0;
        for (;;) {
            
        }
        return true;
    }
    
};



#endif /* aarc_hpp */
