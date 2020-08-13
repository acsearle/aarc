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
struct mutex {
    
    mutable M _mutex;
    mutable T _payload;
    
    mutex() = default;
    mutex(mutex const&) = delete;
    mutex(mutex& other) : _mutex(), _payload(other._payload) {}
    mutex(mutex&& other) : _mutex(), _payload(std::move(other._payload)) {}
    mutex(mutex const&&) = delete;

    struct guard {
        std::unique_lock<M> _lock;
        T* _ptr;
        
        guard() : _lock{}, _ptr{0} {};
                
        guard(mutex&) = delete;
        guard(mutex&&) = delete;
        guard(mutex const&&) = delete;
        guard(mutex const& x)
        : _lock{x._mutex}
        , _ptr{&x._payload} {
        }
        
        guard(guard&) = delete;
        guard(guard const&) = delete;
        guard(guard const&&) = delete;
        guard(guard&& other)
        : _lock{std::move(other._lock)}
        , _ptr{std::exchange(other._ptr, nullptr)} {
        }
        
        T* operator->() {
            return _ptr;
        }
    };
    
};


#endif /* mutex_hpp */
