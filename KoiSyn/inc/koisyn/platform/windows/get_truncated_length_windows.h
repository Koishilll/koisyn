#pragma once

namespace ks3::detail
{

inline int GetTruncatedLength(int lenOrErrc, size_t bufferSize) noexcept
{
    int err = GETSOCKLASTERROR();
    // discard the excess bytes silently
    if (err == WSAEMSGSIZE) { return (int)bufferSize; }
    return lenOrErrc;
}

} // namespace ks3::detail
