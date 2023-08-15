#pragma once

#include "std/std_precomp.h"

#define FW(...) (decltype(__VA_ARGS__) &&)(__VA_ARGS__)

namespace ks3::detail
{

using namespace std;

// enums, ints, ptrs whose underlying type is integer
template <typename T>
concept ConceptIntLike =
    (is_enum_v<T> || is_integral_v<T> || is_pointer_v<T>) &&
    (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

// Helper conception to check get<N> and and tuple_element_t are well-formed.
template<typename T, size_t N>
concept HasTupleElement = requires(T t)
{
    typename tuple_element_t<N, T>;
    { get<N>(t) } noexcept -> convertible_to<const tuple_element_t<N, T> &>;
};

// Satisfy the tuple protocol, i.e. get<N>, tuple_size_v, and tuple_element_t
// are well-formed.
//
// Tuples, pairs, and their reference, but not arrays or subranges. We offer a
// specialization for ranges below.
//
// To check a tuple-like type correctly is a bit trickier than I thought.
// For more infomation, see:
// https://stackoverflow.com/questions/68443804/c20-concept-to-check-tuple-like-types
template<class T>
concept ConceptTupleLikeButNotRange_impl =
    not ranges::range<T> &&
    requires
    { 
        typename tuple_size<T>::type; // it's a integral_constant<...>
        requires derived_from<
            tuple_size<T>, 
            integral_constant<size_t, tuple_size_v<T>>
        >;
    } &&
    []<size_t ...N>(index_sequence<N...>) {
        return (... && HasTupleElement<T, N>);
    }(make_index_sequence<tuple_size_v<T>>{});

template<class T>
concept ConceptTupleLikeButNotRange =
    ConceptTupleLikeButNotRange_impl<remove_cvref_t<T>>;

// mysterious recipe from boost:
// https://www.boost.org/doc/libs/1_82_0/libs/container_hash/doc/html/hash.html#notes_hash_combine
// more infomation:
// https://stackoverflow.com/questions/664014/what-integer-hash-function-are-good-that-accepts-an-integer-hash-key
[[nodiscard]] constexpr uint32_t Hash32(uint32_t x) noexcept
{
    x = ((x >> 16) ^ x) * 0x21f0aaadU;
    x = ((x >> 15) ^ x) * 0x735a2d97U;
    x = (x >> 15) ^ x;
    return x;
}

// mysterious recipe from boost:
// https://www.boost.org/doc/libs/1_82_0/libs/container_hash/doc/html/hash.html#notes_hash_combine
// more infomation:
// https://stackoverflow.com/questions/664014/what-integer-hash-function-are-good-that-accepts-an-integer-hash-key
[[nodiscard]] constexpr uint64_t Hash64(uint64_t x) noexcept
{
    x = ((x >> 32) ^ x) * 0xe9846af9b1a615dULL;
    x = ((x >> 32) ^ x) * 0xe9846af9b1a615dULL;
    x = (x >> 28) ^ x;
    return x;
}

// Don't cast directly to unsigned because we don't want sign extension.
// e.g. (int16_t)-1 -> (uint16_t)65535 -> (uint32_t)65535,
// other than (int16_t)-1 -> (uint32_t)4294967295
template <ConceptIntLike T>
[[nodiscard]] constexpr uint32_t ToU32WithoutSignExtension(T t) noexcept
{
    static_assert(sizeof(t) <= 4, "<= 32 bits only");
    if constexpr (sizeof(t) == 1)
    {
        return static_cast<uint32_t>(static_cast<uint8_t>(t));
    }
    else if constexpr (sizeof(t) == 2)
    {
        return static_cast<uint32_t>(static_cast<uint16_t>(t));
    }
    else
    {
        return static_cast<uint32_t>(t);
    }
}

// Don't cast directly to unsigned because we don't want sign extension.
// e.g. (int16_t)-1 -> (uint16_t)65535 -> (uint64_t)65535,
// other than (int16_t)-1 -> (uint64_t)<very big number>
template <ConceptIntLike T>
[[nodiscard]] constexpr uint64_t ToU64WithoutSignExtension(T t) noexcept
{
    if constexpr (sizeof(t) == 1)
    {
        return static_cast<uint64_t>(static_cast<uint8_t>(t));
    }
    else if constexpr (sizeof(t) == 2)
    {
        return static_cast<uint64_t>(static_cast<uint16_t>(t));
    }
    else if constexpr (sizeof(t) == 4)
    {
        return static_cast<uint64_t>(static_cast<uint32_t>(t));
    }
    else
    {
        return static_cast<uint64_t>(t);
    }
}

// Combine multiple hashes into one.
// Expands to: Hash32(0xmeow + value1 + ... + valueN), N=0,1,2...
template <ConceptIntLike ...Ints>
    requires (... and (sizeof(Ints) <= 4))
[[nodiscard]] constexpr uint32_t MixValue32(Ints ...ints) noexcept
{
    constexpr uint32_t myrandom = 0x9e3779b9U;
    return Hash32((myrandom + ... + ToU32WithoutSignExtension(ints)));
}

// Combine multiple hashes into one.
// Expands to: Hash32(0xmeow + value1 + ... + valueN), N=0,1,2...
template <ConceptIntLike ...Ints>
[[nodiscard]] constexpr uint64_t MixValue64(Ints ...ints) noexcept
{
    constexpr uint64_t myrandom = 0x3c0aad46f555e0b9ULL;
    return Hash64((myrandom + ... + ToU64WithoutSignExtension(ints)));
}

// Checksum is a hashcode of gamestate and must be identical across platforms.
// KoiSyn use that to verify a gamestate, and will not work if it computes
// differently on different platforms.
//
// The std::hash is implementation-defined, that is, it may yields different
// results on different platforms and different implementations, so we cannot
// use it to hash the gamestate which could run at any time and on any platform.
//
// Floating-points don't have exact values, which should not be used in game
// state. So we don't offer specializations for these types for now.
//
// It's a customization point object (CPO). If you want to enable it on your
// own type, you should provide a member function obj.GetChecksum() or a free
// function GetChecksum(obj). CPO is a contemporary technique for customization
// i.e. inserting some code from user into a "customization point".
// For more infomation, see:
// https://ericniebler.com/2014/10/21/customization-point-design-in-c11-and-beyond/
struct GetChecksum_fn
{
private:
    template <typename T>
    static constexpr bool alwaysFalse = false;

    enum class St { invalid, memberfn, freefn };

private:
    template <typename T>
    static consteval St GetStrategy() noexcept
    {
        constexpr bool okmemberfn = requires(T &&t)
            { { t.GetChecksum() } noexcept -> same_as<uint64_t>; };

        constexpr bool okfreefn = requires(T &&t)
            { { GetChecksum(FW(t)) } noexcept -> same_as<uint64_t>; };

        if constexpr (okmemberfn) { return St::memberfn; }
        else if constexpr (okfreefn) { return St::freefn; }
        else { return St::invalid; }
    }

public:
    template <typename T>
    [[nodiscard]] constexpr uint64_t operator()(T &&t) const noexcept
        requires (GetStrategy<T>() != St::invalid) // SFINAE
    {
        constexpr St st = GetStrategy<T>();

        if constexpr (st == St::memberfn) { return t.GetChecksum(); }
        else if constexpr (st == St::freefn) { return GetChecksum(FW(t)); }
        else
        {
            static_assert(alwaysFalse<T>,
                "no t.GetChecksum() or GetChecksum(t) provided");
        }
    }
};

// To meet this requirement, the type should:
// 1. have member fn t.GetChecksum() or free fn GetChecksum(t)
// 2. no throw (noexcept)
// 3. returns uint64_t
template <typename T>
concept ConceptChecksumCalculable = requires(T &&t)
{
    { GetChecksum_fn{}(FW(t)) } noexcept -> same_as<uint64_t>;
};

//// for those types that member function provided.
//[[nodiscard]] constexpr uint64_t GetChecksum(auto &&t) noexcept
//{
//    return t.GetChecksum();
//}

// for integer-like types.
[[nodiscard]] constexpr uint64_t GetChecksum(ConceptIntLike auto i) noexcept
{
    return Hash64(ToU64WithoutSignExtension(i));
}

// Requirement: every element in the tuple must provide a unihash
template <typename Tuple, size_t ...N>
[[nodiscard]] constexpr uint64_t ChecksumTuple_impl(
    const Tuple &tup, index_sequence<N...>) noexcept
    requires requires { MixValue64(GetChecksum(get<N>(tup))...); }
{
    // hash every element in the tuple separately and mix them
    return MixValue64(GetChecksum(get<N>(tup))...);
}

// for tuple-like but not array that every members are checksum calculable.
template <ConceptTupleLikeButNotRange Tuple>
[[nodiscard]] constexpr uint64_t GetChecksum(Tuple &&tup) noexcept
{
    // TODO: C++23 static operator
    constexpr auto impl =
        []<size_t ...N>(Tuple &&tup1, index_sequence<N...>)
        {
            return MixValue64(GetChecksum(get<N>(tup1))...);
        };
    constexpr size_t tupsize = tuple_size_v<remove_cvref_t<Tuple>>;
    return impl(FW(tup), make_index_sequence<tupsize>{});
}

// Fallback if we don't know the size of range but it's inefficient.
// Equivalent to the runtime version.
[[nodiscard]] constexpr uint64_t HashIntRange_impl(
    ranges::range auto &&rng) noexcept
{
    using T = ranges::range_value_t<decltype(rng)>;

    // We hash 8 bytes at one time, so we check how many ints can be chunked
    // by 8 bytes. e.g. for span<uint16_t, 7>, [01 23 45 67] [01 23 45]
    uint64_t hashSum = 0;
    uint64_t count = 0;
    uint64_t sum1;
    // TODO: C++23 views::chunk
    // for (auto &&ck : FW(rng) | views::chunk(8 / sizeof(T)))
    for (auto &&ck : chunk_view(FW(rng), 8 / sizeof(T)))
    {
        sum1 = 0;
        // TODO: C++23 views::enumerate
        // for (auto &&[idx, elm] : views::enumerate(ck))
        uint64_t idx = 0;
        for (auto &&elm : ck)
        {
            const uint64_t uelm = ToU64WithoutSignExtension(elm);
            sum1 |= uelm << sizeof(T) * 8 * idx;
            ++count;
            ++idx;
        }
        hashSum = MixValue64(hashSum, sum1);
    }
    return MixValue64(hashSum, count);
}

// If we know the size of the range, we can determine how many loops are needed
// at a time.
// Runtime only since memcpy is not constexpr.
template <ranges::sized_range Rng>
[[nodiscard]] uint64_t HashIntSizedRange_impl(Rng &&rng) noexcept
{
    using T = ranges::range_value_t<Rng>;

    // hash 8 bytes at one time, so we check how many ints can be chunked
    // by 8 bytes. e.g. for span<uint16_t, 7>, [01 23 45 67] [01 23 45]
    constexpr uint32_t countPer8 = static_cast<uint32_t>(8 / sizeof(T));
    const uint64_t count = ranges::size(rng);
    const uint32_t chunks = static_cast<uint32_t>(count) & ~(countPer8 - 1);
    auto now = ranges::begin(rng);
    uint64_t hashSum = 0;
    uint64_t sum1;
    for (uint32_t ck = 0; ck < chunks; ck += countPer8)
    {
        sum1 = 0;
        // use memory copy for better performance, conditionally enabled
        if constexpr (ranges::contiguous_range<Rng>
            and endian::native == endian::little)
        {
            memcpy(&sum1, to_address(now + ck), 8);
        }
        else // fall back to element-wise copy
        {
            for (uint32_t i = 0; i < countPer8; ++i)
            {
                const uint64_t elm = ToU64WithoutSignExtension(*now++);
                sum1 |= elm << sizeof(T) * 8 * i;
            }
        }
        hashSum = MixValue64(hashSum, sum1);
    }
    // An extra loop for the rest if needed.
    // We can't use memcpy at this point since the function call can't be
    // optimized to one instruction because of the unknown count of the rest
    // elements at compile time
    if (chunks != count)
    {
        sum1 = 0;
        if constexpr (ranges::contiguous_range<Rng>
            and endian::native == endian::little)
        {
            now += chunks;
        }
        for (uint32_t i = 0; i < count - chunks; ++i)
        {
            const uint64_t elm = ToU64WithoutSignExtension(*now++);
            sum1 |= elm << sizeof(T) * 8 * i;
        }
        hashSum = MixValue64(hashSum, sum1);
    }
    return MixValue64(hashSum, count);
}

template <ranges::range Rng>
[[nodiscard]] constexpr uint64_t HashRange_impl(Rng &&rng) noexcept
{
    if constexpr (ConceptIntLike<ranges::range_value_t<Rng>>)
    {
        if constexpr (ranges::sized_range<Rng>)
        {
            // TODO: use `if consteval`
            if (is_constant_evaluated()) // compile time
            {
                return HashIntRange_impl(FW(rng));
            }
            else // runtime
            {
                return HashIntSizedRange_impl(FW(rng));
            }
        }
        else // not sized_range
        {
            return HashIntRange_impl(FW(rng));
        }
    }
    else // not range of int-like elements
    {
        uint64_t hashSum = 0;
        uint64_t count = 0;
        for (const auto &elm : rng)
        {
            hashSum = MixValue64(hashSum, GetChecksum(elm));
            ++count;
        }
        return MixValue64(hashSum, count);
    }
}

// for range types.
template <ranges::range Rng>
    requires ConceptChecksumCalculable<ranges::range_value_t<Rng>>
[[nodiscard]] constexpr uint64_t GetChecksum(Rng &&rng) noexcept
{
    return HashRange_impl(FW(rng));
}

// For unordered_set whose elements is unordered, we simply sum the checksum of
// each element, and mix it with the checksum of the container size.
template <ConceptChecksumCalculable T>
[[nodiscard]] constexpr uint64_t GetChecksum(
    const unordered_set<T> &rng) noexcept
{
    uint64_t szhash = Hash64(size(rng));
    uint64_t hashSum = 0;
    for (auto &&i : rng)
    {
        hashSum += Hash64(i);
    }
    return MixValue64(szhash, hashSum);
}

// For unordered_map whose elements is unordered, we simply sum the checksum of
// each element, and mix it with the checksum of the container size.
template <ConceptChecksumCalculable K, ConceptChecksumCalculable V>
[[nodiscard]] constexpr uint64_t GetChecksum(
    const unordered_map<K, V> &rng) noexcept
{
    uint64_t szhash = Hash64(size(rng));
    uint64_t hashSum = 0;
    for (auto &&[k, v] : rng)
    {
        ranges::begin(rng);
        hashSum += Hash64(k);
        hashSum += Hash64(v);
    }
    return MixValue64(szhash, hashSum);
}

} // namespace ks3::detail


#undef FW
