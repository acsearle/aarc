//
//  drop.hpp
//  aarc
//
//  Created by Antony Searle on 17/8/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef drop_hpp
#define drop_hpp

#include <utility>

template<typename T> void drop(T& x) { T(std::move(x)); }
template<typename T> void drop(T&& x) { drop(x); }
template<typename T> void drop(T const& x) = delete;
template<typename T> void drop(T const&& x) = delete;

#endif /* drop_hpp */
