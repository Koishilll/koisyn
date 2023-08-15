#pragma once

#pragma warning(push)
#pragma warning(disable:4201) // warning C4201: nonstandard extension used: nameless struct/union
#pragma warning(disable:4324) // warning C4324: structure was padded due to alignment specifier
#pragma warning(disable:4200) // warning C4200: nonstandard extension used: zero-sized array in struct/union

#define QUIC_EVENTS_STDOUT 1
#define QUIC_LOGS_STDOUT 1
#include "../core/precomp.h"
#undef QUIC_EVENTS_STDOUT
#undef QUIC_LOGS_STDOUT

namespace ks3::detail
{

//
// Type of IO.
//
typedef enum DATAPATH_IO_TYPE {
} DATAPATH_IO_TYPE;

typedef struct DATAPATH_SQE {
    uint32_t CqeType;
#ifdef CXPLAT_SQE
    CXPLAT_SQE Sqe;
#endif
} DATAPATH_SQE;

//
// IO header for SQE->CQE based completions.
//
typedef struct DATAPATH_IO_SQE {
    DATAPATH_IO_TYPE IoType;
    DATAPATH_SQE DatapathSqe;
} DATAPATH_IO_SQE;

//
// Represents a single IO completion port and thread for processing work that is
// completed on a single processor.
//
typedef void CXPLAT_DATAPATH_PROC;

typedef void *RIO_CQ, **PRIO_CQ;
typedef void *RIO_RQ, **PRIO_RQ;

//
// Per-processor socket state.
//
typedef struct QUIC_CACHEALIGN CXPLAT_SOCKET_PROC {
    //
    // Used to synchronize clean up.
    //
    CXPLAT_REF_COUNT RefCount;

    //
    // Submission queue event for IO completion
    //
    DATAPATH_IO_SQE IoSqe;

    //
    // Submission queue event for RIO IO completion
    //
    DATAPATH_IO_SQE RioSqe;

    //
    // The datapath per-processor context.
    //
    CXPLAT_DATAPATH_PROC* DatapathProc;

    //
    // Parent CXPLAT_SOCKET.
    //
    CXPLAT_SOCKET* Parent;

    //
    // Socket handle to the networking stack.
    //
    SOCKET Socket;

    //
    // Rundown for synchronizing upcalls to the app and downcalls on the Socket.
    //
    CXPLAT_RUNDOWN_REF RundownRef;

    //
    // Flag indicates the socket started processing IO.
    //
    BOOLEAN IoStarted : 1;

    //
    // Flag indicates a persistent out-of-memory failure for the receive path.
    //
    BOOLEAN RecvFailure : 1;

    //
    // Debug Flags
    //
    uint8_t Uninitialized : 1;
    uint8_t Freed : 1;

    //
    // The set of parameters/state passed to WsaRecvMsg for the IP stack to
    // populate to indicate the result of the receive.
    //

    union {
        //
        // Normal TCP/UDP socket data
        //
        struct {
            RIO_CQ RioCq;
            RIO_RQ RioRq;
            ULONG RioRecvCount;
            ULONG RioSendCount;
            CXPLAT_LIST_ENTRY RioSendOverflow;
            BOOLEAN RioNotifyArmed;
        };
        //
        // TCP Listener socket data
        //
        struct {
            CXPLAT_SOCKET* AcceptSocket;
            char AcceptAddrSpace[
                sizeof(SOCKADDR_INET) + 16 +
                    sizeof(SOCKADDR_INET) + 16
            ];
        };
    };
} CXPLAT_SOCKET_PROC;

//
// Per-port state. Multiple sockets are created on each port.
//
typedef struct CXPLAT_SOCKET {

    //
    // Parent datapath.
    //
    CXPLAT_DATAPATH* Datapath;

    //
    // Client context pointer.
    //
    void *ClientContext;

    //
    // The local address and port.
    //
    SOCKADDR_INET LocalAddress;

    //
    // The remote address and port.
    //
    SOCKADDR_INET RemoteAddress;

    //
    // Synchronization mechanism for cleanup.
    //
    CXPLAT_REF_COUNT RefCount;

    //
    // The local interface's MTU.
    //
    uint16_t Mtu;

    //
    // The size of a receive buffer's payload.
    //
    uint32_t RecvBufLen;

    //
    // Socket type.
    //
    uint8_t Type : 2; // CXPLAT_SOCKET_TYPE

    //
    // Flag indicates the socket has a default remote destination.
    //
    uint8_t HasFixedRemoteAddress : 1;

    //
    // Flag indicates the socket indicated a disconnect event.
    //
    uint8_t DisconnectIndicated : 1;

    //
    // Flag indicates the binding is being used for PCP.
    //
    uint8_t PcpBinding : 1;

    //
    // Flag indicates the socket is using RIO instead of traditional Winsock.
    //
    uint8_t UseRio : 1;

    //
    // Debug flags.
    //
    uint8_t Uninitialized : 1;
    uint8_t Freed : 1;

    //
    // Per-processor socket contexts.
    //
    CXPLAT_SOCKET_PROC Processors[0];

} CXPLAT_SOCKET;



#ifndef TO_ADDRESS
#define TO_ADDRESS(type, pBase, offset) ((type *)(((uint8_t *)pBase) + offset))
#endif // !TO_ADDRESS

inline SOCKET InternalGetSocketFromConnection(HQUIC hconn) noexcept
{
    // is NULL
    if (hconn == NULL)
    {
        return INVALID_SOCKET;
    }

    // is not a connection
    if (hconn->Type != QUIC_HANDLE_TYPE_CONNECTION_CLIENT &&
        hconn->Type != QUIC_HANDLE_TYPE_CONNECTION_SERVER)
    {
        return INVALID_SOCKET;
    }
    QUIC_CONNECTION *conn = (QUIC_CONNECTION *)hconn;

    static const size_t offsetOfPaths    = offsetof(QUIC_CONNECTION, Paths);
    static const size_t offsetOfBinding  = offsetof(QUIC_PATH, Binding);
    static const size_t offsetOfCxPlatSk = offsetof(QUIC_BINDING, Socket);
    static const size_t offsetOfCxSkProc = offsetof(CXPLAT_SOCKET, Processors);
    static const size_t offsetOfSocket   = offsetof(CXPLAT_SOCKET_PROC, Socket);

    // printf("offsetOfPaths   =%lld\n", offsetOfPaths); // 296
    // printf("offsetOfBinding =%lld\n", offsetOfBinding); // 40
    // printf("offsetOfCxPlatSk=%lld\n", offsetOfCxPlatSk); // 32
    // printf("offsetOfCxSkProc=%lld\n", offsetOfCxSkProc); // 128
    // printf("offsetOfSocket  =%lld\n", offsetOfSocket); // 136

    QUIC_PATH *pPaths                    = TO_ADDRESS(QUIC_PATH, conn, offsetOfPaths);
    if ((uintptr_t)pPaths       < 0xFFFF) { return BADSOCKET; }

    QUIC_BINDING **ppBinding             = TO_ADDRESS(QUIC_BINDING *, pPaths, offsetOfBinding);
    if ((uintptr_t)ppBinding    < 0xFFFF) { return BADSOCKET; }

    CXPLAT_SOCKET **ppCxPlatSock         = TO_ADDRESS(CXPLAT_SOCKET *, *ppBinding, offsetOfCxPlatSk);
    if ((uintptr_t)ppCxPlatSock < 0xFFFF) { return BADSOCKET; }

    CXPLAT_SOCKET_PROC *pCxSkProc        = TO_ADDRESS(CXPLAT_SOCKET_PROC, *ppCxPlatSock, offsetOfCxSkProc);
    if ((uintptr_t)pCxSkProc    < 0xFFFF) { return BADSOCKET; }

    SOCKET *pSock                        = TO_ADDRESS(SOCKET, pCxSkProc, offsetOfSocket);
    if ((uintptr_t)pSock        < 0xFFFF) { return BADSOCKET; }

    SOCKET sock                          = *pSock;

    // printf("pPaths          =%p\n", pPaths);
    // printf("ppBinding       =%p\n", ppBinding);
    // printf("ppCxPlatSock    =%p\n", ppCxPlatSock);
    // printf("pCxSkProc       =%p\n", pCxSkProc);
    // printf("sock            =%p\n", (void *)sock);
    return sock;
}

inline SOCKET InternalGetSocketFromListener(HQUIC hlisn) noexcept
{
    // is NULL
    if (hlisn == NULL)
    {
        return INVALID_SOCKET;
    }

    // is not a listener
    if (hlisn->Type != QUIC_HANDLE_TYPE_LISTENER)
    {
        return INVALID_SOCKET;
    }
    QUIC_LISTENER *lisn = (QUIC_LISTENER *)hlisn;

    static const size_t offsetOfBinding2 = offsetof(QUIC_LISTENER, Binding);
    static const size_t offsetOfCxPlatSk = offsetof(QUIC_BINDING, Socket);
    static const size_t offsetOfCxSkProc = offsetof(CXPLAT_SOCKET, Processors);
    static const size_t offsetOfSocket   = offsetof(CXPLAT_SOCKET_PROC, Socket);

    // printf("offsetOfBinding2=%lld\n", offsetOfBinding2); // ??
    // printf("offsetOfCxPlatSk=%lld\n", offsetOfCxPlatSk); // 32
    // printf("offsetOfCxSkProc=%lld\n", offsetOfCxSkProc); // 128
    // printf("offsetOfSocket  =%lld\n", offsetOfSocket); // 136

    QUIC_BINDING **ppBinding2            = TO_ADDRESS(QUIC_BINDING *, lisn, offsetOfBinding2);
    if ((uintptr_t)ppBinding2   < 0xFFFF) { return BADSOCKET; }

    CXPLAT_SOCKET **ppCxPlatSock         = TO_ADDRESS(CXPLAT_SOCKET *, *ppBinding2, offsetOfCxPlatSk);
    if ((uintptr_t)ppCxPlatSock < 0xFFFF) { return BADSOCKET; }

    CXPLAT_SOCKET_PROC *pCxSkProc        = TO_ADDRESS(CXPLAT_SOCKET_PROC, *ppCxPlatSock, offsetOfCxSkProc);
    if ((uintptr_t)pCxSkProc    < 0xFFFF) { return BADSOCKET; }

    SOCKET *pSock                        = TO_ADDRESS(SOCKET, pCxSkProc, offsetOfSocket);
    if ((uintptr_t)pSock        < 0xFFFF) { return BADSOCKET; }

    SOCKET sock                          = *pSock;

    // printf("ppBinding2      =%p\n", ppBinding2);
    // printf("ppCxPlatSock    =%p\n", ppCxPlatSock);
    // printf("pCxSkProc       =%p\n", pCxSkProc);
    // printf("sock            =%p\n", (void *)sock);
    return sock;
}

#undef TO_ADDRESS

} // namespace ks3::detail

#pragma warning(pop)
