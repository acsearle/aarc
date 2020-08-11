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
    
    alignas(T) unsigned char raw[sizeof(T)];
    
    template<typename... Args>
    void emplace(Args&&... args) {
        new (raw) T(std::forward<Args>(args)...);
    }
    
    void erase() const {
        reinterpret_cast<T const*>(raw)->~T();
    }
    
    T& get() { return reinterpret_cast<T&>(*raw); }
    T const& get() const { return reinterpret_cast<T const&>(*raw); }

};

#endif /* maybe_hpp */
