#pragma once

#include "std/std_precomp.h"
#include "checksum.h"

namespace ks3::detail
{

using namespace std;

// A 32 bit linear congruential generator (LCG) with a period of 1 or 2147483646
// It is a modified version of std::minstd_rand (x2 = x1 * 48271 % 2147483647)
// with the following difference:
// 1. defined at 0 and 2147483647: equal to themselves
// 2. defined on negatives: clear the sign bit to calculate then set it
struct Lcg32
{
    uint32_t seed;

    operator uint32_t &() noexcept { return seed; }

    static constexpr uint32_t Next(uint32_t x) noexcept
    {
        // use bitwise operations for better performance instead of division
        // for every 2147483648 of higher 33 bits (but less than 33 actually),
        // mod 2147483647 means plus 1 to lower 31 bits
        uint32_t sign = x & 0x80000000;
        x &= 0x7fffffff;
        uint64_t mul = x * 48271ULL;
        uint32_t hi33 = static_cast<uint32_t>(mul >> 31);
        uint32_t lo31 = static_cast<uint32_t>(mul & 0x7fffffff);
        uint32_t seed1 = hi33 + lo31;

        // minus 2147483647 if it is a carry
        // note: do nothing when equals 2147483647
        seed1 = (seed1 & 0x7fffffff) + (seed1 >> 31);

        return seed1 | sign;
    }

    constexpr uint32_t Next() noexcept
    {
        return seed = Next(seed);
    }

    [[nodiscard]] constexpr uint64_t GetChecksum() const noexcept
    {
        return ks3::detail::GetChecksum(seed);
    }
};
static_assert(sizeof(Lcg32) == sizeof(uint32_t));

// Another 64 bit version LCG with a period of 2^64, which is extremely fast;
// x2 = x1 * 2862933555777941757 + 3037000493 % 2^64
// See this link for more information:
// https://nuclear.llnl.gov/CNP/rng/rngman/node4.html
struct Lcg64
{
    uint64_t seed;

    operator uint64_t &() noexcept { return seed; }

    static constexpr uint64_t Next(uint64_t x) noexcept
    {
        return x * 2862933555777941757ULL + 3037000493ULL;
    }

    constexpr uint64_t Next() noexcept
    {
        return seed = Next(seed);
    }

    [[nodiscard]] constexpr uint64_t GetChecksum() const noexcept
    {
        return ks3::detail::GetChecksum(seed);
    }
};
static_assert(sizeof(Lcg64) == sizeof(uint64_t));

} // namespace ks3::detail
