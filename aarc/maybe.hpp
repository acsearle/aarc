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
union maybe {
    
    T value;
    
    maybe() {}
    ~maybe() {}
        
    template<typename... Args>
    void emplace(Args&&... args) {
        new (&value) T(std::forward<Args>(args)...);
    }
    
    void erase() const noexcept {
        (&value)->~T();
    }
    
};

#endif /* maybe_hpp */
