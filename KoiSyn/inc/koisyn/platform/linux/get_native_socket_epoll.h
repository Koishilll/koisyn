#pragma once

#define QUIC_EVENTS_STDOUT 1
#define QUIC_LOGS_STDOUT 1
#include "../core/precomp.h"
#undef QUIC_EVENTS_STDOUT
#undef QUIC_LOGS_STDOUT

namespace ks3::detail
{

typedef unsigned char BOOLEAN;

typedef struct DATAPATH_SQE {
    uint32_t CqeType;
#ifdef CXPLAT_SQE
    CXPLAT_SQE Sqe;
#endif
} DATAPATH_SQE;

//
// A per processor datapath context.
//
typedef void CXPLAT_DATAPATH_PROC;

//
// Socket context.
//
typedef struct QUIC_CACHEALIGN CXPLAT_SOCKET_CONTEXT {

    //
    // The datapath binding this socket context belongs to.
    //
    CXPLAT_SOCKET* Binding;

    //
    // The datapath proc context this socket belongs to.
    //
    CXPLAT_DATAPATH_PROC* DatapathProc;

    //
    // The socket FD used by this socket context.
    //
    int SocketFd;

    //
    // The submission queue event for shutdown.
    //
    DATAPATH_SQE ShutdownSqe;

    //
    // The submission queue event for IO.
    //
    DATAPATH_SQE IoSqe;

    //
    // The submission queue event for flushing the send queue.
    //
    DATAPATH_SQE FlushTxSqe;

    //
    // The head of list containg all pending sends on this socket.
    //
    CXPLAT_LIST_ENTRY TxQueue;

    //
    // Lock around the PendingSendData list.
    //
    CXPLAT_LOCK TxQueueLock;

    //
    // Rundown for synchronizing clean up with upcalls.
    //
    CXPLAT_RUNDOWN_REF UpcallRundown;

    //
    // Inidicates the SQEs have been initialized.
    //
    BOOLEAN SqeInitialized : 1;

    //
    // Inidicates if the socket has started IO processing.
    //
    BOOLEAN IoStarted : 1;

#if DEBUG
    uint8_t Uninitialized : 1;
    uint8_t Freed : 1;
#endif

} CXPLAT_SOCKET_CONTEXT;

//
// Datapath binding.
//
typedef struct CXPLAT_SOCKET {

    //
    // A pointer to datapath object.
    //
    CXPLAT_DATAPATH* Datapath;

    //
    // The client context for this binding.
    //
    void *ClientContext;

    //
    // The local address for the binding.
    //
    QUIC_ADDR LocalAddress;

    //
    //  The remote address for the binding.
    //
    QUIC_ADDR RemoteAddress;

    //
    // Synchronization mechanism for cleanup.
    //
    CXPLAT_REF_COUNT RefCount;

    //
    // The MTU for this binding.
    //
    uint16_t Mtu;

    //
    // Indicates the binding connected to a remote IP address.
    //
    BOOLEAN Connected : 1;

    //
    // Flag indicates the socket has a default remote destination.
    //
    BOOLEAN HasFixedRemoteAddress : 1;

    //
    // Flag indicates the binding is being used for PCP.
    //
    BOOLEAN PcpBinding : 1;

#if DEBUG
    uint8_t Uninitialized : 1;
    uint8_t Freed : 1;
#endif

    //
    // Set of socket contexts one per proc.
    //
    CXPLAT_SOCKET_CONTEXT SocketContexts[];

} CXPLAT_SOCKET;



#define TO_ADDRESS(type, pBase, offset) ((type *)(((uint8_t *)pBase) + offset))

inline SOCKET InternalGetSocketFromConnection(HQUIC hconn) noexcept
{
    // is not a connection
    if (hconn->Type != QUIC_HANDLE_TYPE_CONNECTION_CLIENT &&
        hconn->Type != QUIC_HANDLE_TYPE_CONNECTION_SERVER) 
    {
        return -1;
    }
    QUIC_CONNECTION *conn = (QUIC_CONNECTION *)hconn;

    static const size_t offsetOfPaths    = offsetof(QUIC_CONNECTION, Paths);
    static const size_t offsetOfBinding  = offsetof(QUIC_PATH, Binding);
    static const size_t offsetOfCxPlatSk = offsetof(QUIC_BINDING, Socket);
    static const size_t offsetOfCxSkCtx  = offsetof(CXPLAT_SOCKET, SocketContexts);
    static const size_t offsetOfSocketFd = offsetof(CXPLAT_SOCKET_CONTEXT, SocketFd);

    // printf("offsetOfPaths   =%lld\n", offsetOfPaths); // ??
    // printf("offsetOfBinding =%lld\n", offsetOfBinding); // ??
    // printf("offsetOfCxPlatSk=%lld\n", offsetOfCxPlatSk); // ??
    // printf("offsetOfCxSkCtx =%lld\n", offsetOfCxSkCtx); // ??
    // printf("offsetOfSocketFd=%lld\n", offsetOfSocketFd); // ??

    QUIC_PATH *pPaths                    = TO_ADDRESS(QUIC_PATH, conn, offsetOfPaths);
    QUIC_BINDING **ppBinding             = TO_ADDRESS(QUIC_BINDING *, pPaths, offsetOfBinding);
    CXPLAT_SOCKET **ppCxPlatSock         = TO_ADDRESS(CXPLAT_SOCKET *, *ppBinding, offsetOfCxPlatSk);
    CXPLAT_SOCKET_CONTEXT *pCxSkCtx      = TO_ADDRESS(CXPLAT_SOCKET_CONTEXT, *ppCxPlatSock, offsetOfCxSkCtx);
    int sock                             = *TO_ADDRESS(int, pCxSkCtx, offsetOfSocketFd);

    // printf("pPaths          =%p\n", pPaths);
    // printf("ppBinding       =%p\n", ppBinding);
    // printf("ppCxPlatSock    =%p\n", ppCxPlatSock);
    // printf("pCxSkCtx        =%p\n", pCxSkCtx);
    // printf("sock            =%p\n", (void *)sock);
    return sock;
}

inline SOCKET InternalGetSocketFromListener(HQUIC hlisn) noexcept
{
    // is not a listener
    if (hlisn->Type != QUIC_HANDLE_TYPE_LISTENER)
    {
        return -1;
    }
    QUIC_CONNECTION *lisn = (QUIC_CONNECTION *)hlisn;

    static const size_t offsetOfBinding2 = offsetof(QUIC_LISTENER, Binding);
    static const size_t offsetOfCxPlatSk = offsetof(QUIC_BINDING, Socket);
    static const size_t offsetOfCxSkCtx  = offsetof(CXPLAT_SOCKET, SocketContexts);
    static const size_t offsetOfSocketFd = offsetof(CXPLAT_SOCKET_CONTEXT, SocketFd);

    // printf("offsetOfBinding2=%lld\n", offsetOfBinding2); // ??
    // printf("offsetOfCxPlatSk=%lld\n", offsetOfCxPlatSk); // ??
    // printf("offsetOfCxSkCtx =%lld\n", offsetOfCxSkCtx); // ??
    // printf("offsetOfSocketFd=%lld\n", offsetOfSocketFd); // ??

    QUIC_BINDING **ppBinding2            = TO_ADDRESS(QUIC_BINDING *, lisn, offsetOfBinding2);
    CXPLAT_SOCKET **ppCxPlatSock         = TO_ADDRESS(CXPLAT_SOCKET *, *ppBinding2, offsetOfCxPlatSk);
    CXPLAT_SOCKET_CONTEXT *pCxSkCtx      = TO_ADDRESS(CXPLAT_SOCKET_CONTEXT, *ppCxPlatSock, offsetOfCxSkCtx);
    int sock                             = *TO_ADDRESS(int, pCxSkCtx, offsetOfSocketFd);

    // printf("ppBinding2      =%p\n", ppBinding2);
    // printf("ppCxPlatSock    =%p\n", ppCxPlatSock);
    // printf("pCxSkCtx        =%p\n", pCxSkCtx);
    // printf("sock            =%p\n", (void *)sock);
    return sock;
}

#undef TO_ADDRESS

} // namespace ks3::detail
