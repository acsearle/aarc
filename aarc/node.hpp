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

#include "counted.hpp"
#include "finally.hpp"

namespace aarc {

// provides reference counting, intrusive linked list, type erasure

template<typename T>
class alignas(std::max_align_t) node final {

    node() : count(0), next(nullptr) {}
    
public:
    
    // layout:
    //     count
    //     next
    //     __vtbl
    //     fields...
    
    mutable u64 count;
    mutable CountedPtr<node> next;
    
    node(node const&) = delete;

    ~node() { assert(count == 0); }
    
    node& operator=(node const&) = delete;
    
    template<typename U = T, typename... Args>
    static node* make(Args&&... args) {
        static_assert(alignof(node) >= alignof(U));
        static_assert(std::is_base_of_v<T, U>);
        void* p = operator new (sizeof(node) + sizeof(U));
        auto a = finally([=] { operator delete(p); });
        new (p) node;
        node* q = static_cast<node*>(p);
        auto b = finally([=] { static_cast<node*>(q)->~node(); });
        new (q + 1) U(std::forward<Args>(args)...);
        b.disarm();
        a.disarm();
        return q;
    }
    
    static node* make_empty() {
        void* p = operator new (sizeof(node));
        auto a = finally([=] { operator delete(p); });
        new (p) node;
        node* q = static_cast<node*>(p);
        return q;
    }
        
    void erase() const {
        reinterpret_cast<T const*>(this + 1)->~T();
    }
    
    u64 release(u64 n) const {
        auto m = atomic_fetch_sub(&count, n, std::memory_order_release);
        assert(m >= n);
        if (m == n) {
            [[maybe_unused]] auto z = atomic_load(&count, std::memory_order_acquire);
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
    
    T* operator->() { return reinterpret_cast<T*>(this + 1); }
    T& operator*() { return reinterpret_cast<T*>(this + 1); }
    
}; // node

} // namespace aarc

#endif /* node_hpp */
