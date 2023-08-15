#pragma once

#include "../std/std_precomp.h"
#include "../msquic.h"

namespace ks3::detail
{

using namespace std;

// For now, we just use this temporary certificate and key to enable QUIC
// encryption, but it's unsafe if handshake packet was stolen by the middle.
// We don't think a game state is some top secret that should be protect on a
// high level, but if you need to, you should consider using a real certificate.
// Nevertheless, it's still better than raw UDP datagram without encryption
// at all.
inline constexpr string_view CertRaw = R"(-----BEGIN CERTIFICATE-----
MIICjjCCAfCgAwIBAgIUVMd9cxBei9IBadcTOYP2E9GgRPEwCgYIKoZIzj0EAwIw
WTELMAkGA1UEBhMCQVUxEzARBgNVBAgMClNvbWUtU3RhdGUxITAfBgNVBAoMGElu
dGVybmV0IFdpZGdpdHMgUHR5IEx0ZDESMBAGA1UEAwwJUXVpYyBUZXN0MB4XDTIy
MTAyNjE1MjMxMFoXDTIyMTAyODE1MjMxMFowWTELMAkGA1UEBhMCQVUxEzARBgNV
BAgMClNvbWUtU3RhdGUxITAfBgNVBAoMGEludGVybmV0IFdpZGdpdHMgUHR5IEx0
ZDESMBAGA1UEAwwJUXVpYyBUZXN0MIGbMBAGByqGSM49AgEGBSuBBAAjA4GGAAQB
Mm5WoJJVOFhwIHsk710M7OgIQNyYPXZFquwPU8wOIxU0aWZNgUcge5grrspJigU+
K4gNYxToSs9qkGNsj5+jdMUAEWeddPXUq6b1JP6Lf2afuJBZdADv7qzQwrtOmkwP
4DIxRgVcY+v2nVH3ZDeAmnbW4OMpL9tOuQsq+c3HnwmpKZujUzBRMB0GA1UdDgQW
BBRZColZMh4F40ePUjdfe5GvyKG3ajAfBgNVHSMEGDAWgBRZColZMh4F40ePUjdf
e5GvyKG3ajAPBgNVHRMBAf8EBTADAQH/MAoGCCqGSM49BAMCA4GLADCBhwJBIEob
GXhcTQdU/qskUCvQUjvFfJuNQYVVIYQ07S1Lz6QEZL53AurYHxoG/XIQ4SX76OF7
fbRALSy9tG2Fs3hy7GsCQgCDnK+8DPcMzFYVr1yKU+ZPYwLadvSxoJnBDIyNiWRC
21oqV0HwuiXcJALmI+sPl3Tvaf1rrLp1rtLEHLhcjl+T4Q==
-----END CERTIFICATE-----
)"sv;

inline constexpr string_view KeyRaw = R"(-----BEGIN EC PARAMETERS-----
BgUrgQQAIw==
-----END EC PARAMETERS-----
-----BEGIN EC PRIVATE KEY-----
MIHcAgEBBEIB+PCKc0FRsUyem41JLPWvAlONujHxsRdrbeY12KHn5fBH/Dq97BQW
3nILan+NrpN1hbuZM07lx4hSDHlBz2WeoMygBwYFK4EEACOhgYkDgYYABAEyblag
klU4WHAgeyTvXQzs6AhA3Jg9dkWq7A9TzA4jFTRpZk2BRyB7mCuuykmKBT4riA1j
FOhKz2qQY2yPn6N0xQARZ5109dSrpvUk/ot/Zp+4kFl0AO/urNDCu06aTA/gMjFG
BVxj6/adUfdkN4Cadtbg4ykv2065Cyr5zcefCakpmw==
-----END EC PRIVATE KEY-----
)"sv;

inline constexpr const wchar_t *CertFileName = L"koisyn-cert.pem";
inline constexpr const wchar_t *KeyFileName = L"koisyn-key.pem";
inline constexpr const char *AppName = "MyGame with KoiSyn";
inline constexpr uint8_t AlpnData[] = { "mygame-ksyn" };
inline constexpr QUIC_BUFFER Alpn{sizeof("mygame-ksyn") - 1, const_cast<uint8_t *>(AlpnData)};

inline const QUIC_API_TABLE *MsQuic;

inline QUIC_SETTINGS MakeClientSettings() noexcept
{
    QUIC_SETTINGS settings{};
    constexpr int True = 1;
    constexpr int False = 0;

    // Can be idle for 15s before closed
    settings.IdleTimeoutMs = 15'000;
    settings.IsSet.IdleTimeoutMs = True;

    //// Keep alive per 5s
    //settings.KeepAliveIntervalMs = 5'000;
    //settings.IsSet.KeepAliveIntervalMs = True;

    // Max ack delay 8ms (half of frame time)
    settings.MaxAckDelayMs = 8;
    settings.IsSet.MaxAckDelayMs = True;

    // No need to pace sending
    settings.PacingEnabled = False;
    settings.IsSet.PacingEnabled = True;

    // Enable unreliable sending
    settings.DatagramReceiveEnabled = True;
    settings.IsSet.DatagramReceiveEnabled = True;

    // Don't show "QUIC bit" to the middle
    settings.GreaseQuicBitEnabled = True;
    settings.IsSet.GreaseQuicBitEnabled = True;

    return settings;
}

inline QUIC_SETTINGS MakeServerSettings() noexcept
{
    // Allow client side to open up to 4 bidi-stream.
    // Note: since we open 2 connections at one endpoint (the endpoint we are
    // saying means a client and a server on a host), and opens 4 bidi-streams
    // on client side, we have 8 channel at one side to send, but we only
    // provide 4 of 8 channels to send freely, and the other 4 channels is used
    // as duplications.
    QUIC_SETTINGS settings = MakeClientSettings();
    constexpr int True = 1;

    settings.PeerBidiStreamCount = 4;
    settings.IsSet.PeerBidiStreamCount = True;

    return settings;
}

// make certificate for encryption
inline error_code MakeCertificate(string &certPathStr, string &keyPathStr)
    noexcept
try
{
    filesystem::path tempPath, certPath, keyPath;
    error_code ec;

    tempPath = GetTempDirectoryPath(ec);
    if (ec) { return ec; }

    certPath = tempPath / CertFileName;
    keyPath = tempPath / KeyFileName;
    bool bothExists = filesystem::exists(certPath, ec);
    if (ec) { return ec; }

    bothExists &= filesystem::exists(keyPath, ec);
    if (ec) { return ec; }

    if (not bothExists)
    {
        ofstream{certPath.c_str()} << CertRaw;
        ofstream{keyPath.c_str()} << KeyRaw;
    }

    certPathStr = certPath.generic_string();
    keyPathStr = keyPath.generic_string();
    return {};
}
catch (...)
{
    // TODO: log
    return {-1, generic_category()};
}

class MsQuicLoader
{
public:
    enum class ErrorType
    {
        Uninitialized = -1,
        Success = 0,
        MsQuicOpen2Error = 1,
        RegistrationOpenError = 2,
        WriteCertificateError = 3,
        ServerConfigOpenError = 4,
        ServerConfigLoadCredentialError = 5,
        ClientConfigOpenError = 6,
        ClientConfigLoadCredentialError = 7,
    };

    struct Error
    {
        ErrorType Type;
        QUIC_STATUS Code;
        // If error occurs, return true
        operator bool() noexcept { return Type != ErrorType::Success; }
    };

    Error InitError = {ErrorType::Uninitialized, QUIC_STATUS_INVALID_STATE};
    HQUIC Registration;
    HQUIC ClientConfig;
    HQUIC ServerConfig;

public:
    MsQuicLoader() noexcept
    {
        InitError = Startup();
    }

    ~MsQuicLoader() noexcept
    {
        InitError = {ErrorType::Uninitialized, QUIC_STATUS_INVALID_STATE};
        Cleanup();
    }

    /*
    class Listener;
    class Connection;
    class Stream;
    */

private:
    // load MsQuic library and initialize these object:
    // msquic -> registration -> server configuration -> client configuration
    Error Startup() noexcept
    {
        QUIC_STATUS status = MsQuicOpen2(&MsQuic);
        if (QUIC_FAILED(status))
        {
            return {ErrorType::MsQuicOpen2Error, status};
        }

        // open registration
        auto regConfig = QUIC_REGISTRATION_CONFIG{
            AppName, QUIC_EXECUTION_PROFILE_TYPE_REAL_TIME};
        status = MsQuic->RegistrationOpen(&regConfig, &Registration);
        if (QUIC_FAILED(status))
        {
            return {ErrorType::RegistrationOpenError, status};
        }

        // make certificate
        auto certPathStr = string{};
        auto keyPathStr = string{};
        error_code ec;
        if ((ec = MakeCertificate(certPathStr, keyPathStr)))
        {
            return {ErrorType::WriteCertificateError, ec.value()};
        }

        // make server credential configuration
        auto credcfgsvr = QUIC_CREDENTIAL_CONFIG{};
        auto certkey = QUIC_CERTIFICATE_FILE{};
        certkey.CertificateFile = certPathStr.c_str();
        certkey.PrivateKeyFile = keyPathStr.c_str();
        credcfgsvr.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
        credcfgsvr.CertificateFile = &certkey;

        // make server and client setting
        auto serverSettings = MakeServerSettings();
        auto clientSettings = MakeClientSettings();

        // open server configuration
        status = MsQuic->ConfigurationOpen(
            Registration,
            &Alpn,
            1,
            &serverSettings,
            sizeof(serverSettings),
            nullptr,
            &ServerConfig);
        if (QUIC_FAILED(status))
        {
            // TODO: log
            return {ErrorType::ServerConfigOpenError, status};
        }

        // load server credential
        status = MsQuic->ConfigurationLoadCredential(
            ServerConfig, &credcfgsvr);
        if (QUIC_FAILED(status))
        {
            // TODO: log
            return {ErrorType::ServerConfigLoadCredentialError, status};
        }

        // make client credential configuration
        // since we use a fake certificate, there is no need to validation
        auto credcfgcli = QUIC_CREDENTIAL_CONFIG{};
        credcfgcli.Type = QUIC_CREDENTIAL_TYPE_NONE;
        credcfgcli.Flags = QUIC_CREDENTIAL_FLAG_CLIENT |
            QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

        // open client configuration
        status = MsQuic->ConfigurationOpen(
            Registration,
            &Alpn,
            1,
            &clientSettings,
            sizeof(clientSettings),
            nullptr,
            &ClientConfig);
        if (QUIC_FAILED(status))
        {
            // TODO: log
            return {ErrorType::ClientConfigOpenError, status};
        }

        // load client credential
        status = MsQuic->ConfigurationLoadCredential(
            ClientConfig, &credcfgcli);
        if (QUIC_FAILED(status))
        {
            // TODO: log
            return {ErrorType::ClientConfigLoadCredentialError, status};
        }

        return {ErrorType::Success, 0};
    }

    void Cleanup() noexcept
    {
        if (ServerConfig)
        {
            MsQuic->ConfigurationClose(ServerConfig);
            ServerConfig = nullptr;
        }
        if (ClientConfig)
        {
            MsQuic->ConfigurationClose(ClientConfig);
            ClientConfig = nullptr;
        }
        if (Registration)
        {
            MsQuic->RegistrationShutdown(Registration,
                QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT, 0ULL);
            // block until all registered objects are closed
            MsQuic->RegistrationClose(Registration);
            Registration = nullptr;
        }
        if (MsQuic)
        {
            MsQuicClose(MsQuic);
            MsQuic = nullptr;
        }
    }
};

} // namespace ks3::detail
