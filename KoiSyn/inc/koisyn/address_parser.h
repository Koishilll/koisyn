#pragma once

#include "std/std_precomp.h"
#include "msquic.h"

namespace ks3::detail
{

struct AddressParser
{
    // try to parse IP string to internal representation. The second port is
    // ignored when address already contains one.
    // example:
    // 1. addrAndPort = "192.168.1.1", port = 54545 -> out = IPv4
    // 2. addrAndPort = "[::1]:54545", port = <not used> -> out = IPv6
    // 3. use "[::]:0", or "::" and port 0, but not "[::]"
    static bool TryParse(
        string_view addrAndPort, uint16_t port, QUIC_ADDR &out
        ) noexcept
    {
        [[maybe_unused]] QUIC_ADDR_STR addrstr;
        const char *zstr = addrAndPort.data();
        size_t len = addrAndPort.length();

        // if addrAndPort is not a zero-terminated string, i.e. it is a slice
        // of string, then we have to copy it.
        if (addrAndPort.data()[len] != 0)
        {
            //len = min(len, 63);
            len = len < 63 ? len : 63;
            memcpy(&addrstr, addrAndPort.data(), len);
            addrstr.Address[len] = 0;
            zstr = addrstr.Address;
        }

        return QuicAddrFromString(zstr, port, &out);
    }

    // try parse, and if we find that it is a IPv4 address, then we add IPv6
    // mapping prefix ("::FFFF:") and try again.
    static bool TryParseMap6(
        string_view addrAndPort, uint16_t port, QUIC_ADDR &out
        ) noexcept
    {
        bool ok = TryParse(addrAndPort, port, out);
        if (not ok) { return false; }

        if (out.Ipv4.sin_family == QUIC_ADDRESS_FAMILY_INET)
        {
            QUIC_ADDR_STR addrstr;
            memcpy(&addrstr, "::FFFF:", 7);

            size_t len = addrAndPort.length();
            //len = min(len, 63 - 7);
            len = len < 56 ? len : 56;
            memcpy(addrstr.Address + 7, addrAndPort.data(), len);
            addrstr.Address[len + 7] = 0;

            ok = TryParse(string_view{addrstr.Address, len + 7}, port, out);
        }
        return ok;
    }
};

} // namespace ks3::detail
