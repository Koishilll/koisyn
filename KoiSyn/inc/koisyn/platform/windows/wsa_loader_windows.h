#pragma once

namespace ks3::detail
{

inline WsaLoader::WsaLoader() noexcept
{
    WinSockInitialized = false;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        // TODO: Log it into file rather than handle it because these error
        // is not recoverable.
        // KoiSyn cannot work without successful initializing.
        return;
    }
    WinSockInitialized = true;
}

inline WsaLoader::~WsaLoader() noexcept
{
    WinSockInitialized = false;
    WSACleanup();
}

} // namespace ks3::detail
