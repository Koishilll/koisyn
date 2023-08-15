#pragma once

#include "../std/std_precomp.h"

#if defined _WIN32 && _WIN32 != 0

#include <WinSock2.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "ntdll.lib")

#define BADSOCKET INVALID_SOCKET
#define ISVALIDSOCK(sock) ((sock) != INVALID_SOCKET)
#define CLOSESOCKET(sock) closesocket(sock)
#define GETSOCKLASTERROR() (WSAGetLastError())

#elif (defined __linux__ && __linux__ != 0) || \
    (defined __FreeBSD__ && __FreeBSD__ != 0) || \
    (defined __APPLE__ && __APPLE__ != 0) // ^^^ Windows / Unix-like vvv

#include <sys/socket.h>

#define BADSOCKET -1
#define ISVALIDSOCK(sock) ((sock) >= 0)
#define CLOSESOCKET(sock) close(sock)
#define GETSOCKLASTERROR() (errno)
typedef int SOCKET;

#else /// ^^^ Unix-like / Unsupported vvv

#error "Unsupported Platform"

#endif // ^^^ Windows / Unix-like / Unsupported ^^^

#if (defined __linux__ && __linux__ != 0) || \
    (defined __FreeBSD__ && __FreeBSD__ != 0)
#ifndef CX_PLATFORM_LINUX
#define CX_PLATFORM_LINUX 1
#endif // !CX_PLATFORM_LINUX
#elif (defined __APPLE__ && __APPLE__ != 0) // ^^^ Linux / MacOS vvv
#ifndef CX_PLATFORM_DARWIN
#define CX_PLATFORM_DARWIN 1
#endif // !CX_PLATFORM_DARWIN
#endif // ^^^ Linux / MacOS ^^^




typedef struct QUIC_HANDLE *HQUIC;

namespace ks3::detail
{
    // call WSAStartup and WSACleanup on Windows, and do nothing on others
    struct WsaLoader
    {
        bool WinSockInitialized;
        WsaLoader() noexcept;
        ~WsaLoader() noexcept;
    };

    int GetTruncatedLength(int lenOrErrc, size_t) noexcept;
    SOCKET InternalGetSocketFromConnection(HQUIC hconn) noexcept;
    SOCKET InternalGetSocketFromListener(HQUIC hlisn) noexcept;
    std::filesystem::path GetTempDirectoryPath(std::error_code &) noexcept;
}

#include "koisyn_platform-impl.h"
