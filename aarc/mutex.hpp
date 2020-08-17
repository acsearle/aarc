//
//  mutex.hpp
//  aarc
//
//  Created by Antony Searle on 13/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef mutex_hpp
#define mutex_hpp

#include <mutex>

template<typename T, typename M = std::mutex>
class mutex {
    
    mutable M _mutex;
    mutable T _payload;
    
public:
    
    mutex() = default;
    
    template<typename... Args>
    mutex(Args&&... args) : _payload(std::forward<Args>(args)...) {}
    
    mutex(mutex const&) = delete;
    mutex(mutex& other) : _payload(other._payload) {}
    mutex(mutex&& other) :_payload(std::move(other._payload)) {}
    mutex(mutex const&&) = delete;
    
    class guard {
        
        friend class mutex;
        
        mutex const* _ptr;
        
        explicit guard(mutex const* p) : _ptr(p) {}
        
    public:
        
        guard() : _ptr{0} {}
                        
        guard(guard const&) = delete;

        guard(guard&& other)
        : _ptr(std::exchange(other._ptr, nullptr)) {
        }
        
        ~guard() {
            if (_ptr)
                _ptr->_mutex.unlock();
        }
        
        void swap(guard& other) {
            using std::swap;
            swap(_ptr, other._ptr);
        }
        
        guard& operator=(guard const&) = delete;

        guard& operator=(guard&& other) {
            guard(std::move(other)).swap(*this);
            return *this;
        }

        explicit operator bool() const {
            return _ptr;
        }
        
        T* operator->() const {
            assert(_ptr);
            return &_ptr->_payload;
        }
        
        T& operator*() const {
            assert(_ptr);
            return _ptr->_payload;
        }
        
    };
    
    guard lock() = delete;
    guard lock() const {
        _mutex.lock();
        return guard(this);
    }
    
    guard try_lock() = delete;
    guard try_lock() const {
        return guard(_mutex.try_lock() ? this : nullptr);
    }
    
    T* operator->() {
        return &_payload;
    }
    
    guard operator->() const {
        return lock();
    }
    
    T& operator*() {
        return _payload;
    }
        
    T into_inner() && {
        return std::move(_payload);
    }
    
};

template<typename T>
mutex(T&&) -> mutex<std::decay_t<T>>;

#endif /* mutex_hpp */
