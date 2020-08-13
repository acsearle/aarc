//
//  stack.hpp
//  aarc
//
//  Created by Antony Searle on 12/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef stack_hpp
#define stack_hpp

#include "fn.hpp"

template<typename>
struct stack;

template<typename R>
struct atomic<stack<fn<R>>> : detail::successible {
    
    static constexpr auto PTR = detail::PTR;
    
    static detail::node<R> const* cptr(u64 v) {
        return reinterpret_cast<detail::node<R> const*>(v & PTR);
    }

    static detail::node<R>* mptr(u64 v) {
        return reinterpret_cast<detail::node<R>*>(v & PTR);
    }

    atomic()
    : detail::successible{0} {
    }
    
    explicit atomic(std::uint64_t v)
    : detail::successible{v} {
    }
    
    atomic(atomic const&) = delete;
    atomic(atomic&& other)
    : detail::successible{std::exchange(other._next, 0)} {
    }

    ~atomic() {
        while (!empty())
            pop();
    }
    
    // mutable
    
    void swap(atomic& other) {
        using std::swap;
        std::swap(_next, other._next);
    }
    
    atomic& operator=(atomic const&) = delete;
    atomic& operator=(atomic&& other) {
        atomic(std::move(other)).swap(*this);
        return *this;
    }
    
    void push(fn<R> x) {
        x->_next = _next;
        _next = x._value;
        x._value = 0;
    }
    
    void splice(atomic x) {
        auto p = mptr(x._next);
        if (p) {
            while (auto q = mptr(p->_next))
                p = q;
            p->_next = _next;
            _next = x._next;
            x._next = 0;
        }
    }
    
    fn<R> pop() {
        fn<R> r{_next};
        if (r)
            _next = r->_next;
        return r;
    }
    
    bool empty() {
        return !(_next & detail::PTR);
    }
    
    void reverse() {
        atomic x;
        while (!empty())
            x.push(pop());
        x.swap(*this);
    }
    
    std::size_t size() {
        std::size_t n = 0;
        for ([[maybe_unused]] auto& _ : *this)
            ++n;
        return n;
    }
    
    void clear() {
        atomic{}.swap(*this);
    }
    
    // const
        
    void store(atomic) = delete;
    void store(atomic x) const {
        exchange(std::move(x));
    }
    
    atomic exchange(atomic x) = delete;
    atomic exchange(atomic x) const {
        return atomic{_next.exchange(std::exchange(x._next, 0), std::memory_order_acq_rel)};
    }
        
    bool push(fn<R> x) const {
        if (x) {
            u64 old = _next.load(std::memory_order_relaxed);
            x->_next = old;
            while (!_next.compare_exchange_weak(x->_next, x._value, std::memory_order_release, std::memory_order_relaxed))
                old = x->_next;
            x._value = 0;
            return !cptr(old); // <-- did we transition from empty to nonempty?
        }
        return false;
    }
    
    bool splice(atomic x) const {
        auto p = mptr(x._next);
        if (p) {
            while (auto q = mptr(p->_next))
                p = q;
            u64 old = _next.load(std::memory_order_relaxed);
            p->_next = old;
            while (!_next.compare_exchange_weak(p->_next, x._next, std::memory_order_release, std::memory_order_relaxed))
                old = p->_next;
            x._next = 0;
            return !cptr(old); // <-- did we transition from empty to nonempty?
        }
        return false;
    }
    
    atomic take() = delete;
    atomic take() const {
        return atomic{_next.exchange(0, std::memory_order_acquire)};
    }

    void wait() = delete;
    void wait() const {
        _next.wait(0, std::memory_order_acquire);
    }
    
    void notify_one() = delete;
    void notify_one() const {
        _next.notify_one();
    }

    void notify_all() = delete;
    void notify_all() const {
        _next.notify_all();
    }

    atomic& unsafe_reference() = delete;
    atomic& unsafe_reference() const {
        return const_cast<atomic&>(*this);
    }
        
    // iterators
    
    struct iterator {
        
        struct sentinel {};

        detail::successible* _ptr;
        
        iterator& operator++() {
            assert(_ptr);
            _ptr = mptr(_ptr->_next);
            return *this;
        }
        
        iterator operator++(int) {
            assert(_ptr);
            iterator tmp{_ptr};
            _ptr = mptr(_ptr->_next);
            return tmp;
        }
        
        detail::node<R>& operator*() {
            return *mptr(_ptr->_next);
        }
        
        detail::node<R>* operator->() {
            return mptr(_ptr->_next);
        }
                
        bool operator!=(iterator b) {
            return (_ptr->_next ^ b._ptr->_next) & PTR;
        }
        
        bool operator==(iterator b) {
            return !(*this != b);
        }
        
        bool operator!=(sentinel) {
            return _ptr->_next & PTR;
        }
        
        bool operator==(sentinel) {
            return !(*this != sentinel{});
        }
        
    };
    
    iterator begin() { return iterator{this}; }
    typename iterator::sentinel end() { return typename iterator::sentinel{}; }
    
    // erases element pointed to by iterator
    // after erasure, same iterator points at element after erased element
    fn<R> erase(iterator it) {
        return fn<R>{std::exchange(it._ptr->_next,
                                   mptr(it._ptr->_next)->_next)};
    }
    
    // inserts before element pointed to by iterator
    // after insertion, points at inserted element
    void insert(iterator it, fn<R> x) {
        x->_next = it._ptr->_next;
        it._ptr->_next = x._value;
        x._value = 0;
    }

};





template<typename T>
struct stack {
    
    struct node {
                
        std::atomic<std::int64_t> _count;
        std::uint64_t _next;
        maybe<T> _payload;
        
    };
    
    static constexpr std::uint64_t LO = 0x0000'FFFF'FFFF'FFFF;
    static constexpr std::uint64_t HI = 0xFFFF'0000'0000'0000;
    static constexpr std::uint64_t ST = 0x0001'0000'0000'0000;

    std::atomic<std::uint64_t> _head;
    
    stack() : _head{0} {}
    
    template<typename... Args>
    void push(Args&&... args) {
        node* ptr = new node{ 0x0000'0000'0001'0000 };
        ptr->_payload.emplace(std::forward<Args>(args)...);
        ptr->_next = _head.load(std::memory_order_relaxed);
        std::uint64_t desired = HI | (std::uint64_t) ptr;
        while (!_head.compare_exchange_weak(ptr->_next, desired, std::memory_order_release, std::memory_order_relaxed))
            ;
    }
    
    static void _release(node* ptr, std::int64_t n) {
        if (ptr->_count.fetch_sub(n, std::memory_order_release) == n) {
            ptr->_count.load(std::memory_order_acquire);
            delete ptr;
        }
    }
    
    bool try_pop(T& x) {
        std::uint64_t a = _head.load(std::memory_order_relaxed);
        while (a & LO) {
            assert(a & HI);
            std::uint64_t b = a - ST;
            if (_head.compare_exchange_weak(a, b, std::memory_order_acquire, std::memory_order_relaxed)) {
                node* ptr = (node*) (b & LO);
                do if (_head.compare_exchange_weak(b, ptr->_next, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    x = std::move(*ptr->_payload);
                    ptr->_payload.erase();
                    _release(ptr, (b >> 48) + 2);
                    return true;
                } while ((b & LO) == (a & LO));
                _release(ptr, 1);
            }
        }
        return false;
    }
    
};



#endif /* stack_hpp */
