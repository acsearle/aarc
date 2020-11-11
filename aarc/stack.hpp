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

using rust::u64;
using namespace aarc;

template<typename>
struct stack;

template<typename R, typename... Args>
struct stack<fn<R(Args...)>> {
    
    static constexpr auto PTR = detail::PTR;
    
    static detail::node<R(Args...)> const* cptr(u64 v) {
        return reinterpret_cast<detail::node<R(Args...)> const*>(v & PTR);
    }

    static detail::node<R(Args...)>* mptr(u64 v) {
        return reinterpret_cast<detail::node<R(Args...)>*>(v & PTR);
    }
    
    mutable u64 _head;

    stack()
    : _head{0} {
    }
    
    explicit stack(std::uint64_t v)
    : _head{v} {
    }
    
    stack(stack const&) = delete;
    stack(stack&& other)
    : _head{std::exchange(other._head, 0)} {
    }

    ~stack() {
        while (!empty())
            pop();
    }
    
    // mutable
    
    void swap(stack& other) {
        using std::swap;
        std::swap(_head, other._head);
    }
    
    stack& operator=(stack const&) = delete;
    stack& operator=(stack&& other) {
        stack{std::move(other)}.swap(*this);
        return *this;
    }
    
    void push(fn<R(Args...)> x) {
        x->_next = _head;
        _head = x._value;
        x._value = 0;
    }
    
    void splice(stack x) {
        auto p = mptr(x._head);
        if (p) {
            while (auto q = mptr(p->_next))
                p = q;
            p->_next = _head;
            _head = x._head;
            x._head = 0;
        }
    }
    
    fn<R(Args...)> pop() {
        fn<R(Args...)> tmp{_head};
        if (tmp)
            _head = tmp->_next;
        return tmp;
    }
    
    bool empty() {
        return !(_head & detail::PTR);
    }
    
    void reverse() {
        stack x;
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
        stack{}.swap(*this);
    }
    
    // const
        
    void store(stack) = delete;
    void store(stack x) const {
        exchange(std::move(x));
    }
    
    stack exchange(stack x) = delete;
    stack exchange(stack x) const {
        return stack { atomic_exchange(&_head, std::exchange(x._next, 0), std::memory_order_acq_rel) };
    }
        
    bool push(fn<R(Args...)> x) const {
        if (x) {
            u64 old = atomic_load(&_head, std::memory_order_relaxed);
            x->_next = old;
            while (!atomic_compare_exchange_weak(&_head, &x->_next, x._value, std::memory_order_release, std::memory_order_relaxed))
                old = x->_next;
            x._value = 0;
            return !cptr(old); // <-- did we transition from empty to nonempty?
        }
        return false;
    }
    
    bool splice(stack x) const {
        auto p = mptr(x._head);
        if (p) {
            while (auto q = mptr(p->_next))
                p = q;
            u64 old = atomic_load(&_head, std::memory_order_relaxed);
            p->_next = old;
            while (!atomic_compare_exchange_weak(&_head, &p->_next, x._head, std::memory_order_release, std::memory_order_relaxed))
                old = p->_next;
            x._head = 0;
            return !cptr(old); // <-- did we transition from empty to nonempty?
        }
        return false;
    }
    
    stack take() = delete;
    stack take() const {
        return stack{atomic_exchange(&_head, (u64) 0, std::memory_order_acquire)};
    }

    void wait() = delete;
    void wait() const {
        atomic_wait(&_head, 0, std::memory_order_acquire);
    }
    
    void notify_one() = delete;
    void notify_one() const {
        atomic_notify_one(&_head);
    }

    void notify_all() = delete;
    void notify_all() const {
        atomic_notify_all(&_head);
    }

    stack& unsafe_reference() = delete;
    stack& unsafe_reference() const {
        return const_cast<stack&>(*this);
    }
        
    // iterators
    
    struct sentinel {};
    
    struct iterator {
        
        u64* _ptr;
        
        iterator& operator++() {
            assert(_ptr);
            _ptr = &mptr(*_ptr)->_next;
            return *this;
        }
        
        iterator operator++(int) {
            assert(_ptr);
            iterator tmp{_ptr};
            operator++();
            return tmp;
        }
        
        detail::node<R(Args...)>& operator*() {
            return *mptr(*_ptr);
        }
        
        detail::node<R(Args...)>* operator->() {
            return mptr(*_ptr);
        }
                
        bool operator!=(iterator b) {
            return ((*_ptr) ^ (*b._ptr)) & PTR;
        }
        
        bool operator==(iterator b) {
            return !(*this != b);
        }
        
        bool operator!=(sentinel) {
            return *_ptr & PTR;
        }
        
        bool operator==(sentinel) {
            return !(*this != sentinel{});
        }
        
    };
    
    iterator begin() { return iterator{&_head}; }
    sentinel end() { return sentinel{}; }
    
    // erases element pointed to by iterator
    // after erasure, same iterator points at element after erased element
    fn<R(Args...)> erase(iterator it) {
        return fn<R(Args...)>{std::exchange(*it._ptr,
                                   mptr(*it._ptr)->_next)};
    }
    
    // inserts before element pointed to by iterator
    // after insertion, points at inserted element
    void insert(iterator it, fn<R(Args...)> x) {
        x->_next = *it._ptr;
        *it._ptr = x._value;
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
                    x = std::move(ptr->_payload.value);
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
