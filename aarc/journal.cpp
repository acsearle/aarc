//
//  journal.cpp
//  aarc
//
//  Created by Antony Searle on 17/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include "journal.hpp"

#include "catch.hpp"

TEST_CASE("journal") {
    
    journal::enter("hello");
    
    auto x = journal::take<char const*>();
    printf("%s\n", std::get<0>(x.front().second.front()));
    
}
