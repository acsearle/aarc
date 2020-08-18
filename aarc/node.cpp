//
//  node.cpp
//  aarc
//
//  Created by Antony Searle on 15/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#include <cstddef>
#include <new>

#include "atomic.hpp"
#include "node.hpp"
#include "maybe.hpp"

#include "catch.hpp"

using u64 = std::uint64_t;
using i64 = std::int64_t;

struct A {
    ~A() { std::printf("~A\n"); }
    uint64_t a;
};

struct B {
    B() { printf("B::B\n"); }
    virtual ~B() { std::printf("~B\n"); }
};

struct C : B {
    uint64_t c;
    C() { printf("C::C\n"); }
    virtual ~C() override { std::printf("~C\n"); }
};

template<typename T>
struct D {
    A first;
    maybe<T> second;
    // T second;
};

bool flag = false;
void* operator new(std::size_t count) {
    void* p = malloc(count);
    if (flag)
        printf("malloc %.16lu at %p\n", count, p);
    return p;
}

void operator delete(void* p) noexcept {
    if (flag)
        printf("free %p\n", p);
    free(p);
}

TEST_CASE("abomination") {
    
    static_assert(alignof(B) == alignof(C));
    static_assert(offsetof(D<B>, second) == offsetof(D<C>, second));
    
    printf("%lu\n", sizeof(std::max_align_t));
    
    flag = true;
    D<C>* p = new D<C>;
    p->second.emplace();
    D<B>* q = reinterpret_cast<D<B>*>(p);
    q->second.erase();
    delete q;
    
    /*
    auto* a = new node<C>;
    a->emplace();
    auto b = (node<B>*) a;
    b->erase();
    delete a;
     */
    
    auto* c = node<B>::make<C>();
    auto* b = c;
    b->erase();
    
    printf("%lu\n", sizeof(node<C>));
    
    delete b;
    
    flag = false;

    
}
