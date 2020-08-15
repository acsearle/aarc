//
//  queue.hpp
//  aarc
//
//  Created by Antony Searle on 12/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef queue_hpp
#define queue_hpp

#include "atomic.hpp"
#include "accountant.hpp"
#include "maybe.hpp"

template<typename T>
struct queue {
    
    static constexpr std::uint64_t LO = 0x0000'FFFF'FFFF'FFFF;
    static constexpr std::uint64_t HI = 0xFFFF'0000'0000'0000;
    static constexpr std::uint64_t ST = 0x0001'0000'0000'0000;
    
    struct node {
        
        atomic<std::int64_t> _count;
        atomic<std::uint64_t> _next; // changes from zero to next node and thereafter immutable
        maybe<T> _payload;

    };
    
    atomic<std::uint64_t> _head;
    atomic<std::uint64_t> _tail;
    
    queue()
    : queue{HI | (std::uint64_t) new node{0x0000'0000'0002'0000, 0}} {
    }
    
    explicit queue(std::uint64_t sentinel)
    : _head{sentinel}, _tail{sentinel} {
    }
        
    static void _release(node const* ptr, std::int64_t n) {
        auto m = ptr->_count.fetch_sub(n, std::memory_order_release);
        assert(m >= n);
        if (m == n) {
            m = ptr->_count.load(std::memory_order_acquire); // synch with releases
            assert(m == 0);
            delete ptr;
        }
    }
    
    template<typename... Args>
    void push(Args&&... args) const {
        node* ptr_mut = new node{0x0000'0000'0002'0000, 0};
        // nodes are created with
        //     weight MAX-1 to be installed in tail
        //   + weight     1 to be awarded to the tail installing thread
        //   + weight MAX-1 to be installed in head
        //   + weight     1 to be awarded to the head installing thread
        ptr_mut->_payload.emplace(std::forward<Args>(args)...);
        std::uint64_t z = 0xFFFE'0000'0000'0000 | (std::uint64_t) ptr_mut;
        ptr_mut = nullptr;
        node const* ptr = nullptr;
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
                ptr = (node const*) (b & LO);
                c = 0;
                do if (ptr->_next.compare_exchange_weak(c, z, std::memory_order_acq_rel, std::memory_order_acquire)) {
                    // we installed a new node

                    // we can try to eagerly swing the head here
                    // not clear if this is an optimization (we do less total work)
                    // or a pessimization (we increase contention on _tail)
                    z |= HI;
                    do if (_tail.compare_exchange_weak(b, z, std::memory_order_acq_rel, std::memory_order_acquire)) {
                        // release tail's current count plus our one unit
                        _release(ptr, (b >> 48) + 2);
                        return;
                    } while ((b & LO) == (a & LO));
                     
                    // somebody else won the race
                    _release(ptr, 1); // release our one unit of old tail
                    return;
                    
                } while (!c);
                // we failed to install the node and instead must swing tail to next
                do if (_tail.compare_exchange_weak(b, c, std::memory_order_acq_rel, std::memory_order_acquire)) {
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
    
    bool try_pop(T& x) const {
        std::uint64_t a = _head.load(std::memory_order_relaxed);
        std::uint64_t b = 0;
        node const* ptr = nullptr;
        std::uint64_t c = 0;
        for (;;) {
            // _head always points to the sentinel before the (potentially empty) queue
            assert(a & LO);
            assert(a & HI); // <-- sentinel was drained, can happen if hammering on empty
            b = a - ST;
            if (_head.compare_exchange_weak(a, b, std::memory_order_acquire, std::memory_order_relaxed)) {
                // we can now read _head->_next
                ptr = (node*) (b & LO);
                c = ptr->_next.load(std::memory_order_acquire);
                if (c & LO) {
                    do if (_head.compare_exchange_weak(b, c, std::memory_order_release, std::memory_order_relaxed)) {
                        // we installed _head and have one unit of ownership of the new head node
                        _release(ptr, (b >> 48) + 2); // release old head node
                        ptr = (node*) (c & LO);
                        // we have established unique access to the payload
                        x = std::move(const_cast<T&>(*(ptr->_payload)));
                        ptr->_payload.erase();
                        _release(ptr, 1); // release new head node
                        return true;
                    } while ((b & LO) == (a & LO));
                    // somebody else swung the head, release the old one
                    _release(ptr, 1);
                    // start over with new head we just read
                    a = b;
                } else {
                    // queue is empty
                    do if (_head.compare_exchange_weak(b, b + ST, std::memory_order_relaxed, std::memory_order_relaxed)) {
                        // we put back the local weight we took
                        return false;
                    } while ((b & LO) == (a & LO));
                    // head was changed before we could return weight, so we put weight back to global count
                    _release(ptr, 1);
                    return false;
                }
            }
            // failed to acquire head, try again
        }
    }
         
    
    
    
    
    
    ~queue() {
        
        // destroy all unpopped nodes
        // tail can lag head which makes things a bit tricky

        T x;
        while (try_pop(x))
            ;

        
    }
    
};

#endif /* queue_hpp */
