//
//  y.hpp
//  aarc
//
//  Created by Antony Searle on 15/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef y_hpp
#define y_hpp

#include <type_traits>
#include <utility>

//  Y combinator enables lambdas to refer to themselves
//
//      auto fac = Y([](auto& self, int n) -> int {
//          return n ? n * self(n - 1) : 1;
//      });
//
//  It is necessary to explicitly specify the return type of the lambda if it
//  depends (circularly) on the return type of self.
//
//  Y combinators are particularly useful when self is not called directly (as
//  deep recursion can overflow the call stack) such as when a recurring task
//  needs to schedule itself.
//
//      Y([](auto& self) mutable -> void {
//          do_something();
//          submit_after(std::move(self), seconds(1));
//      });
//
//  Coroutines can solve the same problem with iteration (which itself requires
//  that the coroutine implementation supports symmetric transfer aka tail
//  calls)
//
//      for (;;) {
//          do_something();
//          co_await seconds{1};
//      }

template<typename Callable>
class Y {
    
    Callable f;
    
public:
    
    Y() = default;

    explicit Y(Callable const& x) : f(x) {}
    explicit Y(Callable& x) : f(x) {}
    explicit Y(Callable const&& x) : f(std::move(x)) {}
    explicit Y(Callable&& x) : f(std::move(x)) {}
    
    template<typename... Args>
    decltype(auto) operator()(Args&&... args) const& {
        return f(*this, std::forward<Args>(args)...);
    }

    template<typename... Args>
    decltype(auto) operator()(Args&&... args) & {
        return f(*this, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    decltype(auto) operator()(Args&&... args) const&& {
        return std::move(f)(*this, std::forward<Args>(args)...);
    }

    template<typename... Args>
    decltype(auto) operator()(Args&&... args) && {
        return std::move(f)(*this, std::forward<Args>(args)...);
    }

};

template<typename Callable>
Y(Callable&&) -> Y<std::decay_t<Callable>>;

#endif /* y_hpp */
