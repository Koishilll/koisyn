#pragma once

#include "std/std_precomp.h"
#include "platform/msquic_loader.h"
#include "msquic.h"
#include "udpsocket.h"
#include "checksum.h"
#include "address_parser.h"
#include "koichan.h"


// TODO: log
#define DO_CASE(evType) \
    case evType: /*puts(#evType);*/ \
    return Handle_##evType(hndl, ctx, ev);

#define LISTENER_PARAMS (\
    [[maybe_unused]] HQUIC lisn, \
    [[maybe_unused]] void *ctx, \
    [[maybe_unused]] QUIC_LISTENER_EVENT *ev) /* may throw by user callback */

#define CONNECTION_PARAMS (\
    [[maybe_unused]] HQUIC conn, \
    [[maybe_unused]] void *ctx, \
    [[maybe_unused]] QUIC_CONNECTION_EVENT *ev) /* may throw by user callback */

#define STREAM_PARAMS (\
    [[maybe_unused]] HQUIC strm, \
    [[maybe_unused]] void *ctx, \
    [[maybe_unused]] QUIC_STREAM_EVENT *ev) /* may throw by user callback */

#define LISTENER_HANDLER(evType) \
    inline QUIC_STATUS Handle_##evType LISTENER_PARAMS

#define CONNECTION_HANDLER(evType) \
    inline QUIC_STATUS Handle_##evType CONNECTION_PARAMS

#define STREAM_HANDLER(evType) \
    inline QUIC_STATUS Handle_##evType STREAM_PARAMS


namespace ks3::detail
{

using namespace std;
using namespace std::chrono;

_Function_class_(QUIC_LISTENER_CALLBACK)
inline QUIC_STATUS ListenerCallback(
    HQUIC /* lisn */ hndl, void *ctx, QUIC_LISTENER_EVENT *ev) noexcept;

_Function_class_(QUIC_CONNECTION_CALLBACK)
inline QUIC_STATUS ConnectionCallback(
    HQUIC /* conn */ hndl, void *ctx, QUIC_CONNECTION_EVENT *ev) noexcept;

_Function_class_(QUIC_STREAM_CALLBACK)
inline QUIC_STATUS StreamCallback(
    HQUIC /* strm */ hndl, void *ctx, QUIC_STREAM_EVENT *ev) noexcept;

class KoiSession
{
public:

private:
    constexpr static int maxConnections = 16;
    constexpr static seconds retryTimeout = 4s;
    constexpr static seconds longStopRetryTimeout = 60s;
    constexpr static seconds shortStopRetryTimeout = 12s;

    inline static MsQuicLoader msquic;

    // Lock when creating a new connection context.
    mutex connCtxCreationMutex;

    Kontext appContext;
    array<ConnectionContext, maxConnections> connectionContexts{};
    SharedListener listener;
    NonOwningUdpSocket socketFromListener;
    UdpHandler sentinel;

    // loop to check all ongoing connections, and cancel if times out
    condition_variable stopSignal;
    mutex stopMutex;
    thread halfConnectionDaemon;

public:
    // defaulted construction

    ~KoiSession() noexcept
    {
        if (halfConnectionDaemon.joinable())
        {
            stopSignal.notify_all();
            halfConnectionDaemon.join();
        }

        // TODO: We sleep for some time, and wait for potential using have done.
        // It is a workaround.
        for (ConnectionContext &connCtx : connectionContexts)
        {
            unique_lock lk{connCtx.ModifyMutex};
            connCtx.Reset();
            while (connCtx.RefCount != 0)
            {
                lk.unlock();
                this_thread::sleep_for(200ms);
                lk.lock();
            }
        }
    }

    uint16_t GetSentinelPort() noexcept
    {
        return sentinel.GetPort();
    }

    // Start a session to connect to peers actively or accept connections
    // passively. Let OS choose a port when not specify a port.
    //
    // Return a non-zero sentinel port started on success.
    optional<uint16_t> Start(Kontext &kontext, uint16_t port = 0) noexcept
    {
        // startup failed
        if (msquic.InitError) [[unlikely]] { return nullopt; }

        // already started
        uint16_t alreadyStartedPort = GetSentinelPort();
        if (alreadyStartedPort) { return alreadyStartedPort; }

        // try to bind a specific or unspecific port
        auto maybeHandler = UdpHandler::Bind(port);
        if (not maybeHandler) { return nullopt; }
        sentinel = move(*maybeHandler);

        // open then start a QUIC listener
        auto maybeListener = SharedListener::OpenAndStart(
            msquic.Registration, ListenerCallback, this);
        if (not maybeListener)
        {
            sentinel = {};
            return nullopt;
        }
        listener = move(*maybeListener);
        socketFromListener = {InternalGetSocketFromListener(listener.get())};

        // register the consumer to the socket
        bool ok = sentinel.RegisterRecvFromCallback<16>(
            [this](span<const uint8_t> data, const QUIC_ADDR &remote) noexcept
            {
                ConsumeRawUdpData(data, remote);
            });

        if (not ok)
        {
            sentinel = {};
            return nullopt;
        }

        appContext = move(kontext);
        for (auto &connCtx : connectionContexts)
        {
            connCtx.pSession = this;
        }

        halfConnectionDaemon = thread{
            &KoiSession::CheckHalfConnection,
            this};

        return sentinel.GetPort();
    }

    // Try to establish a connection. Will do this thing:
    // 0. bind a UDP port for a sentinel to handle raw UDP packet.
    // 1. bind a UDP port for a local client.
    // 2. inform remote the local client port.
    // ... (see ConsumeRawUdpData below)
    //
    // The second port is ignored when address already contains one.
    void ConnectTo(string_view addrAndPort, uint16_t port) noexcept
    {
        if (not sentinel.IsRunning()) { return; }

        // parse input address string
        QUIC_ADDR remote;
        bool ok = AddressParser::TryParseMap6(addrAndPort, port, remote);
        // bad address string
        if (not ok) { return; }

        // lock, and find by remote address
        lock_guard creationLock{connCtxCreationMutex};
        bool matching;
        int foundIndex;
        tie(matching, foundIndex) = FindMatchingEntry(remote);

        // a connection on this address is being established. We just
        // discard this connecting attempt.
        if (matching) { return; }

        // We reach the max connections limit. Discard this request.
        if (foundIndex == -1) { return; }

        // Now we try to reserve a port for local client that the peer's server
        // (listener) will send to.
        auto maybeSock = UdpSocket::Bind();
        if (not maybeSock) { return; }

        // Save our connection context.
        uint16_t localServerPort = socketFromListener.GetPort();
        uint16_t localClientPort = maybeSock->GetPort();
        ConnectionContext &ctx = connectionContexts[foundIndex];
        ctx.Ports.LocalServer = localServerPort;
        ctx.Ports.LocalClient = localClientPort;
        ctx.HandshakeBegin = steady_clock::now();
        ctx.Transient = move(*maybeSock);
        ctx.RemoteSentinel = remote;
        //ctx.ConnectingDaemon = thread{ConnectingDaemon, ref(ctx)};

        // send our ports to the peer. (first packet)
        puts("Send 1st");
        SendPorts(remote, localServerPort, localClientPort, 0, 0);
    }

private:
    void SendPorts(
        const QUIC_ADDR &remote,
        uint16_t inLocalServerPort,
        uint16_t inLocalClientPort,
        uint16_t inRemoteServerPort,
        uint16_t inRemoteClientPort
        ) noexcept
    {
        uint16_t ports[4]{};
        ports[0] = htons(inLocalServerPort);
        ports[1] = htons(inLocalClientPort);
        ports[2] = htons(inRemoteServerPort);
        ports[3] = htons(inRemoteClientPort);
        span bytes = span{(uint8_t *)&ports, 8};
        sentinel.SendTo(remote, bytes);
    }

    static bool ConvertBufferToPorts(
        span<const uint8_t> inBuffer,
        uint16_t &outRemoteServerPort,
        uint16_t &outRemoteClientPort,
        uint16_t &outLocalServerPort,
        uint16_t &outLocalClientPort
        ) noexcept
    {
        if (inBuffer.size() < 8) [[unlikely]] { return false; }
        uint16_t ports[4];
        memcpy(&ports, inBuffer.data(), 8);
        outRemoteServerPort = ntohs(ports[0]);
        outRemoteClientPort = ntohs(ports[1]);
        outLocalServerPort  = ntohs(ports[2]);
        outLocalClientPort  = ntohs(ports[3]);
        return true;
    }

    void CheckHalfConnection() noexcept
    {
        while (true)
        {
            unique_lock lk{stopMutex};
            bool stopNow =
                stopSignal.wait_for(lk, retryTimeout) == cv_status::no_timeout;
            if (stopNow) { return; }

            for (ConnectionContext &connCtx : connectionContexts)
            {
                DoCheck(connCtx);
            }
        }
    }

    void DoCheck(ConnectionContext &connCtx) noexcept
    {
        lock_guard _{connCtx.ModifyMutex};
        
        // idle or connected
        if (connCtx.HandshakeBegin == steady_clock::time_point{}) { return; }

        auto elapsed = steady_clock::now() - connCtx.HandshakeBegin;

        // retry at next check
        if (elapsed < retryTimeout - 2ms) { return; }

        // No retry anymore; the first packet wasn't receive.
        if (elapsed > longStopRetryTimeout - 2ms)
        {
            puts("Stop retry");
            connCtx.Reset();
            return;
        }

        // Quick fail; the second or third packet wasn't receive. This means
        // network is ok and maybe the peer doesn't want to continue.
        bool knownPeerPorts =
            connCtx.Ports.RemoteServer | connCtx.Ports.RemoteClient;
        if (knownPeerPorts and elapsed > shortStopRetryTimeout - 2ms)
        {
            puts("Stop retry");
            connCtx.Reset();
            appContext.OnDisconnect(
                CreateChannel(connCtx),
                appContext.GlobalContext,
                connCtx.ChannelContext.lock().get());
            return;
        }

        // start retry
        puts("Retry");
        SendPorts(
            connCtx.RemoteSentinel,
            connCtx.Ports.LocalServer,
            connCtx.Ports.LocalClient,
            connCtx.Ports.RemoteServer,
            connCtx.Ports.RemoteClient);
    }

    // Find an matching entry in all status by remote sentinel address.
    // If there is an matching entry, return (true, index).
    // If not but there is an empty space, return (false, index).
    // If not and there is no space to add new one, return (false, -1).
    pair<bool, int> FindMatchingEntry(const QUIC_ADDR &remote) noexcept
    {
        int firstEmpty = -1;
        for (int i = 0; i < connectionContexts.size(); ++i)
        {
            auto &connCtx = connectionContexts[i];
            if (not connCtx.ModifyMutex.try_lock()) { continue; }
            unique_lock _{connCtx.ModifyMutex, adopt_lock};

            QUIC_ADDR *pRemote = &connCtx.RemoteSentinel;
            bool same = QuicAddrCompare(pRemote, &remote);
            if (same) { return {true, i}; }

            // find a free space to store the status.
            if (firstEmpty == -1)
            {
                bool empty = connCtx.RefCount == 0 and
                    bit_cast<uint64_t>(connCtx.Ports) == 0;
                if (empty)
                {
                    firstEmpty = i;
                }
            }
        }
        return {false, firstEmpty};
    }

    // continue to do this thing:
    // ... (see ConnectTo above)
    // 3. get the native socket of remote listener.
    // 4. send a packet from the remote listener to the local client.
    // 5. establish connections on the new ports.
    void ConsumeRawUdpData(span<const uint8_t> data, const QUIC_ADDR &remote)
        noexcept
    {
        // unexpected data length
        if (data.size() != 8) { return; }

        // get peer's ports
        uint16_t remoteServerPort;
        uint16_t remoteClientPort;
        uint16_t localServerPort;
        uint16_t localClientPort;
        ConvertBufferToPorts(data,
            remoteServerPort,
            remoteClientPort,
            localServerPort,
            localClientPort);

        lock_guard creationLock{connCtxCreationMutex};

        // Find whether there is a matching entry or empty space
        bool matching;
        int foundIndex;
        tie(matching, foundIndex) = FindMatchingEntry(remote);

        // We reach the max connections limit. Discard this request.
        if (foundIndex == -1) { return; }

        ConnectionContext &ctx = connectionContexts[foundIndex];

        // Receive a connecting request (first packet). We find a space,
        // reserve a client port for them and send a response.
        if ((remoteServerPort | remoteClientPort) != 0 and
            ( localServerPort |  localClientPort) == 0)
        {
            // maybe we and them are trying to connect to each other
            // simultaneously, so we should check if this matching entry
            // indicates a connected connection.

            // already connected and the begin time of handshake was reset.
            bool connected = ctx.HandshakeBegin == steady_clock::time_point{};

            if (matching and connected) { return; }

            puts("Receive 1st");
            puts("Send 2nd");
            return Receive1st(
                remoteServerPort, remoteClientPort, foundIndex, remote);
        }

        // Receive a connecting response (second packet). We search the ongoing request,
        // acknowledge it and start a connection.
        if ((remoteServerPort | remoteClientPort) != 0 and
            ( localServerPort |  localClientPort) != 0)
        {
            // no matching connection means we'd never tried to connect to.
            if (not matching) { return; }

            // already connected and the begin time of handshake was reset.
            bool connected = ctx.HandshakeBegin == steady_clock::time_point{};
            if (connected) { return; }

            puts("Receive 2nd");
            puts("Send 3rd");
            return Receive2nd(
                remoteServerPort, remoteClientPort, foundIndex, remote);
        }

        // Receive a acknowledgement (third packet). Start a connection.
        if ((remoteServerPort | remoteClientPort) == 0 and
            ( localServerPort |  localClientPort) != 0)
        {
            // no matching connection means we'd never tried to connect to.
            if (not matching) { return; }

            // already connected and the begin time of handshake was reset.
            bool connected = ctx.HandshakeBegin == steady_clock::time_point{};
            if (connected) { return; }

            puts("Receive 3rd");
            return Receive3rd(
                remoteServerPort, remoteClientPort, foundIndex, remote);
        }
    }

    void Receive1st(
        uint16_t remoteServerPort,
        uint16_t remoteClientPort,
        int foundIndex,
        const QUIC_ADDR &remote
        ) noexcept
    {
        ConnectionContext &ctx = connectionContexts[foundIndex];
        uint16_t localServerPort = ctx.Ports.LocalServer;
        uint16_t localClientPort = ctx.Ports.LocalClient;

        // Save their ports.
        ctx.Ports.RemoteServer = remoteServerPort;
        ctx.Ports.RemoteClient = remoteClientPort;

        // Challenge their firewall.
        if (remoteClientPort != 0)
        {
            QUIC_ADDR remoteClientAddr;
            memcpy(&remoteClientAddr, &remote, sizeof(remoteClientAddr));
            QuicAddrSetPort(&remoteClientAddr, remoteClientPort);
            uint8_t nonsense[2]{};
            socketFromListener.SendTo(remoteClientAddr, span{nonsense, 2});
        }

        // If we start this connection passively, we create these thing, send
        // the second packet and wait for the third packet.
        if (ctx.HandshakeBegin == steady_clock::time_point{})
        {
            // Now we try to reserve a port for local client.
            auto maybeSock = UdpSocket::Bind();
            if (not maybeSock) { return; }

            // Save our connection context.
            localServerPort = socketFromListener.GetPort();
            localClientPort = maybeSock->GetPort();
            ctx.Ports.LocalServer = localServerPort;
            ctx.Ports.LocalClient = localClientPort;
            ctx.HandshakeBegin = steady_clock::now();
            ctx.Transient = move(*maybeSock);
            ctx.RemoteSentinel = remote;
            //ctx.ConnectingDaemon = thread{ConnectingDaemon, ref(ctx)};
        }
        // We have created these thing before (ConnectTo or another Receive1st)
        // and already know their client port. We need to create nothing.

        // send our ports to the peer. (second packet)
        SendPorts(remote, localServerPort, localClientPort,
            remoteServerPort, remoteClientPort);
    }

    void Receive2nd(
        uint16_t remoteServerPort,
        uint16_t remoteClientPort,
        int foundIndex,
        const QUIC_ADDR &remote
        ) noexcept
    {
        ConnectionContext &ctx = connectionContexts[foundIndex];
        unique_lock connCtxLock{ctx.ModifyMutex};

        // Save their ports.
        ctx.Ports.RemoteServer = remoteServerPort;
        ctx.Ports.RemoteClient = remoteClientPort;
        connCtxLock.unlock();

        // Challenge their firewall.
        if (remoteClientPort != 0)
        {
            QUIC_ADDR remoteClientAddr;
            memcpy(&remoteClientAddr, &remote, sizeof(remoteClientAddr));
            QuicAddrSetPort(&remoteClientAddr, remoteClientPort);
            uint8_t nonsense[2]{};
            socketFromListener.SendTo(remoteClientAddr, span{nonsense, 2});
        }

        // send the last acknowledgement. (third packet)
        SendPorts(remote, 0, 0, remoteServerPort, remoteClientPort);

        StartClient(ctx, remote);
    }

    void Receive3rd(
        uint16_t /* remoteServerPort */,
        uint16_t /* remoteClientPort */,
        int foundIndex,
        const QUIC_ADDR &remote
        ) noexcept
    {
        StartClient(connectionContexts[foundIndex], remote);
    }

    void StartClient(
        ConnectionContext &ctx,
        const QUIC_ADDR &remote
        ) noexcept
    {
        unique_lock<mutex> connCtxLock{ctx.ModifyMutex};

        // Don't start multiple times
        if (ctx.Unreliable.Self) { return; }

        // Allocate connection.
        auto maybeConnection = SharedConnection::Open(
            msquic.Registration,
            ConnectionCallback,
            &ctx);
        if (not maybeConnection) { return; }
        ctx.Unreliable.Self = move(*maybeConnection);
        SharedConnection &conn = ctx.Unreliable.Self;

        // Allocate 4 streams.
        for (int i = 0; i < 4; ++i)
        {
            auto maybeStream = SharedStream::Open(
                conn.get(),
                QUIC_STREAM_OPEN_FLAG_NONE,
                StreamCallback,
                &ctx);
            if (not maybeStream) { continue; }

            ctx.Reliable[i].Self = move(*maybeStream);
        }
        connCtxLock.unlock();

        // Before we finally start this connection, we require the user whether
        // we should start or not.
        bool shouldCreate = appContext.OnAccept(
            CreateChannel(ctx),
            appContext.GlobalContext,
            ctx.ChannelContext);
        if (not shouldCreate or ctx.ChannelContext.expired())
        {
            ctx.Reset();
            return;
        }
        // Now we can start this connection.

        // Close the client socket and start quic connection on the same port.
        // Theoretically, it might happen that the port used by the closed
        // socket will be used immediately by other program, but we assume this
        // will not happen.
        QUIC_ADDR localClientAddr;
        memset(&localClientAddr, 0, sizeof(localClientAddr));
        QuicAddrSetFamily(&localClientAddr, QUIC_ADDRESS_FAMILY_INET6);
        QuicAddrSetPort(&localClientAddr, ctx.Ports.LocalClient);
        MsQuic->SetParam(
            conn.get(),
            QUIC_PARAM_CONN_LOCAL_ADDRESS,
            sizeof(localClientAddr),
            &localClientAddr);

        QUIC_ADDR remoteServerAddr;
        memcpy(&remoteServerAddr, &remote, sizeof(remoteServerAddr));
        QuicAddrSetPort(&remoteServerAddr, ctx.Ports.RemoteServer);
        QUIC_ADDR_STR addrstr;
        QuicAddrToString(&remoteServerAddr, &addrstr);

        ctx.Transient = {};

        conn.Start(
            msquic.ClientConfig,
            QuicAddrGetFamily(&remoteServerAddr),
            addrstr.Address,
            ctx.Ports.RemoteServer);

        for (int i = 0; i < 4; ++i)
        {
            ctx.Reliable[i].Self.Start(QUIC_STREAM_START_FLAG_NONE);
        }
    }

    static KoiChan CreateChannel(ConnectionContext &ctx) noexcept
    {
        return KoiChan{ctx};
    }

    friend inline QUIC_STATUS ListenerCallback(
        HQUIC /* lisn */ hndl, void *ctx, QUIC_LISTENER_EVENT *ev) noexcept;
    friend inline QUIC_STATUS ConnectionCallback(
        HQUIC /* conn */ hndl, void *ctx, QUIC_CONNECTION_EVENT *ev) noexcept;
    friend inline QUIC_STATUS StreamCallback(
        HQUIC /* strm */ hndl, void *ctx, QUIC_STREAM_EVENT *ev) noexcept;

    friend LISTENER_HANDLER(QUIC_LISTENER_EVENT_NEW_CONNECTION);
    friend LISTENER_HANDLER(QUIC_LISTENER_EVENT_STOP_COMPLETE);

    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_CONNECTED);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_LOCAL_ADDRESS_CHANGED);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_PEER_ADDRESS_CHANGED);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_PEER_NEEDS_STREAMS);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_RESUMED);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED);
    friend CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED);

    friend STREAM_HANDLER(QUIC_STREAM_EVENT_START_COMPLETE);
    friend STREAM_HANDLER(QUIC_STREAM_EVENT_RECEIVE);
    friend STREAM_HANDLER(QUIC_STREAM_EVENT_SEND_COMPLETE);
    friend STREAM_HANDLER(QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN);
    friend STREAM_HANDLER(QUIC_STREAM_EVENT_PEER_SEND_ABORTED);
    friend STREAM_HANDLER(QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED);
    friend STREAM_HANDLER(QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE);
    friend STREAM_HANDLER(QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE);
    friend STREAM_HANDLER(QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE);
    friend STREAM_HANDLER(QUIC_STREAM_EVENT_PEER_ACCEPTED);
};



LISTENER_HANDLER(QUIC_LISTENER_EVENT_NEW_CONNECTION)
{
    // We find the correspond context and save the handle into it.
    const QUIC_ADDR *pRemote = ev->NEW_CONNECTION.Info->RemoteAddress;
    HQUIC conn = ev->NEW_CONNECTION.Connection;
    uint16_t remotePort = QuicAddrGetPort(pRemote);

    KoiSession &sess = *(KoiSession *)ctx;
    for (auto &connCtx : sess.connectionContexts)
    {
        lock_guard _{connCtx.ModifyMutex};

        // We compare the port first; it is faster than comparing address.
        if (remotePort != connCtx.Ports.RemoteClient) { continue; }

        bool same = QuicAddrCompareIp(&connCtx.RemoteSentinel, pRemote);
        if (not same) { continue; }

        connCtx.Unreliable.Peer = SharedConnection{conn};

        MsQuic->SetCallbackHandler(conn, (void *)ConnectionCallback, &connCtx);

        return MsQuic->ConnectionSetConfiguration(
            conn, sess.msquic.ServerConfig);
    }

    // On failure (someone bypass the UDP handshake step), we reject this
    // connection. The new connection is closed by MsQuic, so we can't close
    // it again.
    // TODO: log
    return QUIC_STATUS_CONNECTION_REFUSED;
}

LISTENER_HANDLER(QUIC_LISTENER_EVENT_STOP_COMPLETE)
{
    MsQuic->ListenerClose(lisn);
    return QUIC_STATUS_SUCCESS;
}

_Function_class_(QUIC_LISTENER_CALLBACK)
inline QUIC_STATUS ListenerCallback(
    HQUIC /* lisn */ hndl, void *ctx, QUIC_LISTENER_EVENT *ev) noexcept
try
{
    //printf("[lisn%p]", hndl);
    switch (ev->Type)
    {
    DO_CASE(QUIC_LISTENER_EVENT_NEW_CONNECTION);
    DO_CASE(QUIC_LISTENER_EVENT_STOP_COMPLETE);
    default:
        puts("Unknown Event Type");
        return QUIC_STATUS_INVALID_STATE;
    }
}
catch (const exception &)
{
    // TODO: log
    return QUIC_STATUS_SUCCESS;
}
catch (...)
{
    // TODO: log
    return QUIC_STATUS_SUCCESS;
}



CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_CONNECTED)
{
    ConnectionContext &connCtx = *(ConnectionContext *)ctx;
    lock_guard _{connCtx.ModifyMutex};
    connCtx.HandshakeBegin = {};
    ++connCtx.RefCount;

    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT)
{
    // Do nothing. We don't care this event.
    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER)
{
    // Do nothing. We don't care this event.
    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE)
{
    ConnectionContext &connCtx = *(ConnectionContext *)ctx;
    unique_lock lk{connCtx.ModifyMutex};

    // We only clean the handles of the side (client/server) this connection
    // represents.
    SharedConnection *chn;
    if (conn->Type == QUIC_HANDLE_TYPE_CONNECTION_CLIENT)
    {
        connCtx.Reliable[0].Self = {};
        connCtx.Reliable[1].Self = {};
        connCtx.Reliable[2].Self = {};
        connCtx.Reliable[3].Self = {};
        connCtx.Unreliable.Self = {};
        chn = &connCtx.Unreliable.Peer;
    }
    else
    {
        connCtx.Reliable[0].Peer = {};
        connCtx.Reliable[1].Peer = {};
        connCtx.Reliable[2].Peer = {};
        connCtx.Reliable[3].Peer = {};
        connCtx.Unreliable.Peer = {};
        chn = &connCtx.Unreliable.Self;
    }

    // If both side are closed, we clean the context.
    if (--connCtx.RefCount == 0)
    {
        KoiSession &sess = *connCtx.pSession;
        sess.appContext.OnDisconnect(
            sess.CreateChannel(connCtx),
            sess.appContext.GlobalContext,
            connCtx.ChannelContext.lock().get());
        connCtx.Reset();
    }

    lk.unlock();
    MsQuic->ConnectionClose(conn);

    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_LOCAL_ADDRESS_CHANGED)
{
    // Do nothing. The connection that we are server is down, but another that
    // we are client is not. The peer will try to reconnect to us.
    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_PEER_ADDRESS_CHANGED)
{
    // TODO: try to reconnect to the peer.
    // But it is the best if QUIC supports server side connection migration.
    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED)
{
    ConnectionContext &connCtx = *(ConnectionContext *)ctx;
    lock_guard _{connCtx.ModifyMutex};
    HQUIC strm = ev->PEER_STREAM_STARTED.Stream;

    // We ignore the lease 2 bits so that we can get the index.
    // The least significant bit (0x1): client 0 / server 1
    // The second least significant bit (0x2): bidi 0 / unidi 1
    QUIC_UINT62 streamIndex;
    uint32_t len = sizeof(streamIndex);
    MsQuic->GetParam(strm, QUIC_PARAM_STREAM_ID, &len, &streamIndex);

    connCtx.Reliable[streamIndex >> 2].Peer = SharedStream{strm};
    MsQuic->SetCallbackHandler(strm, (void *)StreamCallback, &connCtx);

    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE)
{
    // Do nothing. We will not receive this event.
    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_PEER_NEEDS_STREAMS)
{
    // Do nothing. We will not receive this event.
    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED)
{
    // Do nothing. We don't care this event.
    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED)
{
    ConnectionContext &connCtx = *(ConnectionContext *)ctx;
    lock_guard _{connCtx.ModifyMutex};
    connCtx.Unreliable.MaxSendLength =
        ev->DATAGRAM_STATE_CHANGED.MaxSendLength;
    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED)
{
    ConnectionContext &connCtx = *(ConnectionContext *)ctx;
    KoiSession &sess = *connCtx.pSession;

    // If user delete the app context, we drop connection immediately.
    shared_ptr<void> channelCtx = connCtx.ChannelContext.lock();
    if (not channelCtx)
    {
        connCtx.Reset();
        return QUIC_STATUS_ABORTED;
    }

    const QUIC_BUFFER *buf = ev->DATAGRAM_RECEIVED.Buffer;
    span<const uint8_t> data{buf->Buffer + 4, buf->Length - 4};

    uint32_t packetNumber;
    memcpy(&packetNumber, buf->Buffer, sizeof(packetNumber));
    packetNumber = ntohl(packetNumber);

    lock_guard recvLock{connCtx.Unreliable.RecvMutex};
    if (connCtx.Unreliable.NextRecvPacket == packetNumber)
    {
        ++connCtx.Unreliable.NextRecvPacket;
        sess.appContext.OnUnreliableReceive(
            sess.CreateChannel(connCtx),
            data,
            sess.appContext.GlobalContext,
            channelCtx.get());
    }

    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED)
{
    // Fire and forget
    if (ev->DATAGRAM_SEND_STATE_CHANGED.State == QUIC_DATAGRAM_SEND_SENT)
    {
        RawBuffer *buf =
            (RawBuffer *)ev->DATAGRAM_SEND_STATE_CHANGED.ClientContext;
        if (--buf->RefCount == 0) { delete[] buf; }
    }
    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_RESUMED)
{
    // TODO: how to resume?
    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED)
{
    // TODO: how client to save this ticket?
    return QUIC_STATUS_SUCCESS;
}

CONNECTION_HANDLER(QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED)
{
    // TODO: when we receive this event?
    return QUIC_STATUS_SUCCESS;
}

_Function_class_(QUIC_CONNECTION_CALLBACK)
inline QUIC_STATUS ConnectionCallback(
    HQUIC /* conn */ hndl, void *ctx, QUIC_CONNECTION_EVENT *ev) noexcept
try
{
    //printf("[conn%p]", hndl);
    switch (ev->Type)
    {
    DO_CASE(QUIC_CONNECTION_EVENT_CONNECTED);
    DO_CASE(QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT);
    DO_CASE(QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER);
    DO_CASE(QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE);
    DO_CASE(QUIC_CONNECTION_EVENT_LOCAL_ADDRESS_CHANGED);
    DO_CASE(QUIC_CONNECTION_EVENT_PEER_ADDRESS_CHANGED);
    DO_CASE(QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED);
    DO_CASE(QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE);
    DO_CASE(QUIC_CONNECTION_EVENT_PEER_NEEDS_STREAMS);
    DO_CASE(QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED);
    DO_CASE(QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED);
    DO_CASE(QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED);
    DO_CASE(QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED);
    DO_CASE(QUIC_CONNECTION_EVENT_RESUMED);
    DO_CASE(QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED);
    DO_CASE(QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED);
    default:
        puts("Unknown Event Type");
        return QUIC_STATUS_INVALID_STATE;
    }
}
catch (const exception &)
{
    // TODO: log
    return QUIC_STATUS_SUCCESS;
}
catch (...)
{
    // TODO: log
    return QUIC_STATUS_SUCCESS;
}



STREAM_HANDLER(QUIC_STREAM_EVENT_START_COMPLETE)
{
    // Need to do nothing.
    return QUIC_STATUS_SUCCESS;
}

STREAM_HANDLER(QUIC_STREAM_EVENT_RECEIVE)
{
    ConnectionContext &connCtx = *(ConnectionContext *)ctx;
    KoiSession &sess = *connCtx.pSession;

    // If user delete the app context, we drop connection immediately.
    shared_ptr<void> channelCtx = connCtx.ChannelContext.lock();
    if (not channelCtx)
    {
        connCtx.Reset();
        return QUIC_STATUS_ABORTED;
    }

    // TODO: after reconnecting, the absolute offset is reset to 0. We haven't
    // prepared to deal with this situation. How to repair?
    const uint64_t absoff = ev->RECEIVE.AbsoluteOffset;
    const uint64_t length = ev->RECEIVE.TotalBufferLength;
    const QUIC_BUFFER *bufs = ev->RECEIVE.Buffers;
    const uint32_t bufCount = ev->RECEIVE.BufferCount;
    uint64_t nowoff = absoff;

    // The least significant bit (0x1): client 0 / server 1
    // The second least significant bit (0x2): bidi 0 / unidi 1
    QUIC_UINT62 id;
    uint32_t size = sizeof(id);
    MsQuic->GetParam(strm, QUIC_PARAM_STREAM_ID, &size, &id);
    int index = (int)(id >> 2);

    StreamChannel &chn = connCtx.Reliable[index];
    vector<uint8_t> &vec = chn.Buffer;

    lock_guard recvLock{chn.RecvMutex};
    uint64_t desiredByte = chn.NextRecvByte;

    // We received newer data from another stream.
    if (absoff + length <= desiredByte) { return QUIC_STATUS_CONTINUE; }

    // Locate and read the desired byte.
    for (uint32_t i = 0; i < bufCount; ++i)
    {
        const uint64_t bufBeginOffset = nowoff;
        const uint64_t bufEndOffset = nowoff + bufs[i].Length;
        nowoff = bufEndOffset;

        // desiredByte never less than bufBeginOffset. If it occurs, it means
        // the data have been corrupt.
        if (desiredByte < bufBeginOffset)
        {
            connCtx.Reset();
            return QUIC_STATUS_ABORTED;
        }

        // bufBeginOffset < bufEndOffset <= desiredByte: skip
        if (bufEndOffset <= desiredByte) { continue; }

        // bufBeginOffset <= desiredByte < bufEndOffset: read
        uint8_t *readBegin = bufs[i].Buffer + (desiredByte - bufBeginOffset);
        uint8_t *readEnd   = bufs[i].Buffer + bufs[i].Length;

        vec.insert(vec.end(), readBegin, readEnd);
        desiredByte = chn.NextRecvByte = bufEndOffset;

        // Try to call user callback.
        uint32_t msglen;
        while (vec.size() >= 4)
        {
            memcpy(&msglen, vec.data(), 4);
            msglen = ntohl(msglen);
            if (vec.size() >= msglen + 4)
            {
                span<uint8_t> data{vec.data() + 4, msglen};
                sess.appContext.OnReliableReceive[index](
                    sess.CreateChannel(connCtx),
                    data,
                    sess.appContext.GlobalContext,
                    channelCtx.get());
                vec.erase(vec.begin(), vec.begin() + 4 + msglen);
            }
        }
    }

    return QUIC_STATUS_CONTINUE;
}

STREAM_HANDLER(QUIC_STREAM_EVENT_SEND_COMPLETE)
{
    RawBuffer *buf = (RawBuffer *)ev->SEND_COMPLETE.ClientContext;
    if (--buf->RefCount == 0) { delete[] buf; }

    return QUIC_STATUS_SUCCESS;
}

STREAM_HANDLER(QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN)
{
    // Need to do nothing.
    return QUIC_STATUS_SUCCESS;
}

STREAM_HANDLER(QUIC_STREAM_EVENT_PEER_SEND_ABORTED)
{
    // Need to do nothing.
    return QUIC_STATUS_ABORTED;
}

STREAM_HANDLER(QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED)
{
    // The peer sent reset frame that is not requested by us.
    MsQuic->StreamShutdown(strm, QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE, 0);
    return QUIC_STATUS_ABORTED;
}

STREAM_HANDLER(QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE)
{
    MsQuic->StreamClose(strm);
    return QUIC_STATUS_SUCCESS;
}

STREAM_HANDLER(QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE)
{
    // Because we close it at the destructor, don't close it at this event.
    return QUIC_STATUS_SUCCESS;
}

STREAM_HANDLER(QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE)
{
    // We don't use async send API and will not receive this event.
    return QUIC_STATUS_SUCCESS;
}

STREAM_HANDLER(QUIC_STREAM_EVENT_PEER_ACCEPTED)
{
    // Do nothing. We will not receive this event.
    return QUIC_STATUS_SUCCESS;
}

_Function_class_(QUIC_STREAM_CALLBACK)
inline QUIC_STATUS StreamCallback(
    HQUIC /* strm */ hndl, void *ctx, QUIC_STREAM_EVENT *ev) noexcept
try
{
    //printf("[strm%p]", hndl);
    switch (ev->Type)
    {
    DO_CASE(QUIC_STREAM_EVENT_START_COMPLETE);
    DO_CASE(QUIC_STREAM_EVENT_RECEIVE);
    DO_CASE(QUIC_STREAM_EVENT_SEND_COMPLETE);
    DO_CASE(QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN);
    DO_CASE(QUIC_STREAM_EVENT_PEER_SEND_ABORTED);
    DO_CASE(QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED);
    DO_CASE(QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE);
    DO_CASE(QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE);
    DO_CASE(QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE);
    DO_CASE(QUIC_STREAM_EVENT_PEER_ACCEPTED);
    default:
        puts("Unknown Event Type");
        return QUIC_STATUS_INVALID_STATE;
    }
}
catch (const exception &)
{
    // TODO: log
    return QUIC_STATUS_SUCCESS;
}
catch (...)
{
    // TODO: log
    return QUIC_STATUS_SUCCESS;
}

} // namespace ks3::detail


#undef DO_CASE
#undef LISTENER_PARAMS
#undef CONNECTION_PARAMS
#undef STREAM_PARAMS
#undef LISTENER_HANDLER
#undef CONNECTION_HANDLER
#undef STREAM_HANDLER
