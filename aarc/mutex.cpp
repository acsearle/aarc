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
    
    const mutex a(7);
    
    auto b = a.lock();
    *b = 8;
    auto c = a.try_lock();
    REQUIRE_FALSE(c);
    drop(b);
    REQUIRE_FALSE(b);
    c = a.try_lock();
    REQUIRE(c);
    b = a.try_lock();
    REQUIRE_FALSE(b);
    drop(c);
    
    mutex z(9);
    // z.lock();
    *z = 10;
    // z.try_lock();
    
    
    
    
    
}
