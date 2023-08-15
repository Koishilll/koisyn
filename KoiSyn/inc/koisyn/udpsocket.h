#pragma once

#include "std/std_precomp.h"
#include "platform/koisyn_platform.h"
#include "address_parser.h"

namespace ks3::detail
{

using namespace std;

// A very simple cross platform UDP socket Wrapper.
// It is just a workaround. If we have std::udp or something in the future, it
// will be replaced.
class UdpSocket
{
public:

protected:
    SOCKET sock;

public:
    UdpSocket() noexcept : sock{BADSOCKET} {};
    UdpSocket(const UdpSocket &)            = delete;
    UdpSocket(UdpSocket &&other) noexcept
    {
        sock = other.sock;
        other.sock = BADSOCKET;
    }

    void operator=(const UdpSocket &) = delete;
    void operator=(UdpSocket &&other) noexcept
    {
        if (this == &other) { return; }

        CLOSESOCKET(sock);
        sock = other.sock;
        other.sock = BADSOCKET;
    }

    ~UdpSocket() noexcept
    {
        CLOSESOCKET(sock);
        sock = BADSOCKET;
    }

    void swap(UdpSocket &other) noexcept
    {
        std::swap(sock, other.sock);
    }

    bool IsOpened() const noexcept
    {
        return ISVALIDSOCK(sock);
    }

    // Factory. Bind to a specific endpoint i.e. address + port, or a endpoint
    // with unspecific port choosen by the OS.
    // If address already contains a non-zero port, the second parameter will
    // be ignored.
    // Return an instance of UdpSocket on success.
    static auto Bind(string_view addr = "::"sv, uint16_t port = 0)
        noexcept -> optional<UdpSocket>
    {
        QUIC_ADDR localaddr{};
        // error C4805: '!=': unsafe mix of type 'BOOLEAN' and type 'bool'
        // in operation
        if (not QuicAddrFromString(addr.data(), port, &localaddr))
        {
            return nullopt;
        }
        return Bind(localaddr);
    }

    static auto Bind(uint16_t port) noexcept
    {
        return Bind("::"sv, port);
    }

    uint16_t GetPort() const noexcept
    {
        if (not ISVALIDSOCK(sock)) { return 0; } // socket closed
        QUIC_ADDR localhost{};
        socklen_t len = sizeof(localhost);
        int ret = getsockname(sock, (sockaddr *)&localhost, &len);
        return ret != 0 ? 0 : QuicAddrGetPort(&localhost);
    }

    bool SendTo(
        string_view addrAndPort,
        uint16_t port,
        span<const uint8_t> data
        ) const noexcept
    {
        QUIC_ADDR remote;
        bool ok = AddressParser::TryParseMap6(addrAndPort, port, remote);
        if (not ok) { return false; }

        int ret = sendto(sock, (char *)data.data(), (int)data.size(), 0,
            (sockaddr *)&remote, sizeof(remote.Ipv6));
        return ret != 0;
    }

    // We don't verify the address in this overload. You should make sure that
    // your endpoint is IPv6 or IPv6-mapped address.
    bool SendTo(const QUIC_ADDR &endpoint, span<const uint8_t> data)
        const noexcept
    {
        int ret = sendto(sock, (char *)data.data(), (int)data.size(), 0,
            (sockaddr *)&endpoint, sizeof(endpoint.Ipv6));
        return ret != 0;
    }

    // Current thread will be blocked until receiving a datagram.
    //
    // On success, length of received data will be returned with filled buffer
    // and remote address. If buffer is too small, the length of received data
    // other than error code will be returned.
    //
    // On failure, error code less than 0 will be returned.
    int RecvFrom(const span<uint8_t> buf, QUIC_ADDR &remote) const noexcept
    {
        socklen_t remotelen = sizeof(remote);
        int lenOrErrc = recvfrom(sock, (char *)buf.data(), (int)buf.size(), 0,
            (sockaddr *)&remote, &remotelen);

        // note: on Windows, if the length of the buffer is too small, the
        // recvfrom call will fail with WSAGetLastError set to WSAEMSGSIZE;
        // on Linux, it will silently discard the excess bytes. See:
        // https://github.com/zephyrproject-rtos/zephyr/issues/31100
        // Anyway, we just want to silently discard the excess bytes, so handle
        // it specially on Windows.
        return GetTruncatedLength(lenOrErrc, buf.size());
    }

private:
    static optional<UdpSocket> Bind(const QUIC_ADDR &localaddr) noexcept
    {
        static WsaLoader wsa;
        // startup failed
        if (not wsa.WinSockInitialized) [[unlikely]] { return nullopt; }

        SOCKET sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        // open failed
        if (not ISVALIDSOCK(sock))
        {
            // TODO: log
            return nullopt;
        }

        // Set IPv4 & v6 dual stack mode
        const int False = 0;
        int ret = setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY,
            (const char *)&False, 4);
        // set dual stack mode failed
        if (ret != 0)
        {
            // TODO: log
            CLOSESOCKET(sock);
            return nullopt;
        }

        ret = ::bind(sock, (sockaddr *)&localaddr, sizeof(localaddr));
        // bind failed
        if (ret != 0)
        {
            CLOSESOCKET(sock);
            return nullopt;
        }

        UdpSocket udpsock;
        udpsock.sock = sock;
        return udpsock;
    }
};

// A very simple wrapper of native socket handle.
// Note: since we don't have this socket, we should close socket manually.
class NonOwningUdpSocket : private UdpSocket
{
public:
    NonOwningUdpSocket() noexcept { sock = BADSOCKET; }
    NonOwningUdpSocket(const NonOwningUdpSocket &other) noexcept
    {
        sock = other.sock;
    }
    void operator=(const NonOwningUdpSocket &other) noexcept
    {
        sock = other.sock;
    }

    NonOwningUdpSocket(SOCKET existingSock) noexcept { sock = existingSock; }

    ~NonOwningUdpSocket() noexcept { sock = BADSOCKET; }

    using UdpSocket::SendTo;
    using UdpSocket::GetPort;
};

// Handler with a socket and a blocking loop receive thread.
// Instead of handle it on exception, we just log into file and return false.
class UdpHandler
{
private:
    thread recvThread;
    UdpSocket usk;

public:
    UdpHandler() noexcept                              = default;
    UdpHandler(const UdpHandler &)                     = delete;
    UdpHandler(UdpHandler &&other) noexcept            = default;
    UdpHandler &operator=(const UdpHandler &)          = delete;
    UdpHandler &operator=(UdpHandler &&other) noexcept = default;

    UdpHandler(UdpSocket &&sock) noexcept : usk(move(sock)) {}

    ~UdpHandler() noexcept
    {
        // close socket first, then wait thread exit
        usk = {};
        if (recvThread.joinable())
        {
            recvThread.join();
        }
    }

    // factory
    static auto Bind(string_view addr = "::"sv, uint16_t port = 0)
        noexcept -> optional<UdpHandler>
    {
        return UdpSocket::Bind(addr, port);
    }

    static auto Bind(uint16_t port) noexcept
    {
        return Bind("::"sv, port);
    }

    uint16_t GetPort() const noexcept
    {
        return usk.GetPort();
    }

    bool SendTo(string_view addr, uint16_t port, span<const uint8_t> data)
        const noexcept
    {
        return usk.SendTo(addr, port, data);
    }

    bool SendTo(const QUIC_ADDR &addr, span<const uint8_t> data)
        const noexcept
    {
        return usk.SendTo(addr, data);
    }

    // Start a new thread to listen to the port. Calling thread will return
    // immediately, while the new thread will be blocked by calling
    // recvfrom()s, and will not return until the UdpHandler it is attached to
    // is destructing.
    // Note: this function is not thread safe. It is your responsibility to
    // ensure that not to call this function on different thread.
    //
    // Return true if the new thread starts.
    template <int BufferLen = 1520, typename Fn>
        requires requires(
            Fn consume,
            span<const uint8_t> data,
            const QUIC_ADDR &remote
            )
        {
            { consume(data, remote) } noexcept;
        }
    bool RegisterRecvFromCallback(Fn &&consume) noexcept
    try
    {
        if (not usk.IsOpened()) { return false; }

        // If a receive thread is running, it can't accept another callback.
        if (recvThread.get_id() != thread::id{}) { return false; }

        // consume() will be forwarded other than moved.
        auto recvloop = [&usk = this->usk, consume = (Fn &&)consume]
            () noexcept
        {
            uint8_t buf[BufferLen]{};
            QUIC_ADDR remote{};
            while (true)
            {
                int ret = usk.RecvFrom({buf, BufferLen}, remote);
                if (ret < 0) { break; } // error occurred, or socket closed
                if (ret == 0) { continue; } // ignore 0 bytes of data
                consume(span{buf, (size_t)ret}, remote);
            }
        };
        recvThread = thread{move(recvloop)};
        return true;
    }
    catch (const exception &)
    {
        // thread creating failed
        // TODO: log
        return false;
    }

    bool IsRunning() const noexcept
    {
        return recvThread.get_id() != thread::id{};
    }

private:

};

} // namespace ks3::detail
