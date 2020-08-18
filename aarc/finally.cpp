//
//  finally.cpp
//  aarc
//
//  Created by Antony Searle on 17/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include "finally.hpp"

#include "catch.hpp"

TEST_CASE("finally") {
    
    bool a = false;;
    {
        auto guard = finally([&] { a = true; });
        REQUIRE_FALSE(a);
    }
    REQUIRE(a);
    
}
