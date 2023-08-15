#pragma once

#include "std/std_precomp.h"
#include "msquic.h"
#include "udpsocket.h"
#include "shared_handle.h"

namespace ks3::detail
{

using namespace std;
using namespace std::chrono;

// UDP-like channel that is unreliable. The data sent from this channel
// will not re-transmit when drop packet, and the acceptable maximum number
// of bytes sending in one packet is restricted by the middle boxes.
struct DatagramChannel
{
    mutex RecvMutex;
    SharedConnection Self; // Connection started by us
    SharedConnection Peer; // Connection received passively
    uint32_t NextRecvPacket = 0;
    uint32_t NextSendPacket = 0;
    uint16_t MaxSendLength = 0;

    void Reset() noexcept
    {
        Self = {};
        Peer = {};
        lock_guard _{RecvMutex};
        NextRecvPacket = 0;
        NextSendPacket = 0;
        MaxSendLength = 0;
    }
};

// TCP-like channel that is reliable.
struct StreamChannel
{
    mutex RecvMutex;
    vector<uint8_t> Buffer;
    SharedStream Self; // Stream started by us
    SharedStream Peer; // Stream received passively
    uint64_t NextRecvByte = 0;

    StreamChannel() noexcept
    {
        Buffer.reserve(1500);
    }

    void Reset() noexcept
    {
        Self = {};
        Peer = {};
        lock_guard _{RecvMutex};
        Buffer.clear();
        NextRecvByte = 0;
    }
};

// Before starting a QUIC connection, we want to send a UDP packet to inform the
// remote firewall to let us communicate on a specific port. We get a port from
// the OS in advance as the local client port, then inform the remote which
// port they should communicate with (i.e. local client), then receive a
// meaningless packet on this port from remote listener (using a hacky trick),
// then close it to open the local client.
struct ConnectionContext
{
    mutex ModifyMutex;
    weak_ptr<void> ChannelContext;
    class KoiSession *pSession;
    QUIC_ADDR RemoteSentinel;

    // If the time of handshake is default value, we consider it is not
    // hanshakeing. (connected or idle)
    steady_clock::time_point HandshakeBegin;

    // only available before connection establishing
    UdpSocket Transient;

    struct
    {
        uint16_t RemoteServer;
        uint16_t RemoteClient;
        uint16_t LocalServer;
        uint16_t LocalClient;
    } Ports;

    DatagramChannel Unreliable;
    array<StreamChannel, 4> Reliable;

    atomic_uint32_t RefCount;

    ConnectionContext() noexcept :
        pSession{},
        RemoteSentinel{},
        Ports{} {}

    // Close both client (send) side and server (receive) side.
    // Don't clean pSession.
    void Reset() noexcept
    {
        Reliable[0].Reset();
        Reliable[1].Reset();
        Reliable[2].Reset();
        Reliable[3].Reset();
        Unreliable.Reset();
        ChannelContext.reset();
        RemoteSentinel = {};
        HandshakeBegin = {};
        Transient = {};
        Ports = {};
    }
};

#pragma warning(push)
#pragma warning(disable: 4200) // warning C4200: nonstandard extension used: zero-sized array in struct/union

struct RawBuffer
{
    QUIC_BUFFER Buffer;
    atomic_uint32_t RefCount;
    uint32_t PacketLengthOrNumber;
    uint8_t Data[];
};

#pragma warning(pop)

class KoiChan
{
public:
    void *handle;

public:
    operator void *() noexcept
    {
        return handle;
    }

    void Disconnect() noexcept
    {
        ConnectionContext &ctx = *(ConnectionContext *)handle;
        lock_guard _{ctx.ModifyMutex};
        ctx.Reset();
    }

    bool ReliablePacketSend0(span<const uint8_t> data) noexcept
    {
        return ReliablePacketSend(0, data);
    }

    bool ReliablePacketSend1(span<const uint8_t> data) noexcept
    {
        return ReliablePacketSend(1, data);
    }

    bool ReliablePacketSend2(span<const uint8_t> data) noexcept
    {
        return ReliablePacketSend(2, data);
    }

    bool ReliablePacketSend3(span<const uint8_t> data) noexcept
    {
        return ReliablePacketSend(3, data);
    }

    bool ReliablePacketSend(
        uint32_t channel,
        span<const uint8_t> data
        ) noexcept
    {
        ConnectionContext *pctx = (ConnectionContext *)handle;
        if (pctx == nullptr) { return false; }

        // Now we only accept packet size <= 65512 for now.
        if (data.size() > 65512) { return false; }
        if (channel >= 4) [[unlikely]] { return false; }
        StreamChannel &chn = pctx->Reliable[channel];

        optional maybeRawBuffer = MakeBuffer(data);
        if (not maybeRawBuffer) { return false; }
        RawBuffer *rawBuffer = *maybeRawBuffer;
        rawBuffer->PacketLengthOrNumber = htonl((uint32_t)data.size());

        SharedStream peer = chn.Peer;
        SharedStream self = chn.Self;

        bool sent = false;

        // The reason we split it into two loop is, if the first sending is
        // done before the second sending start, the allocated memory will be
        // delete in advance and the second sending will cause memory access
        // violation.
        // We delete it in the handler of
        // QUIC_STREAM_EVENT_SEND_COMPLETE at koisession.h .
        for (HQUIC strm : { peer.get(), self.get() })
        {
            if (strm == nullptr) { continue; }
            ++rawBuffer->RefCount;
        }

        for (HQUIC strm : { peer.get(), self.get() })
        {
            if (strm == nullptr) { continue; }
            QUIC_STATUS status = MsQuic->StreamSend(
                strm,
                &rawBuffer->Buffer,
                1,
                QUIC_SEND_FLAG_ALLOW_0_RTT,
                rawBuffer);
            sent |= QUIC_SUCCEEDED(status);
        }

        if (not sent) { delete[] rawBuffer; }

        return sent;
    }

    bool UnreliablePacketSend(span<const uint8_t> data) noexcept
    {
        ConnectionContext *pctx = (ConnectionContext *)handle;

        // The size can be sent in a packet may vary, depending on IPv4 / IPv6
        // and other factor.
        DatagramChannel &chn = pctx->Unreliable;
        if (data.size() > chn.MaxSendLength) { return false; }

        uint32_t number = chn.NextSendPacket++;

        optional maybeRawBuffer = MakeBuffer(data);
        if (not maybeRawBuffer) { return false; }
        RawBuffer *rawBuffer = *maybeRawBuffer;
        rawBuffer->PacketLengthOrNumber = htonl(number);

        SharedConnection peer = chn.Peer;
        SharedConnection self = chn.Self;

        bool sent = false;

        // The reason we split it into two loop is, if the first sending is
        // done before the second sending start, the allocated memory will be
        // delete in advance and the second sending will cause memory access
        // violation.
        // We delete it in the handler of
        // QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED at koisession.h .
        for (HQUIC conn : { peer.get(), self.get() })
        {
            if (conn == nullptr) { continue; }
            ++rawBuffer->RefCount;
        }

        for (HQUIC conn : { peer.get(), self.get() })
        {
            if (conn == nullptr) { continue; }
            QUIC_STATUS status = MsQuic->DatagramSend(
                conn,
                &rawBuffer->Buffer,
                1,
                QUIC_SEND_FLAG_ALLOW_0_RTT,
                rawBuffer);
            sent |= QUIC_SUCCEEDED(status);
        }

        if (not sent) { delete[] rawBuffer; }

        return sent;
    }

private:
    friend class KoiSession;

    KoiChan() noexcept : handle{} {};
    KoiChan(ConnectionContext &ctx) noexcept : handle{&ctx} {}

    optional<RawBuffer *> MakeBuffer(span<const uint8_t> data) noexcept
    {
        const uint32_t datasize = (uint16_t)data.size();
        const uint32_t allocsize = sizeof(RawBuffer) + datasize;
        uint8_t *allocated = new(nothrow) uint8_t[allocsize];
        if (allocated == nullptr) { return nullopt; }

        // Not only the real data, but we also send the packet number and the
        // length of real data in addition (4 bytes).
        RawBuffer *rawBuffer = (RawBuffer *)allocated;

        // warning C6001: Using uninitialized memory '*allocated'.
        // No need to initialize because the constructor of atomic_uint32_t
        // is trivial.
#pragma warning(push)
#pragma warning(disable: 6001)
        rawBuffer->RefCount = 0;
#pragma warning(pop)

        rawBuffer->Buffer.Buffer = rawBuffer->Data - 4;
        rawBuffer->Buffer.Length = 4 + datasize;
        memcpy(rawBuffer->Data, data.data(), datasize);

        return rawBuffer;
    }
};

inline bool AutoReject(
    [[maybe_unused]] KoiChan newChannel,
    [[maybe_unused]] void *globalContext,
    [[maybe_unused]] weak_ptr<void> &setChannelContext
    ) noexcept { return false; }

inline void NoOpReceive(
    [[maybe_unused]] KoiChan channel,
    [[maybe_unused]] span<const uint8_t> data,
    [[maybe_unused]] void *globalContext,
    [[maybe_unused]] void *channelContext
    ) noexcept {}

inline void NoOpDisconnect(
    [[maybe_unused]] KoiChan channel,
    [[maybe_unused]] void *globalContext,
    [[maybe_unused]] void *channelContext
    ) noexcept {}

// Application global context.
struct Kontext
{
    using AcceptCallback = decltype(AutoReject);
    using ReceiveCallback = decltype(NoOpReceive);
    using DisconnectCallback = decltype(NoOpDisconnect);

    void               *GlobalContext = nullptr;
    AcceptCallback     *OnAccept = &AutoReject;
    ReceiveCallback    *OnReliableReceive[4] =
    {
        &NoOpReceive, &NoOpReceive, &NoOpReceive, &NoOpReceive,
    };
    ReceiveCallback    *OnUnreliableReceive = &NoOpReceive;
    DisconnectCallback *OnDisconnect = &NoOpDisconnect;
};

} // namespace ks3:: detail
