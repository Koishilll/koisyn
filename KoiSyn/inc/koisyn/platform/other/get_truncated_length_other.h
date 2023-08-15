#pragma once

namespace ks3::detail
{

inline int GetTruncatedLength(int lenOrErrc, size_t) noexcept
{
    return lenOrErrc;
}

} // namespace ks3::detail
