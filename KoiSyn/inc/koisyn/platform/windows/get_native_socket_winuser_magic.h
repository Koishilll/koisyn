// We get the native socket from the internal representation of connections
// and listener. Instead of include the entire implementation that will pollute
// our code, we use the offset numbers directly. If you want to figure out these
// internal structure, please see it at:
//
// https://github.com/microsoft/msquic
//
// Because of the dirty way it works, we don't guarantee that it will work for
// any MsQuic version. These offset is for MsQuic v2.2.2 .

#pragma once

typedef enum QUIC_HANDLE_TYPE {

    QUIC_HANDLE_TYPE_REGISTRATION,
    QUIC_HANDLE_TYPE_CONFIGURATION,
    QUIC_HANDLE_TYPE_LISTENER,
    QUIC_HANDLE_TYPE_CONNECTION_CLIENT,
    QUIC_HANDLE_TYPE_CONNECTION_SERVER,
    QUIC_HANDLE_TYPE_STREAM

} QUIC_HANDLE_TYPE;

//
// The base type for all QUIC handles.
//
typedef struct QUIC_HANDLE {

    //
    // The current type of handle (client/server/child).
    //
    QUIC_HANDLE_TYPE Type;

    //
    // The API client context pointer.
    //
    void* ClientContext;

} QUIC_HANDLE;

typedef QUIC_HANDLE *HQUIC;

namespace ks3::detail
{

//#ifndef TO_ADDRESS
//#define TO_ADDRESS(type, pBase, offset) ((type *)(((uint8_t *)pBase) + offset))
//#endif // !TO_ADDRESS

inline SOCKET InternalGetSocketFromConnection(HQUIC hconn) noexcept
{
    // is NULL
    if (hconn == nullptr)
    {
        return BADSOCKET;
    }

    // is not a connection
    if (hconn->Type != QUIC_HANDLE_TYPE_CONNECTION_CLIENT &&
        hconn->Type != QUIC_HANDLE_TYPE_CONNECTION_SERVER)
    {
        return BADSOCKET;
    }

    //QUIC_CONNECTION *conn = (QUIC_CONNECTION *)hconn;
    uint8_t *conn = (uint8_t *)hconn;

    //static const size_t offsetOfPaths    = offsetof(QUIC_CONNECTION, Paths); // 296
    //static const size_t offsetOfBinding  = offsetof(QUIC_PATH, Binding); // 40
    //static const size_t offsetOfCxPlatSk = offsetof(QUIC_BINDING, Socket); // 32
    //static const size_t offsetOfCxSkProc = offsetof(CXPLAT_SOCKET, Processors); // 128
    //static const size_t offsetOfSocket   = offsetof(CXPLAT_SOCKET_PROC, Socket); // 136

    constexpr size_t offsetOfPaths    = 296;
    constexpr size_t offsetOfBinding  = 40;
    constexpr size_t offsetOfCxPlatSk = 32;
    constexpr size_t offsetOfCxSkProc = 128;
    constexpr size_t offsetOfSocket   = 136;

    // printf("offsetOfPaths   =%lld\n", offsetOfPaths); // 296
    // printf("offsetOfBinding =%lld\n", offsetOfBinding); // 40
    // printf("offsetOfCxPlatSk=%lld\n", offsetOfCxPlatSk); // 32
    // printf("offsetOfCxSkProc=%lld\n", offsetOfCxSkProc); // 128
    // printf("offsetOfSocket  =%lld\n", offsetOfSocket); // 136

    uint8_t *pPaths        = conn + offsetOfPaths;
    if ((uintptr_t)pPaths       < 0xFFFF) { return BADSOCKET; }

    uint8_t **ppBinding    = (uint8_t **)(pPaths + offsetOfBinding);
    if ((uintptr_t)ppBinding    < 0xFFFF) { return BADSOCKET; }

    uint8_t **ppCxPlatSock = (uint8_t **)(*ppBinding + offsetOfCxPlatSk);
    if ((uintptr_t)ppCxPlatSock < 0xFFFF) { return BADSOCKET; }

    uint8_t *pCxSkProc     = *ppCxPlatSock + offsetOfCxSkProc;
    if ((uintptr_t)pCxSkProc    < 0xFFFF) { return BADSOCKET; }

    SOCKET *pSock          = (SOCKET *)(pCxSkProc + offsetOfSocket);
    if ((uintptr_t)pSock        < 0xFFFF) { return BADSOCKET; }

    SOCKET sock                          = *pSock;

    // printf("pPaths      =%p\n", pPaths);
    // printf("ppBinding   =%p\n", ppBinding);
    // printf("ppCxPlatSock=%p\n", ppCxPlatSock);
    // printf("pCxSkProc   =%p\n", pCxSkProc);
    // printf("sock        =%p\n", (void *)sock);

    return sock;
}

inline SOCKET InternalGetSocketFromListener(HQUIC hlisn) noexcept
{
    // is NULL
    if (hlisn == NULL)
    {
        return BADSOCKET;
    }

    // is not a listener
    if (hlisn->Type != QUIC_HANDLE_TYPE_LISTENER)
    {
        return BADSOCKET;
    }

    //QUIC_LISTENER *lisn = (QUIC_LISTENER *)hlisn;
    uint8_t *lisn = (uint8_t *)hlisn;

    //static const size_t offsetOfBinding2 = offsetof(QUIC_LISTENER, Binding);
    //static const size_t offsetOfCxPlatSk = offsetof(QUIC_BINDING, Socket);
    //static const size_t offsetOfCxSkProc = offsetof(CXPLAT_SOCKET, Processors);
    //static const size_t offsetOfSocket   = offsetof(CXPLAT_SOCKET_PROC, Socket);

    constexpr size_t offsetOfBinding2 = 96;
    constexpr size_t offsetOfCxPlatSk = 32;
    constexpr size_t offsetOfCxSkProc = 128;
    constexpr size_t offsetOfSocket   = 136;

    // printf("offsetOfBinding2=%lld\n", offsetOfBinding2); // 96
    // printf("offsetOfCxPlatSk=%lld\n", offsetOfCxPlatSk); // 32
    // printf("offsetOfCxSkProc=%lld\n", offsetOfCxSkProc); // 128
    // printf("offsetOfSocket  =%lld\n", offsetOfSocket); // 136

    uint8_t **ppBinding2   = (uint8_t **)(lisn + offsetOfBinding2);
    if ((uintptr_t)ppBinding2   < 0xFFFF) { return BADSOCKET; }

    uint8_t **ppCxPlatSock = (uint8_t **)(*ppBinding2 + offsetOfCxPlatSk);
    if ((uintptr_t)ppCxPlatSock < 0xFFFF) { return BADSOCKET; }

    uint8_t *pCxSkProc     = (*ppCxPlatSock + offsetOfCxSkProc);
    if ((uintptr_t)pCxSkProc    < 0xFFFF) { return BADSOCKET; }

    SOCKET *pSock          = (SOCKET *)(pCxSkProc + offsetOfSocket);
    if ((uintptr_t)pSock        < 0xFFFF) { return BADSOCKET; }

    SOCKET sock            = *pSock;

    // printf("ppBinding2  =%p\n", ppBinding2);
    // printf("ppCxPlatSock=%p\n", ppCxPlatSock);
    // printf("pCxSkProc   =%p\n", pCxSkProc);
    // printf("sock        =%p\n", (void *)sock);

    return sock;
}

//#undef TO_ADDRESS

} // namespace ks3::detail
