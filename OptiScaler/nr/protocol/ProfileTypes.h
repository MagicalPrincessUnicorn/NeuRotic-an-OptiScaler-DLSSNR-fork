#pragma once
#include <nr/contracts/C06_ProtocolInput.h>
#include <nr/contracts/C04_Capability.h>
#include <nr/contracts/C12_Representation.h>
#include <nr/context/ContextMetadata.h>

namespace Neurotic::Protocol
{
namespace C=Contracts;
enum class Family{LegacyCompatibleNativeDlssNr,NoOpBypass};
enum class Purpose
{
    ColorModelInput,OutputModelTarget,DepthGuide,MotionVectorGuide,ProjectionJitter,PreExposureScalar,ExposureResource,
    AutoSkinMaskEnable,UILayer,UIAlpha,BackbufferForUICorrection,UIExclusionMask,ReactiveMask,
    TransparencyCompositionMask,ApplicationMask,MotionRejectionMask,ControlMask,GenericHistoryRejectionMask
};
enum class PurposeSupport{Required,Optional,ScalarControl,ExplicitNull,Unsupported,Unknown};
struct ProfileDescriptor
{
    Family family=Family::LegacyCompatibleNativeDlssNr;
    std::uint32_t schemaVersion=1,bindingVersion=1,transformVersion=1;
    bool modelEvaluation=true;
};
struct RuntimeContract
{
    C::OwnerValueReference source;
    C::Symbol family;
    std::uint32_t abiVersion=0,featureKind=0;
    C::OptionalFact<C::VersionNumber> runtimeRevision,modelRevision;
    C::OptionalFact<C::ProviderIncarnation> incarnation;
    C::OptionalFact<bool> available;
    inline static constexpr std::string_view WireName="RENDER.RuntimeContract";
    static constexpr auto Fields(){return std::tuple{C::Field("source",&RuntimeContract::source),C::Field("family",&RuntimeContract::family),
        C::Field("abiVersion",&RuntimeContract::abiVersion),C::Field("featureKind",&RuntimeContract::featureKind),
        C::Field("runtimeRevision",&RuntimeContract::runtimeRevision),C::Field("modelRevision",&RuntimeContract::modelRevision),
        C::Field("incarnation",&RuntimeContract::incarnation),C::Field("available",&RuntimeContract::available)};}
    bool operator==(const RuntimeContract&)const=default;
};
inline C::Symbol Symbol(std::string_view value){C::Symbol result;result.Assign(value);return result;}
}

namespace Neurotic::Contracts
{
template<>struct EnumTraits<Protocol::Purpose>
{
    using P=Protocol::Purpose;
    inline static constexpr auto Values=std::array{
        std::pair{P::ColorModelInput,std::string_view{"ColorModelInput"}},std::pair{P::OutputModelTarget,std::string_view{"OutputModelTarget"}},
        std::pair{P::DepthGuide,std::string_view{"DepthGuide"}},std::pair{P::MotionVectorGuide,std::string_view{"MotionVectorGuide"}},
        std::pair{P::ProjectionJitter,std::string_view{"ProjectionJitter"}},std::pair{P::PreExposureScalar,std::string_view{"PreExposureScalar"}},
        std::pair{P::ExposureResource,std::string_view{"ExposureResource"}},std::pair{P::AutoSkinMaskEnable,std::string_view{"AutoSkinMaskEnable"}},
        std::pair{P::UILayer,std::string_view{"UILayer"}},std::pair{P::UIAlpha,std::string_view{"UIAlpha"}},
        std::pair{P::BackbufferForUICorrection,std::string_view{"BackbufferForUICorrection"}},std::pair{P::UIExclusionMask,std::string_view{"UIExclusionMask"}},
        std::pair{P::ReactiveMask,std::string_view{"ReactiveMask"}},std::pair{P::TransparencyCompositionMask,std::string_view{"TransparencyCompositionMask"}},
        std::pair{P::ApplicationMask,std::string_view{"ApplicationMask"}},std::pair{P::MotionRejectionMask,std::string_view{"MotionRejectionMask"}},
        std::pair{P::ControlMask,std::string_view{"ControlMask"}},std::pair{P::GenericHistoryRejectionMask,std::string_view{"GenericHistoryRejectionMask"}}};
};
}
