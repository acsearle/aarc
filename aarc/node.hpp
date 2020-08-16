//
//  node.hpp
//  aarc
//
//  Created by Antony Searle on 15/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef node_hpp
#define node_hpp

#include <memory>
#include <new>

#include "atomic.hpp"

// provides reference counting, intrusive linked list
//

template<typename T>
struct node final {
    
    atomic<u64> count;
    atomic<u64> next;

    union {
        alignas(std::max_align_t) T value;
    };
    
    node() : count(0x0000'0000'0000'0000), next(0) {}
    
    node(node const&) = delete;

    ~node() { assert(count == 0); }
    
    node& operator=(node const&) = delete;
    
    template<typename... Args>
    void emplace(Args&&... args) {
        new (&value) T(std::forward<Args>(args)...);
    }
    
    void erase() const {
        (&value)->~T(); // <-- calls virtual destructor if T has one
    }
    
    u64 release(u64 n) const {
        auto m = count.fetch_sub(n, std::memory_order_release);
        assert(m >= n);
        if (m == n) {
            [[maybe_unused]] auto z = count.load(std::memory_order_acquire);
            assert(z == 0);
            delete this;
        }
        return m - n;
    }
    
    u64 erase_and_release(u64 n) const {
        erase();
        return release(n);
    }
    
    void erase_and_delete() const {
        erase();
        delete this;
    }
    
}; // node<T>

/*
template<typename T>
struct alignas(std::max_align_t) node {
    
    atomic<i64> _count;
    atomic<u64> _next;
    
    struct _helper {
        alignas(node) unsigned char _node[sizeof(node)];
        alignas(T) unsigned char _T[sizeof(T)];
    };
    
    void* operator new(std::size_t count) {
        return ::operator new(sizeof(_helper), std::align_val_t{alignof(_helper)});
    }
    
    T* get() { &reinterpret_cast<_helper*>(this)->_T; }
    T const* get() const { &reinterpret_cast<_helper const*>(this)->_T; }

    template<typename... Args>
    void emplace(Args&&... args) {
        new (get()) T(std::forward<Args>(args)...);
    }
    
    void erase() const {
        get()->~T();
    }
    
    void release(u64 n) const;
    
    void erase_and_delete() {
        erase();
        delete this;
    }
    
    void erase_and_release(u64 n) const {
        erase();
        release(n);
    }
    
};
 
 */



#endif /* node_hpp */
