//
//  epoch.cpp
//  aarc
//
//  Created by Antony Searle on 17/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include <thread>
#include <vector>
#include <map>

#include "atomic.hpp"
#include "epoch.hpp"

#include "catch.hpp"



TEST_CASE("epoch") {
    
    return;
    
    std::size_t n = 8;
    
    const atomic<u64> global{1000};
    std::vector<atomic<u64>> locals;
    
    for (int i = 0; i != n; ++i)
        locals.emplace_back(0);
    
    const atomic<const atomic<int>*> p{new atomic<int>{7}};

    std::vector<std::thread> t;
    for (int i = 0; i != n; ++i) {
        t.emplace_back([&global,
                        &locals = std::as_const(locals),
                        &p,
                        i, n] {
            
            std::multimap<u64, const atomic<int>*> bag;
            
            for (;;) {
                //std::this_thread::sleep_for(std::chrono::milliseconds{rand() % 1000});
                
                u64 epoch = global.load(std::memory_order_relaxed);
                printf("%llx (%d)\n", epoch, i);
                locals[i].store(epoch, std::memory_order_relaxed);
                
                if (rand() % 8) {
                    p.load(std::memory_order_acquire)->fetch_add(1, std::memory_order_relaxed);
                } else {
                    bag.emplace(epoch, p.exchange(new atomic<int>{rand()}, std::memory_order_acq_rel));
                }
                                
                bool flag = true;
                for (int j = 0; j != n; ++j) {
                    auto z = locals[j].load(std::memory_order_relaxed);
                    assert(!z || (z == epoch) || (z == epoch + 1) || (z == epoch - 1));
                    if (z && (z != epoch)) {
                        flag = false;
                        break;
                    }
                }
                
                if (flag) {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    global.store(epoch + 1, std::memory_order_release);
                }
                
                locals[i].store(0, std::memory_order_release);
                
                // bag?
            }
        });
    }
    
    
    
    
 
    while (!t.empty()) {
        t.back().join();
        t.pop_back();
    }
    
}

