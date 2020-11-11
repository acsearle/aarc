//
//  mutex.hpp
//  aarc
//
//  Created by Antony Searle on 13/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef mutex_hpp
#define mutex_hpp

#include <condition_variable>
#include <mutex>

namespace rust {
    
    class Condvar;
    
    template<typename T, typename M = std::mutex>
    class Mutex {
        
        mutable M _mutex;
        mutable T _payload;
        
    public:
        
        Mutex() = default;
        
        template<typename... Args>
        Mutex(Args&&... args) : _payload(std::forward<Args>(args)...) {}
        
        Mutex(Mutex const&) = delete;
        Mutex(Mutex& other) : _payload(other._payload) {}
        Mutex(Mutex&& other) :_payload(std::move(other._payload)) {}
        Mutex(Mutex const&&) = delete;
        
        class Guard {
            
            friend class Mutex;
            friend class Condvar;
            
            Mutex const* _ptr;
            
            explicit Guard(Mutex const* p) : _ptr(p) {}
            
        public:
            
            Guard() : _ptr{0} {}
            
            Guard(Guard const&) = delete;
            
            Guard(Guard&& other)
            : _ptr(std::exchange(other._ptr, nullptr)) {
            }
            
            ~Guard() {
                if (_ptr)
                    _ptr->_mutex.unlock();
            }
            
            void swap(Guard& other) {
                using std::swap;
                swap(_ptr, other._ptr);
            }
            
            Guard& operator=(Guard const&) = delete;
            
            Guard& operator=(Guard&& other) {
                Guard(std::move(other)).swap(*this);
                return *this;
            }
            
            explicit operator bool() const {
                return _ptr;
            }
            
            T* operator->() /* mutable */ {
                assert(_ptr);
                return &_ptr->_payload;
            }
            
            T& operator*() /* mutable */ {
                assert(_ptr);
                return _ptr->_payload;
            }
            
        };
        
        Guard lock() = delete;
        Guard lock() const {
            _mutex.lock();
            return Guard{this};
        }
        
        Guard try_lock() = delete;
        Guard try_lock() const {
            return Guard{_mutex.try_lock() ? this : nullptr};
        }
        
        T* operator->() {
            return &_payload;
        }
        
        Guard operator->() const {
            return lock();
        }
        
        T& operator*() {
            return _payload;
        }
        
        T into_inner() && {
            return std::move(_payload);
        }
        
    };
    
    template<typename T>
    Mutex(T&&) -> Mutex<std::decay_t<T>>;
    
    class Condvar {
        
        std::condition_variable _cv;
        
    public:
        
        template<typename T>
        void wait(typename Mutex<T>::Guard& guard) {
            auto lock = std::unique_lock(guard._ptr->_mutex, std::adopt_lock);
            auto releaser = finally([&] { lock.release(); });
            _cv.wait(lock);
        }
        
        template<typename T, typename Predicate>
        void wait(typename Mutex<T>::Guard& guard, Predicate&& predicate) {
            auto lock = std::unique_lock(guard._ptr->_mutex, std::adopt_lock);
            auto releaser = finally([&] { lock.release(); });
            while (!predicate(*guard))
                _cv.wait(lock);
        }
        
        void notify_one() {
            _cv.notify_one();
        }
        
        void notify_all() {
            _cv.notify_all();
        }
        
    };
    
} // namespace rust

#endif /* mutex_hpp */
