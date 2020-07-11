//
//  aarc.hpp
//  aarc
//
//  Created by Antony Searle on 9/7/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef aarc_hpp
#define aarc_hpp

#include <atomic>
#include <cassert>
#include <cstdint>
#include <utility>
#include <iostream>
#include <cinttypes>

template<typename T>
struct Maybe {
    
    alignas(T) std::byte _value[sizeof(T)];
    
    template<typename... Args>
    void construct(Args&&... args) const {
        new ((void*) _value) T{std::forward<Args>(args)...};
    }
    
    void destroy() const {
        (*this)->~T();
    }

    T* operator->() { return reinterpret_cast<T*>(_value); }
    T const* operator->() const { return reinterpret_cast<T const*>(_value); }
    
    T& operator*() { return reinterpret_cast<T&>(*_value); }
    T const& operator*() const { return reinterpret_cast<T const&>(*_value); }

};

template<typename T>
struct Arc {
    
    struct Inner {
        std::atomic<std::int64_t> _strong;
        T _payload;
    };

    std::uint64_t _value;
    
    static constexpr std::uint64_t MASK = 0x0000'FFFF'FFFF'FFFF;
    static constexpr std::uint64_t INC  = 0x0001'0000'0000'0000;
    
    Arc()
    : _value(0) {
    }
    
    explicit Arc(std::uint64_t x)
    : _value{x} {
    }
    
    Arc(Arc&& x)
    : _value(std::exchange(x._value, 0)) {
    }
    
    Arc(Arc const& x);
    
    ~Arc() {
        auto p = reinterpret_cast<Inner*>(_value & MASK);
        auto n = (_value >> 48) + 1;
        if (p && p->_strong.fetch_sub(n, std::memory_order_release) == n) {
            p->_strong.load(std::memory_order_acquire);
            delete p;
        }
    }
    
    void swap(Arc& x) {
        using std::swap;
        swap(_value, x._value);
    }
    
    Arc& operator=(Arc&& x) {
        Arc(std::move(x)).swap(*this);
        return *this;
    }
    
    Arc& operator=(Arc const& x) {
        Arc(x).swap(*this);
        return *this;
    }
    
    T* operator->() const {
        assert(_value);
        return &(reinterpret_cast<Inner*>(_value & MASK)->_payload);
    }
    
    T& operator*() const {
        assert(_value);
        reinterpret_cast<Inner*>(_value & MASK)->_payload;
    }
    
};

template<typename T>
struct Aarc {
    
    std::atomic<std::uint64_t> _value;
    
    Aarc()
    : _value{0} {
    }

    explicit Aarc(Arc<T> x)
    : _value{std::exchange(x._value, 0)} {
    }
    
    ~Aarc() {
        Arc<T>{_value.load(std::memory_order_relaxed)};
    }

    Arc<T> load() {
        std::uint64_t expected = _value.load(std::memory_order_relaxed);
        std::uint64_t desired = 0;
        
        for (;;) {
            if (!expected) {
                return Arc<T>{};
            } else {
                // nonzero value
            }
        }
        
        do {
            desired = expected - Arc<T>::INC;
        } while (_value.compare_exchange_weak(expected,
                                              desired,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed));
        return Arc<T>{expected & Arc<T>::MASK};
    }
    
    Arc<T> exchange(Arc<T> desired) {
        desired._value = _value.exchange(desired._value,
                                         std::memory_order_acq_rel);
        return desired;
    }
    
    void store(Arc<T> desired) {
        exchange(std::move(desired));
    }
    
    bool compare_exchange_weak(Arc<T>& expected, Arc<T> desired) {
        std::uint64_t x = _value.load(std::memory_order_relaxed);
        std::uint64_t d = 0;
        for (;;) {
            
        }
        return true;
    }
    
};

/*
template<typename T>
struct Stack {
    
    struct Node {
        Aarc<Node> _next;
        T _payload;
    };
    
    Aarc<Node> _head;
    
};

template<typename T>
struct Queue {
    
    struct Node {
        Aarc<Node> _next;
        Maybe<T> _payload;
    };
    
    Aarc<Node> _head;
    Aarc<Node> _tail;
    
    void push() {
        Arc<T> desired; // = new node
        Arc<T> expected = _tail.load();
        
    }
    
    void pop() {
        Arc<T> expected = _head.load();
        while (expected && !_head.compare_exchange_weak(expected, expected->_next.load()))
            ;
    }
    
};
 */


template<typename T>
struct Stack {
    
    struct Node {
        
        inline static std::atomic<std::int64_t> _extant = 0;
        
        std::atomic<std::int64_t> _count;
        std::uint64_t _next;
        Maybe<T> _payload;
        
        Node() : _count{0x0000'0000'0001'0000} { _extant.fetch_add(1, std::memory_order_relaxed); }
        ~Node() { _extant.fetch_sub(1, std::memory_order_relaxed); }
        
    };
    
    static constexpr std::uint64_t LO = 0x0000'FFFF'FFFF'FFFF;
    static constexpr std::uint64_t HI = 0xFFFF'0000'0000'0000;
    static constexpr std::uint64_t ST = 0x0001'0000'0000'0000;

    std::atomic<std::uint64_t> _head;
    
    Stack() : _head{0} {}
    
    template<typename... Args>
    void push(Args&&... args) {
        Node* ptr = new Node;
        ptr->_payload.construct(std::forward<Args>(args)...);
        ptr->_next = _head.load(std::memory_order_relaxed);
        std::uint64_t desired = HI | (std::uint64_t) ptr;
        while (!_head.compare_exchange_weak(ptr->_next, desired, std::memory_order_release, std::memory_order_relaxed))
            ;
    }
    
    static void _release(Node* ptr, std::int64_t n) {
        if (ptr->_count.fetch_sub(n, std::memory_order_release) == n) {
            ptr->_count.load(std::memory_order_acquire);
            delete ptr;
        }
    }
    
    T pop() {
        std::uint64_t a = _head.load(std::memory_order_relaxed);
        while (a & LO) {
            assert(a & HI);
            std::uint64_t b = a - ST;
            if (_head.compare_exchange_weak(a, b, std::memory_order_acquire, std::memory_order_relaxed)) {
                Node* ptr = (Node*) (b & LO);
                do if (_head.compare_exchange_weak(b, ptr->_next, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    T x{std::move(*ptr->_payload)};
                    ptr->_payload.destroy();
                    _release(ptr, (b >> 48) + 2);
                    return x;
                } while ((b & LO) == (a & LO));
                _release(ptr, 1);
            }
        }
        return T{};
    }
    
};


template<typename T>
struct Queue {
    
    static constexpr std::uint64_t LO = 0x0000'FFFF'FFFF'FFFF;
    static constexpr std::uint64_t HI = 0xFFFF'0000'0000'0000;
    static constexpr std::uint64_t ST = 0x0001'0000'0000'0000;
    
    struct Node {
        
        std::atomic<std::int64_t> _count;
        std::atomic<std::uint64_t> _next; // changes from zero to next node and then immutable
        Maybe<T> _payload;
        
        inline static std::atomic<std::int64_t> _extant = 0;
        Node() : _count{0x0000'0000'0002'0002}, _next{0} { _extant.fetch_add(1, std::memory_order_relaxed); }
        ~Node() { _extant.fetch_sub(1, std::memory_order_relaxed); }

    };
    
    std::atomic<std::uint64_t> _head;
    std::atomic<std::uint64_t> _tail;
    
    Queue()
    : Queue{HI | (std::uint64_t) new Node/*{0x0000'0000'0002'0000, 0}*/} {
    }
    
    explicit Queue(std::uint64_t sentinel)
    : _head{sentinel}, _tail{sentinel} {
        Node* p = (Node*) (sentinel & LO);
        p->_count.fetch_sub(2);
    }
    
    static void _release(Node* ptr, std::int64_t n) {
        auto m = ptr->_count.fetch_sub(n, std::memory_order_release);
        assert(m >= n);
        if (m == n) {
            m = ptr->_count.load(std::memory_order_acquire); // synch with releases
            assert(m == 0);
            delete ptr;
        }
    }
    
    template<typename... Args>
    void push(Args&&... args) {
        Node* ptr = new Node/*{0x0000'0000'0002'0002, 0}*/;
        // nodes are created with
        //     weight MAX to be installed in tail
        //   + weight   1 to be awarded to the tail installing thread
        //   + weight MAX to be installed in head
        //   + weight   1 to be awarded to the head installing thread
        ptr->_payload.construct(std::forward<Args>(args)...);
        std::uint64_t z = HI | (std::uint64_t) ptr;
        ptr = nullptr;
        std::uint64_t a = _tail.load(std::memory_order_relaxed);
        std::uint64_t b = 0;
        std::uint64_t c = 0;
        for (;;) {
            // _tail will always be a valid pointer (points to sentinel when queue is empty)
            assert(a & LO);
            assert(a & HI);
            b = a - ST;
            if (_tail.compare_exchange_weak(a, b, std::memory_order_acquire, std::memory_order_relaxed)) {
                // we take partial ownership of _tail and can dereference it
            alpha:
                ptr = (Node*) (b & LO);
                c = 0;
                do if (ptr->_next.compare_exchange_weak(c, z, std::memory_order_release, std::memory_order_acquire)) {
                    // we installed a new node
                    // todo: move tail forward here as a possible optimization
                    _release(ptr, 1); // release tail
                    return;
                } while (!c);
                // we failed to install the node and instead must swing tail to next
                do if (_tail.compare_exchange_weak(b, c, std::memory_order_release, std::memory_order_relaxed)) {
                    // we swung tail and are awarded one unit of ownership
                    _release(ptr, (b >> 48) + 2); // release old tail
                    a = b = c;
                    // resume attempt to install new node
                    // todo: express as better control flow
                    goto alpha;
                } while ((b & LO) == (a & LO)); // allow spurious failures and changed counts
                // another thread advanced the tail
                _release(ptr, 1);
                a = b;
                // start loop over with new tail we just read
            }
            // failed to take ownership, continue
        }
    }
    
    T pop() {
        std::uint64_t a = _head.load(std::memory_order_relaxed);
        std::uint64_t b = 0;
        Node* ptr = nullptr;
        std::uint64_t c = 0;
        for (;;) {
            // _head always points to the sentinel before the (potentially empty) queue
            assert(a & LO);
            assert(a & HI); // <-- sentinel was drained, can happen if hammering on empty
             b = a - ST;
            if (_head.compare_exchange_weak(a, b, std::memory_order_acquire, std::memory_order_relaxed)) {
                // we can now read _head->_next
                ptr = (Node*) (b & LO);
                c = ptr->_next.load(std::memory_order_acquire);
                if (c & LO) {

                    do if (_head.compare_exchange_weak(b, c, std::memory_order_release, std::memory_order_relaxed)) {
                        // we installed _head and have one unit of ownership of the node
                        _release(ptr, (b >> 48) + 2);
                        ptr = (Node*) (c & LO);
                        T x{std::move(*ptr->_payload)};
                        ptr->_payload.destroy();
                        _release(ptr, 1);
                        return x;
                    } while ((b & LO) == (a & LO));
                    // somebody else swung the head, release the old one
                    _release(ptr, 1);
                    // start over with new head we just read
                    a = b;
                } else {
                    // queue is empty
                    _release(ptr, 1); // <-- this path tends to drain the sentinel's weight
                    return T{};
                }
            }
            // failed to acquire head, try again
        }
    }
                    
                    
                    
                    
                    /*
                if (_head.compare_exchange_weak(b, c, std::memory_order_release, std::memory_order_relaxed)) {
                    _release(ptr, (b >> 48) + 2);
                    ptr = (Node*) (c & LO);
                    T x{std::move(*ptr->_payload)};
                    ptr->_payload.destroy();
                    _release(ptr, 1);
                    return x;
                } else {
                    assert(false);
                }
                
            }
        }
    }*/
    
    
};

#endif /* aarc_hpp */
