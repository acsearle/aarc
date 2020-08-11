//
//  async_semaphore.cpp
//  aarc
//
//  Created by Antony Searle on 18/7/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include "async_semaphore.hpp"

#include "catch.hpp"

TEST_CASE("async_semaphore") {
    
    int x = 0;
    
    AsyncSemaphore s;
    
    s.wait_async([&]() {
        printf("a\n");
        x |= 1;
    });
    printf("b\n");
    s.wait_async([&]() {
        printf("c\n");
        x |= 2;
    });
    printf("d\n");
    REQUIRE(x == 0);
    s.notify();
    printf("e\n");
    REQUIRE(x == 1);
    s.notify();
    REQUIRE(x == 3);
    printf("f\n");

    

    
}

