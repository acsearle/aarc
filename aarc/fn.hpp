//
//  fn.hpp
//  aarc
//
//  Created by Antony Searle on 12/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef fn_hpp
#define fn_hpp

#include <atomic>
#include <cassert>
#include <chrono>

#include "atomic_wait.hpp"

#include "maybe.hpp"

// fn is a polymorphic function wrapper like std::function
//
// move-only (explicit maybe-clone)

template<typename T>
class atomic {
    
    union {
        T _value;
        mutable std::atomic<T> _atomic;
    };
    
public:
    
    atomic() = default;
    
    atomic(T x) : _value(x) {}
    
    atomic(atomic const&) = delete;
    atomic(atomic& other) : _value(other._value) {}
    atomic(atomic&& other) : _value(other._value) {}
    
    ~atomic() = default;
        
    atomic& operator=(T x) & { _value = x; return *this; }
    
    atomic& operator=(atomic const&) & = delete;
    atomic& operator=(atomic& other) & { _value = other._value; return *this; }
    atomic& operator=(atomic&& other) & { _value = other._value; return *this; }
    
    void swap(T& x) & { using std::swap; swap(_value, x); }
    
    // implicit conversion to T enables ++, etc.
    
    operator T&() { return _value; }
    explicit operator bool() { return _value; }
    
    // const-qualified
    
    T load(std::memory_order order) const {
        return _atomic.load(order);
    }
    
    void store(T x, std::memory_order order) const {
        _atomic.store(x, order);
    }
    
    T exchange(T x, std::memory_order order) const {
        return _atomic.exchange(order);
    }
    
    bool compare_exchange_weak(T& expected, T desired, std::memory_order success, std::memory_order failure) const {
        return _atomic.compare_exchange_weak(expected, desired, success, failure);
    }

    bool compare_exchange_strong(T& expected, T desired, std::memory_order success, std::memory_order failure) const {
        return _atomic.compare_exchange_strong(expected, desired, success, failure);
    }
    
    void wait(T old, std::memory_order order) const noexcept {
        std::atomic_wait_explicit(&_atomic, old, order);
    }
    
    void notify_one() const noexcept {
        std::atomic_notify_one(&_atomic);
    }
    
    void notify_all() const noexcept {
        std::atomic_notify_all(*_atomic);
    }
    
    T fetch_add(T x, std::memory_order order) const {
        return _atomic.fetch_add(x, order);
    }
    
    T fetch_sub(T x, std::memory_order order) const {
        return _atomic.fetch_sub(x, order);
    }
    
    T fetch_and(T x, std::memory_order order) const {
        return _atomic.fetch_and(x, order);
    }

    T fetch_or(T x, std::memory_order order) const {
        return _atomic.fetch_or(x, order);
    }
    
    T fetch_xor(T x, std::memory_order order) const {
        return _atomic.fetch_xor(x, order);
    }

    // escape hatch
    
    T& unsafe_reference() const {
        return const_cast<T&>(_value);
    }

};

template<typename T>
void swap(atomic<T>& a, T& b) {
    a.swap(b);
}

namespace detail {

static constexpr std::uint64_t PTR = 0x0000'FFFF'FFFF'FFF0;
static constexpr std::uint64_t CNT = 0xFFFF'0000'0000'0000;
static constexpr std::uint64_t TAG = 0x0000'0000'0000'000F;
static constexpr std::uint64_t LOW = 0x0000'0000'0000'FFFF;

template<typename T>
inline T* ptr(std::uint64_t v) {
    return reinterpret_cast<T*>(v & PTR);
}

inline std::uint64_t cnt(std::uint64_t v) {
    return (v >> 48) + 1;
}

inline std::uint64_t tag(std::uint64_t v) {
    return v & TAG;
}

inline std::uint64_t val(void* p) {
    auto n = reinterpret_cast<std::uint64_t>(p);
    assert(!(n & ~PTR));
    return n;
}

inline std::uint64_t val(std::uint64_t n, void* p) {
    n = (n - 1);
    assert(!(n & ~LOW));
    return (n << 48) | val(p);
}

inline std::uint64_t val(std::uint64_t n, void* p, std::uint64_t t) {
    assert(!(t & ~TAG));
    return val(n, p) | t;
}

struct successible {
    atomic<std::uint64_t> _next;
    successible() = default;
    explicit successible(std::uint64_t x) : _next{x} {}
    virtual ~successible() = default;
};

template<typename R>
struct alignas(32) node
: successible {
    
    //
    // layout:
    //
    //  0: __vtbl
    //  8: _raw_next + _atomic_next
    // 16: _count
    // 24: { _fd, _flags } + _t
        
    atomic<std::uint64_t> _count;
        
    union {
        struct {
            int _fd;
            int _flags;
        };
        std::chrono::time_point<std::chrono::steady_clock> _t;
    };
    
    node()
    : _count{0x0000'0000'0001'0000} {
    }
    
    void release(std::uint64_t n) const {
        auto m = _count.fetch_sub(n, std::memory_order_release);
        assert(m >= n);
        if (m == n) {
            [[maybe_unused]] auto z = _count.load(std::memory_order_acquire); // <-- read to synchronize with release on other threads
            assert(z == 0);
            delete this;
        }
    }

    virtual ~node() noexcept = default;
    
    virtual R call() { abort(); }
    virtual void erase() const noexcept {}
    virtual R call_and_erase() { abort(); }
    virtual void erase_and_delete() const noexcept { delete this; }
    virtual R call_and_erase_and_delete() { abort(); }
    
    virtual std::uint64_t try_clone() const { return CNT | val(new node); }

}; // node

template<typename R, typename T>
struct wrapper : node<R> {
    
    static_assert(sizeof(node<R>) == 32);
    
    maybe<T> _payload;
                
    virtual ~wrapper() noexcept override final = default;
    
    virtual R call() override final {
        if constexpr (std::is_same_v<R, void>) {
            _payload.get()();
        } else {
            return _payload.get()();
        }
    }
    
    virtual void erase() const noexcept override final {
        _payload.erase();
    }
    
    virtual R call_and_erase() override final {
        if constexpr (std::is_same_v<R, void>) {
            _payload.get()();
            erase(); // <-- nonvirtual because final
        } else {
            R r{_payload.get()()};
            erase();
            return r; // <-- "this" is now invalid but r is a stack variable
        }
    }
    
    virtual void erase_and_delete() const noexcept override final {
        _payload.erase();
        delete this;
    };

    virtual R call_and_erase_and_delete() override final {
        if constexpr (std::is_same_v<R, void>) {
            _payload.get()();
            erase_and_delete(); // <-- nonvirtual because final
            return;
        } else {
            R r{_payload.get()()};
            erase_and_delete();
            return r; // <-- "this" is now invalid but r is a stack variable
        }
    }
    
    virtual std::uint64_t try_clone() const override final {
        if constexpr (std::is_copy_constructible_v<T>) {
            auto p = new wrapper;
            p->_payload.emplace(_payload.get());
            return CNT | val(p);
        } else {
            return 0;
        }
        
    }

};

} // namespace detail

template<typename R>
struct fn {
    
    using N = detail::node<R>;
                
    std::uint64_t _value;
    
    fn()
    : _value{0} {
    }

    fn(fn const&) = delete;
    
    fn(fn&& other)
    : _value(std::exchange(other._value, 0)) {
    }
    
    explicit fn(std::uint64_t value)
    : _value(value) {
    }
    
    fn try_clone() const {
        auto p = detail::ptr<detail::node<R>>(_value);
        std::uint64_t v = 0;
        if (p) {
            v = p->try_clone();
            if (v) {
                v |= (_value & detail::TAG);
            }
        }
        return fn(v);
    }

    detail::node<R>* get() {
        return detail::ptr<detail::node<R>>(_value);
    }
    
    ~fn() {
        auto p = get();
        if (p) {
            assert(detail::cnt(_value) == p->_count.load(std::memory_order_relaxed));
            p->erase_and_delete();
        }
    }
    
    void swap(fn& other) {
        using std::swap;
        swap(_value, other._value);
    }

    fn& operator=(fn const&) = delete;
    
    fn& operator=(fn&& other) {
        fn(std::move(other)).swap(*this);
        return *this;
    }

    template<typename T>
    static fn from(T&& x) {
        auto p = new detail::wrapper<R, T>;
        p->_payload.emplace(std::forward<T>(x));
        return fn(detail::CNT | val(p));
    }
    
    static fn sentinel() {
        auto p = new detail::node<R>;
        return fn(detail::CNT | val(p));
    }
    
    R operator()() {
        auto p = detail::ptr<detail::node<R>>(_value);
        assert(p);
        assert(detail::cnt(_value) == p->_count.load(std::memory_order_relaxed));
        _value = 0;
        if constexpr (std::is_same_v<R, void>) {
            p->call_and_erase_and_delete();
            return;
        } else {
            R r(p->call_and_erase_and_delete());
            return r;
        }
    }
    
    operator bool() const {
        return _value & detail::PTR;
    }
    
    std::uint64_t tag() const {
        return _value & detail::TAG;
    }
    
    void set_tag(std::uint64_t t) {
        assert(!(t & ~detail::TAG));
        _value = (_value & ~detail::TAG) | t;
    }
    
    detail::node<R>* operator->() {
        return get();
    }
    
};

template<typename>
struct stack;

template<typename R>
struct atomic<stack<fn<R>>> : detail::successible {
        
    atomic()
    : detail::successible{0} {
    }
    
    explicit atomic(std::uint64_t v) : detail::successible{v} {}
    
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
        auto p = detail::ptr<detail::node<R>>(x._next);
        if (p) {
            while (auto q = detail::ptr<detail::node<R>>(p->_next))
                p = q;
            p->_next = _next;
            _next = x._value;
            x._value = 0;
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
        for (auto& x : *this)
            ++n;
        return n;
    }
    
    // const
        
    void store(atomic x) const {
        exchange(std::move(x));
    }
    
    atomic exchange(atomic x) const {
        return atomic{_next.exchange(std::exchange(x._head._next, 0), std::memory_order_acq_rel)};
    }
        
    void push(fn<R> x) const {
        if (x) {
            x->_next = _next.load(std::memory_order_relaxed);
            while (!_next.compare_exchange_weak(x->_next, x._value, std::memory_order_release, std::memory_order_relaxed))
                ;
            x._value = 0;
        }
    }
    
    void splice(atomic x) const {
        auto p = detail::ptr<detail::node<R>>(x._head._next);
        if (p) {
            while (auto q = detail::ptr<detail::node<R>>(p->_next))
                p = q;
            p->_next = _next.load(std::memory_order_relaxed);
            while (!_next.compare_exchange_weak(p->_next, x._value, std::memory_order_release, std::memory_order_relaxed))
                ;
            x._value = 0;
        }
    }
    
    atomic take() const {
        return atomic{_next.exchange(0, std::memory_order_acquire)};
    }

    void wait() {
        _next.wait(0, std::memory_order_acquire);
    }
    
    void notify_one() {
        _next.notify_one();
    }

    void notify_all() {
        _next.notify_all();
    }

    atomic& unsafe_reference() const {
        return const_cast<atomic&>(*this);
    }
    
    
    // iterators
    
    struct iterator {
        
        struct sentinel {};

        detail::successible* _ptr;
        
        iterator& operator++() {
            assert(_ptr);
            _ptr = detail::ptr<detail::successible>(_ptr->_next);
            return *this;
        }
        
        detail::node<R>& operator*() {
            return *detail::ptr<detail::node<R>>(_ptr->_next);
        }
        
        detail::node<R>* operator->() {
            return detail::ptr<detail::node<R>>(_ptr->_next);
        }
                
        bool operator!=(iterator b) {
            return (_ptr->_next ^ b._ptr->_next) & detail::PTR;
        }
        
        bool operator==(iterator b) {
            return !(*this != b);
        }
        
        bool operator!=(sentinel) {
            return _ptr->_next & detail::PTR;
        }
        
        bool operator==(sentinel) {
            return !(*this != sentinel{});
        }
        
    };
    
    iterator begin() { return iterator{this}; }
    typename iterator::sentinel end() { return typename iterator::sentinel{}; }

};

#endif /* fn_hpp */
