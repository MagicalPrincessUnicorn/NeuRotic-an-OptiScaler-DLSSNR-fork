#pragma once
#include "ProfileTypes.h"
#include <string>

namespace Neurotic::Protocol
{
inline const ProfileDescriptor* LookupProfile(Family family,std::uint32_t version)noexcept
{
    static constexpr ProfileDescriptor native{Family::LegacyCompatibleNativeDlssNr,1,1,1,true};
    static constexpr ProfileDescriptor bypass{Family::NoOpBypass,1,1,1,false};
    if(version!=1)return nullptr;
    if(family==native.family)return &native;if(family==bypass.family)return &bypass;return nullptr;
}
inline PurposeSupport Support(const ProfileDescriptor& profile,Purpose purpose)noexcept
{
    if(!profile.modelEvaluation)return PurposeSupport::Unsupported;
    switch(purpose)
    {
    case Purpose::ColorModelInput:case Purpose::OutputModelTarget:case Purpose::DepthGuide:case Purpose::MotionVectorGuide:return PurposeSupport::Required;
    case Purpose::ProjectionJitter:case Purpose::PreExposureScalar:case Purpose::ExposureResource:return PurposeSupport::Optional;
    case Purpose::AutoSkinMaskEnable:return PurposeSupport::ScalarControl;
    case Purpose::UILayer:case Purpose::UIAlpha:case Purpose::BackbufferForUICorrection:return PurposeSupport::ExplicitNull;
    case Purpose::ControlMask:return PurposeSupport::Unknown;
    default:return PurposeSupport::Unsupported;
    }
}
inline std::optional<C::ProfileKey> BuildProfileKey(const ProfileDescriptor& profile,const RuntimeContract& runtime,
    C::GraphicsApi api,C::Placement placement,const C::Symbol& strategy)
{
    const auto* declared=LookupProfile(profile.family,profile.schemaVersion);
    if(!declared||profile.bindingVersion!=declared->bindingVersion||profile.transformVersion!=declared->transformVersion||
       profile.modelEvaluation!=declared->modelEvaluation||!Context::ValidValues(api)||!Context::ValidValues(placement)||strategy.Empty())return {};
    if(profile.modelEvaluation&&(!Context::ValidValues(runtime)||runtime.source.owner!=C::OwnerDomain::Provider||
       runtime.family.View()!="AlphaFeature18"||runtime.abiVersion!=1||runtime.featureKind!=18||api!=C::GraphicsApi::D3D12||
       placement==C::Placement::UnifiedPresent||strategy.View()!="NativeTemporal"))return {};
    const auto revision=[](const auto& fact){if(!Context::Established(fact))return std::string{"U"};
        const auto& v=fact.KnownPart()->value;return std::string{"K"}+std::to_string(v.major)+":"+std::to_string(v.minor);};
    C::ProfileKey result;result.consumer=Symbol(profile.modelEvaluation?"LegacyCompatibleNativeDlssNr":"NoOpBypass");
    // Complete bounded contract encoding, never a hash. Scope/content/rights/current tuning
    // are not accepted by this function and cannot churn the profile identity.
    std::string encoded=std::string(C::EnumName(api))+":"+std::to_string(strategy.View().size())+":"+std::string(strategy.View())+":"+
        std::string(C::EnumName(placement))+":b1:t1";
    if(profile.modelEvaluation)encoded+=":a1:f18:r"+revision(runtime.runtimeRevision)+":m"+revision(runtime.modelRevision);
    if(!result.profile.Assign(encoded))return {};result.version=profile.schemaVersion;return result;
}
}
