//
//  fn.hpp
//  aarc
//
//  Created by Antony Searle on 12/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef fn_hpp
#define fn_hpp

#include <cassert>
#include <chrono>

#include "atomic.hpp"
#include "common.hpp"
#include "maybe.hpp"
#include "finally.hpp"
#include "counted.hpp"

using namespace aarc;

// fn is a polymorphic function wrapper like std::function
//
// move-only (explicit maybe-clone)
// reference-counted
// ready to participate in intrusive lockfree data structures

namespace detail {
    
    using namespace rust;
    using namespace aarc;
    
    // packed_ptr layout:
    
    static constexpr u64 CNT = 0xFFFF'0000'0000'0000;
    static constexpr u64 PTR = 0x0000'FFFF'FFFF'FFF0;
    static constexpr u64 TAG = 0x0000'0000'0000'000F;
    static constexpr u64 INC = 0x0001'0000'0000'0000;
    
    inline u64 cnt(u64 v) {
        return (v >> 48) + 1;
    }
    
    template<typename T>
    struct node;
    
    template<typename R, typename... Args>
    struct alignas(16) node<R(Args...)> {
        
        inline static u64 _extant{0};
        
        //
        // layout:
        //
        //  0: __vtbl
        //  8: _raw_next + _atomic_next
        // 16: _count
        // 24: { _fd, _flags } + _t + _promise
        
        mutable CountedPtr<node> _next;
        mutable u64 _count;
        
        union {
            struct {
                int _fd;
                int _flags;
            };
            std::chrono::time_point<std::chrono::steady_clock> _t;
            mutable CountedPtr<node> _promise;
        };
        
        node()
        : _next{nullptr}
        , _count{0}
        , _promise{nullptr} {
            atomic_fetch_add(&_extant, 1, std::memory_order_relaxed);
        }
        
        node(node const&) = delete;
        
        virtual ~node() noexcept {
            auto n = atomic_fetch_sub(&_extant, 1, std::memory_order_relaxed);
            assert(n > 0);
        }
        
        node& operator=(node const&) = delete;
        
        virtual u64 try_clone() const {
            auto v = reinterpret_cast<u64>(new node);
            assert(!(v & ~PTR));
            return v;
        }
        
        void acquire(u64 n) const {
            auto m = atomic_fetch_add(&_count, n, std::memory_order_relaxed);
        }
        
        void release(u64 n) const {
            auto m = atomic_fetch_sub(&_count, n, std::memory_order_release);
            assert(m >= n);
            // printf("released %p to 0x%0.5llx (-0x%0.5llx)%s\n", this, m - n, n, (m == n) ? " <--" : "");
            if (m == n) {
                [[maybe_unused]] auto o = atomic_load(&_count,
                                                      std::memory_order_acquire); // <-- read to synchronize with release on other threads
                assert(o == 0);
                delete this;
            }
        }
        
        virtual R mut_call(Args...) { abort(); }
        virtual void erase() const noexcept {}
        
        virtual void erase_and_delete() const noexcept { delete this; }
        virtual void erase_and_release(u64 n) const noexcept { release(n); }
        
        virtual R mut_call_and_erase(Args...) { abort(); }
        virtual R mut_call_and_erase_and_delete(Args...) { abort(); }
        virtual R mut_call_and_erase_and_release(u64 n, Args...) { abort(); }
        
    }; // node
    
    template<typename F, typename T>
    struct wrapper;
    
    template<typename R, typename... Args, typename T>
    struct wrapper<R(Args...), T> final : node<R(Args...)> {
        
        maybe<T> _payload;
        
        virtual ~wrapper() noexcept override final = default;
        
        virtual R mut_call(Args... args) override final {
            return _payload.value(std::forward<Args>(args)...);
        }
        
        virtual void erase() const noexcept override final {
            _payload.erase();
        }
        
        virtual R mut_call_and_erase(Args... args) override final {
            auto guard = gsl::finally([=]{ erase(); });
            return mut_call(std::forward<Args>(args)...);
        }
        
        virtual void erase_and_delete() const noexcept override final {
            _payload.erase();
            delete this;
        };
        
        virtual void erase_and_release(u64 n) const noexcept override final {
            _payload.erase();
            this->release(n); // <-- encourage inlined destructor as part of same call
        };
        
        virtual R mut_call_and_erase_and_delete(Args... args) override final {
            auto guard = gsl::finally([=]{ erase_and_delete(); });
            return mut_call(std::forward<Args>(args)...);
        }
        
        virtual R mut_call_and_erase_and_release(u64 n, Args... args) override final {
            auto guard = gsl::finally([=]{ erase_and_release(n); });
            return mut_call(std::forward<Args>(args)...);
        }
        
        virtual u64 try_clone() const override final {
            if constexpr (std::is_copy_constructible_v<T>) {
                auto p = new wrapper;
                p->_payload.emplace(_payload.value);
                auto v = reinterpret_cast<u64>(p);
                assert(!(v & ~PTR));
                return (u64) p;
            } else {
                return 0;
            }
            
        }
        
    };
    
} // namespace detail

using rust::u64;

template<typename> struct fn;

template<typename R, typename... Args>
struct fn<R(Args...)> {
    
    CountedPtr<detail::node<R(Args...)>> _value;
    
    fn() : _value{nullptr} {}
    
    fn(fn const&) = delete;
    fn(fn&) = delete;
    fn(fn&& other) : _value(std::exchange(other._value, nullptr)) {}
    fn(fn const&&) = delete;
    
    template<typename T, typename = std::void_t<decltype(std::declval<T>()())>>
    fn(T f) : _value{nullptr} {
        auto p = new detail::wrapper<R(Args...), T>;
        p->_payload.emplace(std::forward<T>(f));
        _value.ptr = p;
    }
    
    explicit fn(CountedPtr<detail::node<R(Args...)>> value)
    : _value(value) {
    }
    
    fn try_clone() const {
        auto p = _value.ptr;
        u64 v = 0;
        if (p)
            v = p->try_clone();
        return fn(v);
    }
    
    detail::node<R>* get() {
        return ptr(_value);
    }
    
    detail::node<R> const* get() const {
        return ptr(_value);
    }
    
    ~fn() {
        if (auto p = _value.ptr)
            p->erase_and_delete();
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
    
    R operator()(Args... args) {
        auto p = _value.ptr;
        assert(p);
        _value = 0;
        if constexpr (std::is_same_v<R, void>) {
            p->mut_call_and_erase_and_delete(std::forward<Args>(args)...);
            return;
        } else {
            R r{p->mut_call_and_erase_and_delete(std::forward<Args>(args)...)};
            return r;
        }
    }
    
    operator bool() const {
        return _value.ptr;
    }
    
    std::uint64_t tag() const {
        return _value & detail::TAG;
    }
    
    detail::node<R(Args...)>* operator->() {
        return _value.ptr;
    }
    
    detail::node<R(Args...)> const* operator->() const {
        return _value.ptr;
    }
    
};

#endif /* fn_hpp */
