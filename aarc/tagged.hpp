//
//  tagged.hpp
//  aarc
//
//  Created by Antony Searle on 12/9/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef tagged_hpp
#define tagged_hpp

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "atomic.hpp"

using usize = std::uintptr_t;

template<typename T>
union TaggedPtr {
    
    static constexpr usize TAG = alignof(T) - 1;
    static constexpr usize PTR = ~TAG;
    
    usize _raw;
    
    struct _ptr_t {
        
        usize _raw;
        
        T& as_mut() const {
            return *reinterpret_cast<T*>(_raw & PTR);
        }
        
        T const* operator->() const {
            return reinterpret_cast<T const*>(_raw & PTR);
        }
        
        T const& operator*() const {
            assert(_raw & PTR);
            return *reinterpret_cast<T const*>(_raw & PTR);
        }
        
        operator T const*() const {
            return reinterpret_cast<T const*>(_raw & PTR);
        }
        
        explicit operator bool() const {
            return _raw & PTR;
        }
        
        _ptr_t& operator=(T const* other) {
            assert(!(reinterpret_cast<usize>(other) & TAG));
            _raw = reinterpret_cast<usize>(other) | (_raw & TAG);
        }
        
        _ptr_t& operator=(std::nullptr_t) {
            _raw &= TAG;
        }
        
    } ptr;
    
    struct _tag_t {
        
        usize _raw;
        
        usize operator++(int) {
            usize old = _raw & TAG;
            _raw = (_raw & PTR) | ((_raw + 1) & TAG);
            return old;
        }
        
        usize operator--(int) {
            usize old = _raw & TAG;
            _raw = (_raw & PTR) | ((_raw - 1) & TAG);
            return old;
        }
        
        _tag_t& operator++() {
            _raw = (_raw & PTR) | ((_raw + 1) & TAG);
            return *this;
        }
        
        _tag_t& operator--() {
            _raw = (_raw & PTR) | ((_raw - 1) & TAG);
            return *this;
        }
        
        usize operator+() const {
            return +(_raw & TAG);
        }
        
        usize operator-() const {
            return -(_raw & TAG);
        }
        
        bool operator!() const {
            return !(_raw & TAG);
        }
        
        usize operator~() const {
            return ~(_raw & TAG);
        }
        
        operator usize() const {
            return _raw & TAG;
        }
        
        explicit operator bool() const {
            return static_cast<bool>(_raw & TAG);
        }
        
        _tag_t& operator=(usize other) {
            _raw = (_raw & PTR) | (other & TAG);
            return *this;
        }
        
        _tag_t& operator+=(usize other) {
            _raw = (_raw & PTR) | ((_raw + other) & TAG);
            return *this;
        }
        
        _tag_t& operator-=(usize other) {
            _raw = (_raw & PTR) | ((_raw - other) & TAG);
            return *this;
        }
        
        _tag_t& operator*=(usize other) {
            _raw = (_raw & PTR) | ((_raw * other) & TAG);
            return *this;
        }
        
        _tag_t& operator/=(usize other) {
            assert(other);
            _raw = (_raw & PTR) | ((_raw & TAG) / other);
            return *this;
        }
        
        _tag_t& operator%=(usize other) {
            assert(other);
            _raw = (_raw & PTR) | ((_raw & TAG) % other);
            return *this;
        }
        
        _tag_t& operator<<=(int other) {
            assert(other >= 0);
            _raw = (_raw & PTR) | ((_raw << other) & TAG);
            return *this;
        }
        
        _tag_t& operator>>=(int other) {
            assert(other >= 0);
            _raw = (_raw & PTR) | ((_raw & TAG) >> other);
            return *this;
        }
        
        _tag_t& operator&=(usize other) {
            _raw &= other | PTR;
            return *this;
        }
        
        _tag_t& operator^=(usize other) {
            _raw ^= other & TAG;
            return *this;
        }
        
        _tag_t& operator|=(usize other) {
            _raw |= other & TAG;
        }
        
    } tag;
    
    TaggedPtr() = default;
    explicit TaggedPtr(usize x) : _raw(x) {}
    explicit TaggedPtr(T const* p) : _raw(reinterpret_cast<usize>(p)) {}
    
    TaggedPtr operator++(int) {
        TaggedPtr tmp = *this;
        ++*this;
        return tmp;
    }
    
    TaggedPtr operator--(int) {
        TaggedPtr tmp = *this;
        ++*this;
        return tmp;
    }
    
    T const* operator->() const {
        return ptr.operator->();
    }
    
    TaggedPtr operator++() {
        ++tag;
        return *this;
    }
    
    TaggedPtr operator--() {
        --tag;
        return *this;
    }
    
    bool operator!() const {
        return !_raw;
    }
    
    TaggedPtr operator~() const {
        return TaggedPtr { _raw ^ TAG };
    }
    
    T const& operator*() const {
        return ptr.operator*();
    }
    
    
    TaggedPtr operator&(usize t) const {
        return TaggedPtr { _raw & (t | PTR) };
    }

    TaggedPtr operator^(usize t) const {
        return TaggedPtr { _raw ^ (t & TAG) };
    }

    TaggedPtr operator|(usize t) const {
        return TaggedPtr { _raw | (t & TAG) };
    }

};



#endif /* tagged_hpp */
