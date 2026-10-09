#pragma once
#include "Identity.h"

namespace Neurotic::Contracts
{
// AP1/AP6: a versioned observation subject. No original-content identity,
// execution permission, continuity or physical retirement is asserted here.
// In particular this record is deliberately not a member of FrameIdentity.
struct InterceptedSourceTransactionV1
{
    std::uint32_t version=1;
    InterceptedSourceTransactionIdV1 id;
    NativeSampleIdentityV1 sample;
    EpisodeId episode;
    OptionalFact<ObjectIncarnation> device;
    inline static constexpr std::string_view WireName="InterceptedSourceTransactionV1";
    static constexpr auto Fields()
    {
        return std::tuple{Field("version",&InterceptedSourceTransactionV1::version),
            Field("id",&InterceptedSourceTransactionV1::id),Field("sample",&InterceptedSourceTransactionV1::sample),
            Field("episode",&InterceptedSourceTransactionV1::episode),Field("device",&InterceptedSourceTransactionV1::device)};
    }
    Error Check()const
    {
        if(version!=1)return Error::UnsupportedVersion;
        if(!id.value||id.Check()!=Error::None||sample.Check()!=Error::None||!episode.value||episode.Check()!=Error::None)
            return Error::MissingProvenance;
        // Device remains Unknown until actual Resource-owner observation.
        return Error::None;
    }
    bool operator==(const InterceptedSourceTransactionV1&)const=default;
};
}
