//
//  journal.cpp
//  aarc
//
//  Created by Antony Searle on 17/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include "journal.hpp"

#include "catch.hpp"

/*
 
 // Recursive fun with allocations from journalling allocations
 
void* operator new(std::size_t count) {
    void* p = malloc(count);
    static thread_local bool flag = true;
    if (flag) {
        flag = false;
        journal::enter("alloc", p, count);
        flag = true;
    }
    return p;
}

void operator delete(void* p) noexcept {
    static thread_local bool flag = true;
    if (flag) {
        flag = false;
        journal::enter("free", p);
        flag = true;
    }
    free(p);
}
 
 */

TEST_CASE("journal") {
    
    journal::enter("hello");
    
    auto x = journal::take<char const*>();
    printf("%s\n", std::get<0>(x.front().second.front()));
    
}
