//
//  maybe.hpp
//  aarc
//
//  Created by Antony Searle on 12/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef maybe_hpp
#define maybe_hpp

#include <utility>

template<typename T>
struct maybe {
    
    alignas(T) unsigned char _value[sizeof(T)];
    
    template<typename... Args>
    void emplace(Args&&... args) {
        new ((void*) _value) T(std::forward<Args>(args)...);
    }
    
    void erase() const noexcept {
        (*this)->~T();
    }
    
    T& get() { return reinterpret_cast<T&>(*_value); }
    T const& get() const { return reinterpret_cast<T const&>(*_value); }
    
    T* operator->() { return reinterpret_cast<T*>(_value); }
    T const* operator->() const { return reinterpret_cast<T const*>(_value); }
    
    T& operator*() { return reinterpret_cast<T&>(*_value); }
    T const& operator*() const { return reinterpret_cast<T const&>(*_value); }

};

#endif /* maybe_hpp */
