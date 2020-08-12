//
//  accountant.hpp
//  aarc
//
//  Created by Antony Searle on 12/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef accountant_hpp
#define accountant_hpp

#include "atomic.hpp"

struct Accountant {
    
    inline static std::atomic<std::int64_t> _count{0};
    
    static std::int64_t get() {
        return _count.load(std::memory_order_relaxed);
    }

    static void _add() {
        auto n = _count.fetch_add(1, std::memory_order_relaxed);
        assert(n >= 0);
    }
    
    static void _sub() {
        auto n = _count.fetch_sub(1, std::memory_order_relaxed);
        assert(n >= 1);
    }
        
    Accountant() { _add(); }
    Accountant(Accountant const&) { _add(); }
    ~Accountant() { _sub(); }
    
};
#endif /* accountant_hpp */
