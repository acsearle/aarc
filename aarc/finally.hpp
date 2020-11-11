//
//  finally.hpp
//  aarc
//
//  Created by Antony Searle on 17/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef finally_hpp
#define finally_hpp

#include <type_traits>
#include <utility>

// from GSL

// we can't nodiscard a constructor so we have a final_action struct made by
// a function finally

namespace gsl {
    
    namespace detail {
        
        template<typename Callable>
        class final_action {
            
            Callable _callable;
            bool _flag;
            
        public:
            
            final_action()
            : _callable()
            , _flag(false) {
            }
            
            final_action(Callable const& callable)
            : _callable(callable)
            , _flag(true) {
            }
            
            final_action(Callable&& callable) noexcept
            : _callable(std::move(callable))
            , _flag(true) {
            }
            
            final_action(final_action const&) = delete;
            
            final_action(final_action&& other) noexcept
            : _callable(std::move(other._callable))
            , _flag(std::exchange(other._flag, false)) {
            }
            
            ~final_action() noexcept {
                if (_flag)
                    _callable();
            }
            
            final_action& operator=(final_action const&) = delete;
            
            final_action& operator=(final_action&& other) noexcept {
                _callable = std::move(other._callable); // <-- likely to hit lambda irregularity
                _flag = std::exchange(other._callable, false);
            }
            
            void disarm() { _flag = false; }
            
        }; // class final_action<Callable>
        
    } // namespace detail
    
    template<typename Callable>
    [[nodiscard]] detail::final_action<std::decay_t<Callable>> finally(Callable&& callable) {
        return { std::forward<Callable>(callable) };
    }
    
}

#endif /* finally_hpp */
