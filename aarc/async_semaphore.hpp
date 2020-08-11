//
//  async_semaphore.hpp
//  aarc
//
//  Created by Antony Searle on 18/7/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef async_semaphore_hpp
#define async_semaphore_hpp

#include <stdio.h>

#include "aarc.hpp"




struct AsyncSemaphore {
    
    static constexpr std::uint64_t LO = 0x0000'FFFF'FFFF'FFFF;
    static constexpr std::uint64_t HI = 0xFFFF'0000'0000'0000;
    static constexpr std::uint64_t ST = 0x0001'0000'0000'0000;
    
    struct Node {
        
        std::atomic<std::int64_t> _count;
        std::atomic<std::uint64_t> _next; // changes from zero to next node and then immutable
        Accountant _auditor;
        
        Node() : _count{0x0000'0000'0002'0000}, _next{0} {}
        virtual ~Node() = default;
        virtual void operator()() { assert(false); /* sentinel was executed */ };
        
    };
    
    template<typename T>
    struct Task : Node {
        Maybe<T> _payload;
        
        virtual void operator()() override {
            (*_payload)();
            _payload.destroy();
        }
        
    };
    
    std::atomic<std::uint64_t> _head;
    std::atomic<std::uint64_t> _tail;
    
    AsyncSemaphore()
    : AsyncSemaphore{HI | (std::uint64_t) new Node} {
    }
    
    explicit AsyncSemaphore(std::uint64_t sentinel)
    : _head{sentinel}, _tail{sentinel} {
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
    
    template<typename Callable>
    void wait_async(Callable&& f) {
        std::uint64_t z;
        {
            auto ptr = new Task<std::decay_t<Callable>>;
            ptr->_payload.construct(std::forward<Callable>(f));
            z = 0xFFFE'0000'0000'0000 | (std::uint64_t) ptr;
        }
        Node* ptr;
        std::uint64_t a = _tail.load(std::memory_order_relaxed);
        std::uint64_t b;
        std::uint64_t c;
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

                    // we can try to eagerly swing the head here
                    // not clear if this is an optimization (we do less total work)
                    // or a pessimization (we increase contention on _tail)
                    z |= HI;
                    do if (_tail.compare_exchange_weak(b, z, std::memory_order_release, std::memory_order_relaxed)) {
                        // release tail's current count plus our one unit
                        _release(ptr, (b >> 48) + 2);
                        return;
                    } while ((b & LO) == (a & LO));
                     
                    // somebody else won the race
                    _release(ptr, 1); // release our one unit of old tail
                    return;
                    
                } while (!c);
                // c is not null
                
                
                // fixme: interleave instead
                while (!(c & LO) && (c & HI)) {
                    // try and decrement
                    if (_tail.compare_exchange_weak(c, c - ST, std::memory_order_relaxed, std::memory_order_relaxed)) {
                        _release(ptr, 1);
                        ptr = (Node*) (z & LO);
                        (*ptr)();
                        _release(ptr, 0x2'000);
                        return;
                    }
                }
                
                
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
    
    void notify() {
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
                do if (c & LO) {

                    do if (_head.compare_exchange_weak(b, c, std::memory_order_release, std::memory_order_relaxed)) {
                        // we installed _head and have one unit of ownership of the new head node
                        _release(ptr, (b >> 48) + 2); // release old head node
                        ptr = (Node*) (c & LO);
                        (*ptr)();
                        _release(ptr, 1); // release new head node
                        return;
                    } while ((b & LO) == (a & LO));
                    // somebody else swung the head, release the old one
                    _release(ptr, 1);
                    // start over with new head we just read
                    a = b;
                    break;
                } else {

                    // queue is empty
                    do if (ptr->_next.compare_exchange_weak(c, c + ST, std::memory_order_relaxed, std::memory_order_relaxed)) {
                        // increment the semaphore
                        do if (_head.compare_exchange_weak(b, b + ST, std::memory_order_relaxed, std::memory_order_relaxed)) {
                            // we put back the local weight we took
                            return;
                        } while ((b & LO) == (a & LO));
                        // head was changed before we could return weight, so we put weight back to global count
                        _release(ptr, 1);
                        return;
                    } while (!(c & LO));
                    
                } while (true);
            }
            // failed to acquire head, try again
        }
    }
                    
    ~AsyncSemaphore() {
        
        // destroy all unpopped nodes
        // tail can lag head which makes things a bit tricky
        
        
    }
    
    
};


/*
struct async_semaphore {
    
    // queue of tasks with additional empty states
    //
    // +3 +2 +1 0 item item item
    
    
    
    struct Node {
        
        std::atomic<std::int64_t> _count;
        std::atomic<std::uintptr_t> _next;
        
        virtual void operator()() {}
        virtual ~Node() = default;
        
    };
    
    template<typename Callable>
    struct Task : Node {
        Callable _f;
        virtual void operator()() {
            _f();
        }
    };

    std::atomic<std::uintptr_t> _head;

    constexpr static std::uintptr_t HI = 0xFFFF'0000'0000'0000;
    constexpr static std::uintptr_t LO = 0x0000'FFFF'FFFF'FFFF;
    constexpr static std::uintptr_t ST = 0x0001'0000'0000'0000;
    
    void _release(Node* ptr, std::intptr_t count);
    
    void notify() {
        std::uintptr_t a = _head.load(std::memory_order_relaxed);
        for (;;) {
            assert(a & LO);
            std::uintptr_t b = a + ST;
            if (!_head.compare_exchange_weak(a, b, std::memory_order_acquire, std::memory_order_relaxed)) {
                Node* ptr = (Node*) (b & LO);
                std::uintptr_t c = ptr->_next.load(std::memory_order_acquire);
                if (c & LO) {
                    // pop item
                } else {
                    // queue is empty
                    if (ptr->_next.compare_exchange_weak(c, c + ST)) {
                        // attempt to replenish head
                        return;
                    }
                }
            }
        }
        
    }

    template<typename T>
    void wait_async(T&& x) {
        
    }
    
    
    
    
    
};

*/

#endif /* async_semaphore_hpp */
