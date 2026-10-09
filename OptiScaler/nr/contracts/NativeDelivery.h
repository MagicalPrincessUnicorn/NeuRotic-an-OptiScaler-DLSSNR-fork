#pragma once
#include "SemanticDescriptions.h"

namespace Neurotic::Contracts
{
// W03-SPECTRE-AMENDMENT-1: metadata only. These values issue no identity,
// resource permission or final seal, including after deserialization.
enum class NativeDeliveryKind : std::uint8_t { Invalid=0,HostReturn=1,FinalConsumerRequired=2 };
template<>struct EnumTraits<NativeDeliveryKind>
{
    inline static constexpr auto Values=std::array{
        std::pair{NativeDeliveryKind::Invalid,std::string_view{"Invalid"}},
        std::pair{NativeDeliveryKind::HostReturn,std::string_view{"HostReturn"}},
        std::pair{NativeDeliveryKind::FinalConsumerRequired,std::string_view{"FinalConsumerRequired"}}};
};
enum class NativeReturnBoundary : std::uint8_t { Invalid=0,NgxEvaluateReturn=1 };
template<>struct EnumTraits<NativeReturnBoundary>
{
    inline static constexpr auto Values=std::array{
        std::pair{NativeReturnBoundary::Invalid,std::string_view{"Invalid"}},
        std::pair{NativeReturnBoundary::NgxEvaluateReturn,std::string_view{"NgxEvaluateReturn"}}};
};
enum class NativeHistoryPolicy : std::uint8_t { Invalid=0,OrderedNative=1 };
template<>struct EnumTraits<NativeHistoryPolicy>
{
    inline static constexpr auto Values=std::array{
        std::pair{NativeHistoryPolicy::Invalid,std::string_view{"Invalid"}},
        std::pair{NativeHistoryPolicy::OrderedNative,std::string_view{"OrderedNative"}}};
};
struct NativeFinalConsumerContractV1
{
    std::uint32_t version=1;
    ProviderIncarnation provider;
    HandoffContractGeneration handoff;
    Symbol contract;
    std::uint32_t contractVersion=0;
    BoundaryKind boundary=BoundaryKind::BeforeFg;
    ColorDomain color=ColorDomain::SceneLinear;
    bool hudIncluded=false,toneMapped=false;
    Extent extent;
    inline static constexpr std::string_view WireName="NativeFinalConsumerContractV1";
    static constexpr auto Fields()
    {
        return std::tuple{Field("version",&NativeFinalConsumerContractV1::version),
            Field("provider",&NativeFinalConsumerContractV1::provider),Field("handoff",&NativeFinalConsumerContractV1::handoff),
            Field("contract",&NativeFinalConsumerContractV1::contract),Field("contractVersion",&NativeFinalConsumerContractV1::contractVersion),
            Field("boundary",&NativeFinalConsumerContractV1::boundary),Field("color",&NativeFinalConsumerContractV1::color),
            Field("hudIncluded",&NativeFinalConsumerContractV1::hudIncluded),Field("toneMapped",&NativeFinalConsumerContractV1::toneMapped),
            Field("extent",&NativeFinalConsumerContractV1::extent)};
    }
    Error Check()const
    {
        if(version!=1)return Error::UnsupportedVersion;
        if(contract.Empty()||!contractVersion||boundary!=BoundaryKind::BeforeFg||!extent.width||!extent.height)
            return Error::MissingField;
        return Error::None;
    }
    bool operator==(const NativeFinalConsumerContractV1&)const=default;
};
struct NativeDeliveryContractV1
{
    std::uint32_t version=1;
    NativeDeliveryKind kind=NativeDeliveryKind::Invalid;
    ProfileKey profile;
    Placement placement=Placement::NativeAfter;
    NativeReturnBoundary returnBoundary=NativeReturnBoundary::Invalid;
    NativeHistoryPolicy historyPolicy=NativeHistoryPolicy::Invalid;
    std::optional<MetadataRef<NativeFinalConsumerContractV1>> finalConsumer;
    inline static constexpr std::string_view WireName="NativeDeliveryContractV1";
    static constexpr auto Fields()
    {
        return std::tuple{Field("version",&NativeDeliveryContractV1::version),Field("kind",&NativeDeliveryContractV1::kind),
            Field("profile",&NativeDeliveryContractV1::profile),Field("placement",&NativeDeliveryContractV1::placement),
            Field("returnBoundary",&NativeDeliveryContractV1::returnBoundary),Field("historyPolicy",&NativeDeliveryContractV1::historyPolicy),
            Field("finalConsumer",&NativeDeliveryContractV1::finalConsumer)};
    }
    Error Check()const
    {
        if(version!=1)return Error::UnsupportedVersion;
        if((placement!=Placement::NativeBefore&&placement!=Placement::NativeAfter)||
            returnBoundary!=NativeReturnBoundary::NgxEvaluateReturn||historyPolicy!=NativeHistoryPolicy::OrderedNative)
            return Error::Malformed;
        if(kind==NativeDeliveryKind::HostReturn)return finalConsumer?Error::Malformed:Error::None;
        if(kind!=NativeDeliveryKind::FinalConsumerRequired||!finalConsumer)return Error::MissingField;
        if(finalConsumer->schemaVersion!=SchemaVersion{1,0})return Error::UnsupportedVersion;
        return finalConsumer->owner==OwnerDomain::FrameGeneration?Error::None:Error::WrongOwner;
    }
    bool operator==(const NativeDeliveryContractV1&)const=default;
};
}
