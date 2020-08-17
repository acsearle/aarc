//
//  mutex.cpp
//  aarc
//
//  Created by Antony Searle on 13/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include "drop.hpp"
#include "mutex.hpp"

#include "catch.hpp"

TEST_CASE("mutex", "[mutex]") {
    
    mutex x(7);
    
    auto const& y = x;
    
    auto g = y.lock();
    *g = 8;
    drop(g);
    
    
    
    
}
