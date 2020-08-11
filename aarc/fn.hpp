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

#include "maybe.hpp"

// fn is a polymorphic function wrapper like std::function
//
// move-only (explicit maybe-clone)

namespace detail {

static constexpr std::uint64_t PTR = 0x0000'FFFF'FFFF'FFF0;
static constexpr std::uint64_t CNT = 0xFFFF'0000'0000'0000;
static constexpr std::uint64_t TAG = 0x0000'0000'0000'000F;
static constexpr std::uint64_t LOW = 0x0000'0000'0000'FFFF;

template<typename T>
static T* ptr(std::uint64_t v) {
    return reinterpret_cast<T*>(v & PTR);
}

static std::uint64_t cnt(std::uint64_t v) {
    return (v >> 48) + 1;
}

static std::uint64_t tag(std::uint64_t v) {
    return v & TAG;
}

static std::uint64_t val(void* p) {
    auto n = reinterpret_cast<std::uint64_t>(p);
    assert(!(n & ~PTR));
    return n;
}

static std::uint64_t val(std::uint64_t n, void* p) {
    n = (n - 1);
    assert(!(n & ~LOW));
    return (n << 48) | val(p);
}

static std::uint64_t val(std::uint64_t n, void* p, std::uint64_t t) {
    assert(!(t & ~TAG));
    return val(n, p) | t;
}

template<typename R>
struct alignas(16) node {
    
    // void* __vtbl
    
    std::atomic<std::uint64_t> _count;
    
    union {
        std::uint64_t _raw_next;
        std::atomic<std::uint64_t> _atomic_next;
    };
    
    union {
        int _fd;
        std::chrono::time_point<std::chrono::steady_clock> _t;
    };
    
    node()
    : _count(0x0000'0000'0001'0000)
    , _raw_next{0}
    , _fd(0) {}
    
    void release(std::uint64_t n) {
        auto m = _count.fetch_sub(n, std::memory_order_release);
        assert(m >= n);
        if (m == n) {
            [[maybe_unused]] auto z = _count.load(std::memory_order_acquire);
            assert(z == 0);
            delete this;
        }
    }

    virtual ~node() noexcept = default;
    virtual R call_and_erase() { abort(); }
    virtual void erase() noexcept { abort(); }
    virtual std::uint64_t try_clone() const { return CNT | val(new node); }

}; // node

template<typename R, typename T>
struct wrapper : node<R> {
    
    maybe<T> _payload;
                
    virtual ~wrapper() noexcept override final = default;
    virtual R call_and_erase() override final {
        if constexpr (std::is_same_v<R, void>) {
            _payload.get()();
            erase();
        } else {
            R r{_payload.get()()};
            erase();
            return r;
        }
    }
    virtual void erase() noexcept override final {
        _payload.erase();
    };
    
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

    ~fn() {
        auto p = detail::ptr<detail::node<R>>(_value);
        if (p) {
            p->erase();
            assert(detail::cnt(_value) == p->_count.load(std::memory_order_relaxed));
            delete p;
        }
    }
    
    void swap(fn& other) { using std::swap; swap(_value, other._value); }

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
            p->call_and_erase();
            delete p;
            return;
        } else {
            R r(p->call_and_erase());
            delete p;
            return r;
        }
    }
    
    operator bool() const {
        return _value & detail::PTR;
    }
    
    std::uint64_t tag() {
        return _value & detail::TAG;
    }
    
    void set_tag(std::uint64_t t) {
        assert(!(t & ~detail::TAG));
        _value = (_value & ~detail::TAG) | t;
    }
    
};

#endif /* fn_hpp */
