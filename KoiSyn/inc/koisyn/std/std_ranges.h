#pragma once

// This is a copy from MSVC STL implementation, due to C++20 doesn't support
// std::ranges::chunk_view / std::views::chunk and can't be used in UE 5.2
// (Clang 14.0 for Android NDK r25b).
// Once supported, it will be replaced with std::...

#include <tuple>
#include <type_traits>
#include <ranges>

namespace ks3::detail
{

using namespace std;
using std::ranges::input_range;
using std::ranges::range_difference_t;
using std::ranges::view;
using std::ranges::viewable_range;
using std::ranges::view_interface;
using std::ranges::iterator_t;
using std::ranges::range_value_t;
using std::ranges::range_reference_t;
using std::ranges::range_rvalue_reference_t;
using std::ranges::sentinel_t;
using std::ranges::sized_range;
using std::ranges::forward_range;

template <class _Ty>
concept _Destructible_object = is_object_v<_Ty> && destructible<_Ty>;

template <_Destructible_object _Ty, bool _Needs_operator_bool = true>
class _Non_propagating_cache { // a simplified optional that resets on copy / move
public:
    constexpr _Non_propagating_cache() noexcept {}

    constexpr ~_Non_propagating_cache() {
        if (_Engaged) {
            _Val.~_Ty();
        }
    }

    // clang-format off
    ~_Non_propagating_cache() requires is_trivially_destructible_v<_Ty> = default;
    // clang-format on

    constexpr _Non_propagating_cache(const _Non_propagating_cache&) noexcept {}

    constexpr _Non_propagating_cache(_Non_propagating_cache&& _Other) noexcept {
        if (_Other._Engaged) {
            _Other._Val.~_Ty();
            _Other._Engaged = false;
        }
    }

    constexpr _Non_propagating_cache& operator=(const _Non_propagating_cache& _Other) noexcept {
        if (addressof(_Other) == this) {
            return *this;
        }

        if (_Engaged) {
            _Val.~_Ty();
            _Engaged = false;
        }

        return *this;
    }

    constexpr _Non_propagating_cache& operator=(_Non_propagating_cache&& _Other) noexcept {
        if (_Engaged) {
            _Val.~_Ty();
            _Engaged = false;
        }

        if (_Other._Engaged) {
            _Other._Val.~_Ty();
            _Other._Engaged = false;
        }

        return *this;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
        requires _Needs_operator_bool
    {
        return _Engaged;
    }

    [[nodiscard]] constexpr _Ty& operator*() noexcept {
        return _Val;
    }
    [[nodiscard]] constexpr const _Ty& operator*() const noexcept {
        return _Val;
    }

    template <class... _Types>
    constexpr _Ty& _Emplace(_Types&&... _Args) noexcept(is_nothrow_constructible_v<_Ty, _Types...>) {
        if (_Engaged) {
            _Val.~_Ty();
            _Engaged = false;
        }

        _Construct_in_place(_Val, forward<_Types>(_Args)...);
        _Engaged = true;

        return _Val;
    }

private:
    union {
        _Ty _Val;
    };
    bool _Engaged = false;
};

template <class _Fn, class... _Types>
class _Range_closure {
public:
    // We assume that _Fn is the type of a customization point object. That means
    // 1. The behavior of operator() is independent of cvref qualifiers, so we can use `invocable<_Fn, ` without
    //    loss of generality, and
    // 2. _Fn must be default-constructible and stateless, so we can create instances "on-the-fly" and avoid
    //    storing a copy.

    template <class... _UTypes>
        requires (same_as<decay_t<_UTypes>, _Types> && ...)
    constexpr explicit _Range_closure(_UTypes&&... _Args) noexcept(
        conjunction_v<is_nothrow_constructible<_Types, _UTypes>...>)
        : _Captures(forward<_UTypes>(_Args)...) {}

    void operator()(auto&&) &       = delete;
    void operator()(auto&&) const&  = delete;
    void operator()(auto&&) &&      = delete;
    void operator()(auto&&) const&& = delete;

    using _Indices = index_sequence_for<_Types...>;

    template <class _Ty>
        requires invocable<_Fn, _Ty, _Types&...>
    constexpr decltype(auto) operator()(_Ty&& _Arg) & noexcept(
        noexcept(_Call(*this, forward<_Ty>(_Arg), _Indices{}))) {
        return _Call(*this, forward<_Ty>(_Arg), _Indices{});
    }

    template <class _Ty>
        requires invocable<_Fn, _Ty, const _Types&...>
    constexpr decltype(auto) operator()(_Ty&& _Arg) const& noexcept(
        noexcept(_Call(*this, forward<_Ty>(_Arg), _Indices{}))) {
        return _Call(*this, forward<_Ty>(_Arg), _Indices{});
    }

    template <class _Ty>
        requires invocable<_Fn, _Ty, _Types...>
    constexpr decltype(auto) operator()(_Ty&& _Arg) && noexcept(
        noexcept(_Call(move(*this), forward<_Ty>(_Arg), _Indices{}))) {
        return _Call(move(*this), forward<_Ty>(_Arg), _Indices{});
    }

    template <class _Ty>
        requires invocable<_Fn, _Ty, const _Types...>
    constexpr decltype(auto) operator()(_Ty&& _Arg) const&& noexcept(
        noexcept(_Call(move(*this), forward<_Ty>(_Arg), _Indices{}))) {
        return _Call(move(*this), forward<_Ty>(_Arg), _Indices{});
    }

private:
    template <class _SelfTy, class _Ty, size_t... _Idx>
    static constexpr decltype(auto) _Call(_SelfTy&& _Self, _Ty&& _Arg, index_sequence<_Idx...>) noexcept(
        noexcept(_Fn{}(forward<_Ty>(_Arg), get<_Idx>(forward<_SelfTy>(_Self)._Captures)...))) {
        return _Fn{}(forward<_Ty>(_Arg), get<_Idx>(forward<_SelfTy>(_Self)._Captures)...);
    }

    tuple<_Types...> _Captures;
};

template <view _Vw>
    requires input_range<_Vw>
class chunk_view : public view_interface<chunk_view<_Vw>> {
private:
    /* [[no_unique_address]] */ _Vw _Range;
    range_difference_t<_Vw> _Count;
    range_difference_t<_Vw> _Remainder = 0;
    _Non_propagating_cache<iterator_t<_Vw>> _Current{};

    class _Inner_iterator {
    private:
        friend chunk_view;
        chunk_view* _Parent{};

        constexpr explicit _Inner_iterator(chunk_view* _Parent_) noexcept : _Parent(_Parent_) {}

        [[nodiscard]] constexpr range_difference_t<_Vw> _Get_remainder() const noexcept {
            return _Parent->_Remainder;
        }

        [[nodiscard]] constexpr range_difference_t<_Vw> _Get_size() const
            noexcept(noexcept(std::ranges::end(_Parent->_Range) - *_Parent->_Current)) {
            return (min)(_Parent->_Remainder, std::ranges::end(_Parent->_Range) - *_Parent->_Current);
        }

        [[nodiscard]] constexpr const iterator_t<_Vw>& _Get_current() const noexcept {
            return *_Parent->_Current;
        }

    public:
        using iterator_concept = input_iterator_tag;
        using difference_type  = range_difference_t<_Vw>;
        using value_type       = range_value_t<_Vw>;

        _Inner_iterator(_Inner_iterator&&)            = default;
        _Inner_iterator& operator=(_Inner_iterator&&) = default;

        [[nodiscard]] constexpr const iterator_t<_Vw>& base() const& noexcept /* strengthened */ {
            return *_Parent->_Current;
        }

        [[nodiscard]] constexpr range_reference_t<_Vw> operator*() const
            noexcept(noexcept(**_Parent->_Current)) /* strengthened */ {
            return **_Parent->_Current;
        }

        constexpr _Inner_iterator& operator++() noexcept(noexcept(++*_Parent->_Current) && noexcept(
            static_cast<bool>(*_Parent->_Current == std::ranges::end(_Parent->_Range)))) /* strengthened */ {
            ++*_Parent->_Current;
            if (*_Parent->_Current == std::ranges::end(_Parent->_Range)) {
                _Parent->_Remainder = 0;
            } else {
                --_Parent->_Remainder;
            }

            return *this;
        }

        constexpr void operator++(int) noexcept(noexcept(++*_Parent->_Current) && noexcept(
            static_cast<bool>(*_Parent->_Current == std::ranges::end(_Parent->_Range)))) /* strengthened */ {
            ++*_Parent->_Current;
            if (*_Parent->_Current == std::ranges::end(_Parent->_Range)) {
                _Parent->_Remainder = 0;
            } else {
                --_Parent->_Remainder;
            }
        }

        [[nodiscard]] friend constexpr bool operator==(const _Inner_iterator& _Left, default_sentinel_t) noexcept
            /* strengthened */ {
            return _Left._Get_remainder() == 0;
        }

        [[nodiscard]] friend constexpr range_rvalue_reference_t<_Vw> iter_move(const _Inner_iterator& _It) noexcept(
            noexcept(std::ranges::iter_move(_It._Get_current()))) {
            return std::ranges::iter_move(_It._Get_current());
        }

        friend constexpr void iter_swap(const _Inner_iterator& _Left, const _Inner_iterator& _Right) noexcept(
            noexcept(std::ranges::iter_swap(_Left._Get_current(), _Right._Get_current())))
            requires indirectly_swappable<iterator_t<_Vw>>
        {
            std::ranges::iter_swap(_Left._Get_current(), _Right._Get_current());
        }
    };

    class _Outer_iterator {
    private:
        friend chunk_view;
        chunk_view* _Parent{};

        constexpr explicit _Outer_iterator(chunk_view* _Parent_) noexcept : _Parent(_Parent_) {}

        [[nodiscard]] constexpr bool _Is_end() const
            noexcept(noexcept(*_Parent->_Current == std::ranges::end(_Parent->_Range) && true)) {
            return *_Parent->_Current == std::ranges::end(_Parent->_Range) && _Parent->_Remainder != 0;
        }

        [[nodiscard]] constexpr range_difference_t<_Vw> _Get_size() const
            noexcept(noexcept(std::ranges::end(_Parent->_Range) - *_Parent->_Current)) {
            const auto _Size = std::ranges::end(_Parent->_Range) - *_Parent->_Current;
            if (_Size < _Parent->_Remainder) {
                return _Size == 0 ? 0 : 1;
            }
            return _Div_ceil(_Size - _Parent->_Remainder, _Parent->_Count) + 1;
        }

    public:
        using iterator_concept = input_iterator_tag;
        using difference_type  = range_difference_t<_Vw>;

        struct value_type : view_interface<value_type> {
        private:
            friend _Outer_iterator;
            chunk_view* _Parent{};

            constexpr explicit value_type(chunk_view* _Parent_) noexcept : _Parent(_Parent_) {}

        public:
            [[nodiscard]] constexpr _Inner_iterator begin() const noexcept {
                return _Inner_iterator{_Parent};
            }

            [[nodiscard]] constexpr default_sentinel_t end() const noexcept {
                return default_sentinel;
            }

            [[nodiscard]] constexpr auto size() const
                noexcept(noexcept(std::ranges::end(_Parent->_Range) - *_Parent->_Current)) /* strengthened */
                requires sized_sentinel_for<sentinel_t<_Vw>, iterator_t<_Vw>>
            {
                return _To_unsigned_like(
                    (min)(_Parent->_Remainder, std::ranges::end(_Parent->_Range) - *_Parent->_Current));
            }
        };

        _Outer_iterator(_Outer_iterator&&)            = default;
        _Outer_iterator& operator=(_Outer_iterator&&) = default;

        [[nodiscard]] constexpr value_type operator*() const noexcept /* strengthened */ {
            return value_type{_Parent};
        }

        constexpr _Outer_iterator& operator++() /* not strengthened, see std::ranges::advance */ {
            std::ranges::advance(*_Parent->_Current, _Parent->_Remainder, std::ranges::end(_Parent->_Range));
            _Parent->_Remainder = _Parent->_Count;
            return *this;
        }

        constexpr void operator++(int) /* not strengthened, see std::ranges::advance */ {
            std::ranges::advance(*_Parent->_Current, _Parent->_Remainder, std::ranges::end(_Parent->_Range));
            _Parent->_Remainder = _Parent->_Count;
        }

        [[nodiscard]] friend constexpr bool operator==(const _Outer_iterator& _Left, default_sentinel_t) noexcept(
            noexcept(_Left._Is_end())) /* strengthened */ {
            return _Left._Is_end();
        }
    };

public:
    constexpr explicit chunk_view(_Vw _Range_, const range_difference_t<_Vw> _Count_) noexcept(
        is_nothrow_move_constructible_v<_Vw>) /* strengthened */
        : _Range(move(_Range_)), _Count{_Count_} {
    }

    [[nodiscard]] constexpr _Vw base() const& noexcept(is_nothrow_copy_constructible_v<_Vw>) /* strengthened */
        requires copy_constructible<_Vw>
    {
        return _Range;
    }

    [[nodiscard]] constexpr _Vw base() && noexcept(is_nothrow_move_constructible_v<_Vw>) /* strengthened */ {
        return move(_Range);
    }

    [[nodiscard]] constexpr _Outer_iterator begin() noexcept(
        noexcept(_Current._Emplace(std::ranges::begin(_Range)))) /* strengthened */ {
        _Current._Emplace(std::ranges::begin(_Range));
        _Remainder = _Count;
        return _Outer_iterator{this};
    }

    [[nodiscard]] constexpr default_sentinel_t end() const noexcept {
        return default_sentinel;
    }

    [[nodiscard]] constexpr auto size() noexcept(noexcept(std::ranges::distance(_Range))) /* strengthened */
        requires sized_range<_Vw>
    {
        return _To_unsigned_like(_Div_ceil(std::ranges::distance(_Range), _Count));
    }

    [[nodiscard]] constexpr auto size() const noexcept(noexcept(std::ranges::distance(_Range))) /* strengthened */
        requires sized_range<const _Vw>
    {
        return _To_unsigned_like(_Div_ceil(std::ranges::distance(_Range), _Count));
    }
};

struct _Chunk_fn {
    // clang-format off
    template <viewable_range _Rng>
    [[nodiscard]] constexpr auto operator()(_Rng&& _Range, const range_difference_t<_Rng> _Count) const noexcept(
        noexcept(chunk_view(forward<_Rng>(_Range), _Count))) requires requires {
        chunk_view(forward<_Rng>(_Range), _Count);
    } {
        // clang-format on
        return chunk_view(forward<_Rng>(_Range), _Count);
    }

    template <class _Ty>
        requires constructible_from<decay_t<_Ty>, _Ty>
    [[nodiscard]] constexpr auto operator()(_Ty&& _Count) const
        noexcept(is_nothrow_constructible_v<decay_t<_Ty>, _Ty>) {
        return _Range_closure<_Chunk_fn, decay_t<_Ty>>{forward<_Ty>(_Count)};
    }
};

template <class _Rng>
chunk_view(_Rng&&, range_difference_t<_Rng>) -> chunk_view<views::all_t<_Rng>>;

} // namespace ks3::detail

namespace std::ranges
{
    template <class _Vw>
    inline constexpr bool enable_borrowed_range<ks3::detail::chunk_view<_Vw>> = enable_borrowed_range<_Vw> && forward_range<_Vw>;
}
