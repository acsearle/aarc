//
//  journal.hpp
//  aarc
//
//  Created by Antony Searle on 17/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef journal_hpp
#define journal_hpp

#include <deque>
#include <thread>
#include <list>
#include <vector>
#include <map>
#include <algorithm>

#include "mutex.hpp"

class journal {
    
protected:
    
    journal() = default;
    virtual void _commit_t() = 0;
        
    static std::vector<journal*>& _get_registry() {
        thread_local static std::vector<journal*> _registry;
        return _registry;
    }
    
public:
    
    template<typename... Args>
    static void commit();
    
    template<typename... Args>
    static void enter(Args&&... args);
    
    template<typename... Args>
    static auto take();
        
};

namespace detail {

template<typename... Args>
class journal_of final : journal {
    
    friend class journal;
    
    using T = std::tuple<Args...>;
    using D = std::deque<T>;
    D _deque;
    
    static journal_of& _get_local() {
        thread_local static journal_of _local;
        return _local;
    }
    
    using L = std::list<std::pair<std::thread::id, D>>;
    using M = mutex<L>;
    
    static M const& _get_global() {
        static M g;
        return g;
    }

    journal_of() {
        _get_registry().push_back(this);
    }
    
    ~journal_of() {
        _commit_t();
    }
    
    static void enter(Args... args) {
        _get_local()._deque.emplace_back(std::forward<Args>(args)...);
    }
    
    virtual void _commit_t() override final {
        L x;
        x.emplace_back(std::this_thread::get_id(), std::move(_get_local()._deque));
        auto y = _get_global().lock();
        y->splice(y->end(), std::move(x));
    }
        
};

} // class journal_of

template<typename... Args>
void journal::enter(Args&&... args) {
    static_assert(sizeof...(Args));
    detail::journal_of<std::decay_t<Args>...>::enter(std::forward<Args>(args)...);
}

template<typename... Args>
void journal::commit() {
    if constexpr (sizeof...(Args)) {
        detail::journal_of<std::decay_t<Args>...>::commit(); // <-- commit specific
    } else {
        for (auto p : _get_registry()) { // <-- commit all
            assert(p);
            p->_commit_t();
        }
    }
}

template<typename... Args>
auto journal::take() {
    detail::journal_of<std::decay_t<Args>...>::_get_local()._commit_t();
    return std::move(*detail::journal_of<std::decay_t<Args>...>::_get_global().lock());
}

template<typename... Args>
void flatten(std::list<std::pair<std::thread::id, std::deque<std::tuple<Args...>>>> x) {
    std::size_t n = 0;
    for (auto const& a : x)
        n += a.second.size();
    std::vector<std::tuple<std::thread::id, Args...>> y;
    y.reserve(n);
    for (auto& a : x) {
        std::tuple id{x.first};
        for (auto& b : x.second) {
            y.push_back(std::tuple_cat(id, std::move(b)));
        }
    }
}

template<typename... Args>
std::map<std::tuple<Args...>, std::size_t> histogram(std::vector<std::tuple<Args...>> x) {    
    std::map<std::tuple<Args...>, std::size_t> y;
    for (auto& a : x)
        ++(y[std::move(a)]);
    return y;
}


#endif /* journal_hpp */
