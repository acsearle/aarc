//
//  atomic.hpp
//  aarc
//
//  Created by Antony Searle on 10/11/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef atomic_hpp
#define atomic_hpp

#include <atomic>

#include "atomic_wait.hpp"

namespace std {
    
    template<typename T>
    struct type_identity {
        using type = T;
    };
    
    template<typename T>
    using type_identity_t = typename type_identity<T>::type;
    
}

namespace aarc {
    
    // While we wait for atomic_ref we can
    
    template<typename T>
    T atomic_load(T* target,
                  std::memory_order order) {
        static_assert(std::atomic<T>::is_always_lock_free);
        return std::atomic_load_explicit((std::atomic<T>*) target,
                                         order);
    }
    
    template<typename T>
    void atomic_store(T* target,
                      std::type_identity_t<T> desired,
                      std::memory_order order) {
        static_assert(std::atomic<T>::is_always_lock_free);
        std::atomic_store_explicit((std::atomic<T>*) target,
                                   desired,
                                   order);
    }
    
    template<typename T>
    T atomic_exchange(T* target,
                      std::type_identity_t<T> desired,
                      std::memory_order order) {
        static_assert(std::atomic<T>::is_always_lock_free);
        return std::atomic_exchange_explicit((std::atomic<T>*) target,
                                             desired,
                                             order);
    }
    
#define A(X)\
    template<typename T>\
bool atomic_compare_exchange_##X (T* target,\
                                  std::type_identity_t<T>* expected,\
                                  std::type_identity_t<T> desired,\
                                  std::memory_order success,\
                                  std::memory_order failure) {\
        static_assert(std::atomic<T>::is_always_lock_free);\
        return std::atomic_compare_exchange_##X##_explicit((std::atomic<T>*) target,\
                                                           expected,\
                                                           desired,\
                                                           success,\
                                                           failure);\
    }
    
    A(weak)
    A(strong)
    
#undef A

#define A(X)\
    template<typename T>\
    T atomic_##X (T* target,\
                  std::type_identity_t<T> n,\
                  std::memory_order order) {\
        static_assert(std::atomic<T>::is_always_lock_free);\
        return std::atomic_##X##_explicit((std::atomic<T>*) target,\
                                          n,\
                                          order);\
    }

    A(fetch_add)
    A(fetch_sub)
    A(fetch_and)
    A(fetch_or)
    A(fetch_xor)
    
#undef A
    
    template<typename T>
    void atomic_wait(T* target,
                     std::type_identity_t<T> old,
                     std::memory_order order) {
        static_assert(std::atomic<T>::is_always_lock_free);
        return std::atomic_wait_explicit((std::atomic<T>*) target,
                                         old,
                                         order);
    }
    
    template<typename T>
    void atomic_notify_one(T* target) {
        static_assert(std::atomic<T>::is_always_lock_free);
        return std::atomic_notify_one((std::atomic<T>*) target);
    }
    
    template<typename T>
    void atomic_notify_all(T* target) {
        static_assert(std::atomic<T>::is_always_lock_free);
        return std::atomic_notify_all((std::atomic<T>*) target);
    }
    
} // namespace aarc

#endif /* atomic_hpp */
