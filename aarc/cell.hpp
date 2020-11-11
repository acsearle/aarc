//
//  cell.hpp
//  aarc
//
//  Created by Antony Searle on 10/9/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef cell_hpp
#define cell_hpp

#include "common.hpp"

#include <utility>

namespace rust {
    
    template<typename T>
    class Cell {
        
        mutable T _value;
        
    public:
        
        Cell(T value) : _value(std::move(value)) {}
        Cell& operator=(T value) { _value = std::move(value); }
        T get() const { return _value; }
        T& get_mut() { return _value; }
        
    };
    
    template<typename T>
    class RefCell {
        
        mutable T _value;
        mutable isize _borrows;
        
    public:
        
        RefCell()
        : _value()
        , _borrows(0) {
        }
        
        RefCell(RefCell const& other)
        : _value(other._value)
        , _borrows(0) {
            assert(other._borrows >= 0);
        }
        
        RefCell(RefCell&& other)
        : _value(std::move(other._value))
        , _borrows(0) {
            assert(other._borrows == 0);
        }
        
        ~RefCell() {
            assert(_borrows == 0);
        }
        
        class Ref {
            
            RefCell const* _ptr;
            
            explicit Ref(RefCell const* ptr)
            : _ptr(ptr) {
                if (_ptr) {
                    assert(_ptr->_borrows >= 0);
                    ++ptr->_borrows;
                }
            }
            
        public:
            
            Ref(Ref const& other)
            : _ptr(other._ptr) {
                if (_ptr) {
                    assert(_ptr->_borrows > 0);
                    ++_ptr->_borrows;
                }
            }
            
            Ref(Ref&& other)
            : _ptr(std::exchange(other._ptr, nullptr)) {
                if (_ptr) {
                    assert(_ptr->_borrows > 0);
                }
            }
            
            ~Ref() {
                if (_ptr) {
                    assert(_ptr->_borrows > 0);
                    --_ptr->_borrows;
                }
            }
            
        };
        
        class RefMut {
            
            RefCell const* _ptr;
            
            explicit RefMut(RefCell const* ptr) : _ptr(ptr) {
                assert(ptr->_borrows == 0);
                ptr->_borrows = -1;
            }
            
        public:
            
            ~RefMut() {
                if (_ptr) {
                    assert(_ptr->_borrows == -1);
                    _ptr->_borrows = 0;
                }
            }
            
        };
        
        Ref borrow() const {
            ref(this);
        }
        
        RefMut borrow_mut() const {
            ref_mut(this);
        }
        
        operator T&() {
            assert(_borrows == 0);
            return _value;
        }
        
        void swap(RefCell const& other) const {
            assert((_borrows == 0) && (other._borrows == 0));
            using std::swap;
            swap(_value, other._value);
        }
        
        T take() const {
            replace(T());
        }
        
        T replace(T value) const {
            assert(_borrows == 0);
            using std::swap;
            swap(_value, value);
            return value;
        }
        
        T into_inner() && {
            assert(_borrows == 0);
            return std::move(_value);
        }
        
    }; // RefCell
    
} // namespacr rust

#endif /* cell_hpp */
