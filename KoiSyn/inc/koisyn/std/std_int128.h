#pragma once

#include <cstdint>

#ifndef UINT128_MAX

#if __has_include(<__msvc_int128.hpp>)

#include <__msvc_int128.hpp>
namespace ks3::detail
{
    using int128_t = std::_Signed128;
    using uint128_t = std::_Unsigned128;
}

#else // ^^^ __has_include(<__msvc_int128.hpp>) / not __has_include(<__msvc_int128.hpp>) vvv

namespace ks3::detail
{
    using int128_t = __int128;
    using uint128_t = unsigned __int128;
}

#endif // ^^^ __has_include(<__msvc_int128.hpp>) ^^^
#endif // UINT128_MAX
