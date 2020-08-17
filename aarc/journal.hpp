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
#include <mutex>
#include <thread>
#include <list>
#include <vector>
#include <map>
#include <algorithm>

class journal {
    
protected:
    
    journal() = default;
    virtual void _commit_t() = 0;
        
    static decltype(auto) _get_master() {
        thread_local static std::vector<journal*> _master;
        return _master;
    }
    
public:
    
    template<typename... Args>
    static void commit();
    
    template<typename... Args>
    static void enter(Args&&... args);
    
    template<typename... Args>
    static auto take();
        
};

template<typename... Args>
class _journal_t final : journal {
    
    friend class journal;
    
    using T = std::tuple<Args...>;
    using D = std::deque<T>;
    D _deque;
    
    static _journal_t& _get_local() {
        thread_local static _journal_t j;
        return j;
    }
    
    using L = std::list<std::pair<std::thread::id, D>>;
    using P = std::pair<std::mutex, L>;
    
    static P& _get_global() {
        static P g;
        return g;
    }
    
public:

    _journal_t() {
        _get_master().push_back(this);
    }
    
    ~_journal_t() {
        _commit_t();
    }
    
    static void enter(Args... args) {
        _get_local()._deque.emplace_back(std::forward<Args>(args)...);
    }
    
    virtual void _commit_t() override final {
        L x;
        x.emplace_back(std::this_thread::get_id(), std::move(_get_local()._deque));
        P& y = _get_global();
        auto lock = std::unique_lock{y.first};
        y.second.splice(y.second.end(), std::move(x));
    }
        
};

template<typename... Args>
void journal::enter(Args&&... args) {
    static_assert(sizeof...(Args));
    _journal_t<std::decay_t<Args>...>::enter(std::forward<Args>(args)...);
}

template<typename... Args>
void journal::commit() {
    if constexpr (sizeof...(Args)) {
        _journal_t<std::decay_t<Args>...>::commit();
    } else {
        for (auto p : _get_master()) {
            assert(p);
            p->_commit_t();
        }
    }
}

template<typename... Args>
auto journal::take() {
    _journal_t<std::decay_t<Args>...>::_get_local()._commit_t();
    auto& x = _journal_t<std::decay_t<Args>...>::_get_global();
    auto lock = std::unique_lock{x.first};
    return std::move(x.second);
}

template<typename... Args>
void flatten(std::list<std::pair<std::thread::id, std::deque<std::tuple<Args...>>>> x) {
    /*
    std::map<std::thread::id, std::vector<std::tuple<Args...>>> y;
    while (!x.empty()) {
        auto& z = y[x.front().first];
        z.reserve(z.size() + x.front().second.size());
        std::move(x.front().second.begin(),
                  x.front().second().end(),
                  std::back_inserter(z));
    }
     */
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


#endif /* journal_hpp */
