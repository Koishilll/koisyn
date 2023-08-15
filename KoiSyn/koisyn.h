#pragma once

#include "inc/koisyn/checksum.h"
#include "inc/koisyn/rpng.h"
#include "inc/koisyn/udpsocket.h"
#include "inc/koisyn/koisession.h"
#include "inc/koisyn/koisyn.h"

namespace ks3
{
    // rpng.h
    using detail::Lcg32;
    using detail::Lcg64;

    // checksum.h
    using detail::ConceptIntLike;
    using detail::ConceptTupleLikeButNotRange;
    using detail::ConceptChecksumCalculable;

    using detail::Hash32;
    using detail::Hash64;
    using detail::ToU32WithoutSignExtension;
    using detail::ToU64WithoutSignExtension;
    using detail::MixValue32;
    using detail::MixValue64;
    inline constexpr detail::GetChecksum_fn GetChecksum;

    // udpsocket.h
    using detail::UdpSocket;
    using detail::UdpHandler;

    // koisyn.h
    using detail::ConceptGameState;
    using detail::InputData;
    using detail::InputFor;
    using detail::KoiSynBase;
    using detail::KoiSyn;

    // koichan.h
    using detail::Kontext;
    using detail::KoiChan;

    // koisession.h
    using detail::KoiSession;
}
