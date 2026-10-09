#pragma once
#include "InterceptedSourceTransaction.h"
#include "NativeStageDelivery.h"
#include "C04_Capability.h"

namespace Neurotic::Contracts
{
// AP1-AP6: a separate, versioned requested contract, never an alias for Strict.
// These are owner publications, not an allow-unknown switch or live authority.
// Both independent qualifications must describe this exact native observation.
struct SourceBoundConsumerContractV1
{
    std::uint32_t version=1;
    ProfileKey profile;
    MetadataRef<NativeFinalConsumerContractV1> consumer;
    MetadataRef<QualificationCertificate> profileQualification,consumerQualification;
    inline static constexpr std::string_view WireName="SourceBoundConsumer-v1";
    static constexpr auto Fields()
    {
        return std::tuple{Field("version",&SourceBoundConsumerContractV1::version),
            Field("profile",&SourceBoundConsumerContractV1::profile),Field("consumer",&SourceBoundConsumerContractV1::consumer),
            Field("profileQualification",&SourceBoundConsumerContractV1::profileQualification),
            Field("consumerQualification",&SourceBoundConsumerContractV1::consumerQualification)};
    }
    Error Check()const
    {
        if(version!=1||consumer.schemaVersion!=SchemaVersion{1,0}||
           profileQualification.schemaVersion!=SchemaVersion{1,0}||consumerQualification.schemaVersion!=SchemaVersion{1,0})
            return Error::UnsupportedVersion;
        return consumer.owner==OwnerDomain::FrameGeneration&&profileQualification.owner==OwnerDomain::Strategy&&
            consumerQualification.owner==OwnerDomain::FrameGeneration?Error::None:Error::WrongOwner;
    }
    bool operator==(const SourceBoundConsumerContractV1&)const=default;
};
}
