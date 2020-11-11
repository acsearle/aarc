//
//  common.hpp
//  aarc
//
//  Created by Antony Searle on 9/11/20.
//  Copyright © 2020 Antony Searle. All rights reserved.
//

#ifndef common_hpp
#define common_hpp

#include <cstddef>
#include <cstdint>

namespace rust {
    
    using i8  = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;

    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;
    
    using isize = std::intptr_t;
    using usize = std::uintptr_t;
    
} // namespace rust

namespace aarc {
    
    template<std::size_t BITS>
    struct unsigned_of_width;
    
    template<> struct unsigned_of_width< 8> { using type = std::uint8_t; };
    template<> struct unsigned_of_width<16> { using type = std::uint16_t; };
    template<> struct unsigned_of_width<32> { using type = std::uint32_t; };
    template<> struct unsigned_of_width<64> { using type = std::uint64_t; };
    
    template<std::size_t BITS>
    using unsigned_of_width_t = typename unsigned_of_width<BITS>::type;
    
} // namespace aarc

#endif /* common_hpp */
