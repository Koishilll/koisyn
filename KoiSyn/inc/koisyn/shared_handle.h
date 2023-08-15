#pragma once

#include "std/std_precomp.h"
#include "platform/msquic_loader.h"

namespace ks3::detail
{

using namespace std;

class SharedListener : shared_ptr<QUIC_HANDLE>
{
public:
    constexpr SharedListener()                   noexcept = default;
    SharedListener(const SharedListener &)                = default;
    SharedListener(SharedListener &&)            noexcept = default;
    SharedListener &operator=(const SharedListener &)     = default;
    SharedListener &operator=(SharedListener &&) noexcept = default;

    constexpr SharedListener(nullptr_t) noexcept : SharedListener{} {}

    // Note: we don't check the real type of the handle, so be careful!
    SharedListener(HQUIC lisn) noexcept :
        shared_ptr<QUIC_HANDLE>{lisn, ListenerDeleter} {};

    using shared_ptr<QUIC_HANDLE>::swap;
    using shared_ptr<QUIC_HANDLE>::get;
    using shared_ptr<QUIC_HANDLE>::operator bool;
    using shared_ptr<QUIC_HANDLE>::operator *;
    using shared_ptr<QUIC_HANDLE>::operator ->;

    void reset() noexcept
    {
        static_cast<shared_ptr<QUIC_HANDLE> &>(*this).reset();
    }

    void reset(HQUIC lisn)
    {
        static_cast<shared_ptr<QUIC_HANDLE> &>(*this).reset(
            lisn, ListenerDeleter);
    }

    static void ListenerDeleter(HQUIC lisn) noexcept
    {
        MsQuic->ListenerStop(lisn);
    }

    [[nodiscard]] static optional<SharedListener> Open(
        HQUIC registr,
        QUIC_LISTENER_CALLBACK_HANDLER callback,
        void *context
        ) noexcept
    {
        // open
        HQUIC lisn;
        QUIC_STATUS status;
        status = MsQuic->ListenerOpen(registr, callback, context, &lisn);
        if (QUIC_FAILED(status)) { return nullopt; }

        return SharedListener{lisn};
    }

    QUIC_STATUS Start() noexcept
    {
        QUIC_ADDR wildcard{};
        QuicAddrSetFamily(&wildcard, QUIC_ADDRESS_FAMILY_INET6);
        return MsQuic->ListenerStart(get(), &Alpn, 1, &wildcard);
    }

    [[nodiscard]] static optional<SharedListener> OpenAndStart(
        HQUIC registr,
        QUIC_LISTENER_CALLBACK_HANDLER callback,
        void *context
        ) noexcept
    {
        // open
        HQUIC lisn;
        QUIC_STATUS status;
        status = MsQuic->ListenerOpen(registr, callback, context, &lisn);
        if (QUIC_FAILED(status)) { return nullopt; }

        // start
        QUIC_ADDR wildcard{};
        QuicAddrSetFamily(&wildcard, QUIC_ADDRESS_FAMILY_INET6);
        status = MsQuic->ListenerStart(lisn, &Alpn, 1, &wildcard);
        if (QUIC_FAILED(status)) { return nullopt; }

        return SharedListener{lisn};
    }
};

class SharedConnection : shared_ptr<QUIC_HANDLE>
{
public:
    constexpr SharedConnection()                     noexcept = default;
    SharedConnection(const SharedConnection &)                = default;
    SharedConnection(SharedConnection &&)            noexcept = default;
    SharedConnection &operator=(const SharedConnection &)     = default;
    SharedConnection &operator=(SharedConnection &&) noexcept = default;

    constexpr SharedConnection(nullptr_t) noexcept : SharedConnection{} {}

    // Note: we don't check the real type of the handle, so be careful!
    SharedConnection(HQUIC conn) noexcept :
        shared_ptr<QUIC_HANDLE>{conn, ConnectionDeleter} {};

    using shared_ptr<QUIC_HANDLE>::swap;
    using shared_ptr<QUIC_HANDLE>::get;
    using shared_ptr<QUIC_HANDLE>::operator bool;
    using shared_ptr<QUIC_HANDLE>::operator *;
    using shared_ptr<QUIC_HANDLE>::operator ->;

    void reset() noexcept
    {
        static_cast<shared_ptr<QUIC_HANDLE> &>(*this).reset();
    }

    void reset(HQUIC conn)
    {
        static_cast<shared_ptr<QUIC_HANDLE> &>(*this).reset(
            conn, ConnectionDeleter);
    }

    static void ConnectionDeleter(HQUIC conn) noexcept
    {
        MsQuic->ConnectionShutdown(conn,
            QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
    }

    [[nodiscard]] static optional<SharedConnection> Open(
        HQUIC registr,
        QUIC_CONNECTION_CALLBACK_HANDLER callback,
        void *context
        ) noexcept
    {
        HQUIC conn;
        QUIC_STATUS status;
        status = MsQuic->ConnectionOpen(registr, callback, context, &conn);
        if (QUIC_FAILED(status)) { return nullopt; }

        return SharedConnection{conn};
    }

    QUIC_STATUS Start(
        HQUIC config,
        QUIC_ADDRESS_FAMILY family,
        const char *serverName,
        uint16_t serverPort
        ) noexcept
    {
        return MsQuic->ConnectionStart(
            get(), config, family, serverName, serverPort);
    }

    [[nodiscard]] static optional<SharedConnection> OpenAndStart(
        HQUIC registr,
        QUIC_CONNECTION_CALLBACK_HANDLER callback,
        void *context,
        HQUIC config,
        QUIC_ADDRESS_FAMILY family,
        const char *serverName,
        uint16_t serverPort
        ) noexcept
    {
        // open
        HQUIC conn;
        QUIC_STATUS status;
        status = MsQuic->ConnectionOpen(registr, callback, context, &conn);
        if (QUIC_FAILED(status)) { return nullopt; }

        // start
        status = MsQuic->ConnectionStart(
            conn, config, family, serverName, serverPort);
        if (QUIC_FAILED(status))
        {
            MsQuic->ConnectionClose(conn);
            return nullopt;
        }

        return SharedConnection{conn};
    }
};

class SharedStream : shared_ptr<QUIC_HANDLE>
{
public:
    constexpr SharedStream()                 noexcept = default;
    SharedStream(const SharedStream &)                = default;
    SharedStream(SharedStream &&)            noexcept = default;
    SharedStream &operator=(const SharedStream &)     = default;
    SharedStream &operator=(SharedStream &&) noexcept = default;

    constexpr SharedStream(nullptr_t) noexcept : SharedStream{} {}

    // Note: we don't check the real type of the handle, so be careful!
    SharedStream(HQUIC strm) noexcept :
        shared_ptr<QUIC_HANDLE>{strm, StreamDeleter} {};

    using shared_ptr<QUIC_HANDLE>::swap;
    using shared_ptr<QUIC_HANDLE>::get;
    using shared_ptr<QUIC_HANDLE>::operator bool;
    using shared_ptr<QUIC_HANDLE>::operator *;
    using shared_ptr<QUIC_HANDLE>::operator ->;

    void reset() noexcept
    {
        static_cast<shared_ptr<QUIC_HANDLE> &>(*this).reset();
    }

    void reset(HQUIC strm)
    {
        static_cast<shared_ptr<QUIC_HANDLE> &>(*this).reset(
            strm, StreamDeleter);
    }

    static void StreamDeleter(HQUIC strm) noexcept
    {
        MsQuic->StreamShutdown(strm,
            QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE, 0);
    }

    [[nodiscard]] static optional<SharedStream> Open(
        HQUIC conn,
        QUIC_STREAM_OPEN_FLAGS flags,
        QUIC_STREAM_CALLBACK_HANDLER callback,
        void *context
        ) noexcept
    {
        HQUIC strm;
        QUIC_STATUS status;
        status = MsQuic->StreamOpen(conn, flags, callback, context, &strm);
        if (QUIC_FAILED(status)) { return nullopt; }

        return SharedStream{strm};
    }

    QUIC_STATUS Start(QUIC_STREAM_START_FLAGS flags) noexcept
    {
        return MsQuic->StreamStart(get(), flags);
    }

    [[nodiscard]] static optional<SharedStream> OpenAndStart(
        HQUIC conn,
        QUIC_STREAM_OPEN_FLAGS oflags,
        QUIC_STREAM_CALLBACK_HANDLER callback,
        void *context,
        QUIC_STREAM_START_FLAGS sflags
        ) noexcept
    {
        // open
        HQUIC strm;
        QUIC_STATUS status;
        status = MsQuic->StreamOpen(conn, oflags, callback, context, &strm);
        if (QUIC_FAILED(status)) { return nullopt; }

        // start
        status = MsQuic->StreamStart(strm, sflags);
        if (QUIC_FAILED(status))
        {
            MsQuic->StreamClose(strm);
            return nullopt;
        }

        return SharedStream{strm};
    }
};

} // namespace ks3::detail
