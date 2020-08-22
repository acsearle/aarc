//
//  pool.hpp
//  aarc
//
//  Created by Antony Searle on 10/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef pool_hpp
#define pool_hpp

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

#include "fn.hpp"
#include "stack.hpp"

void pool_submit_one(fn<void()> f);
void pool_submit_many(stack<fn<void()>> s);


#endif /* pool_hpp */
