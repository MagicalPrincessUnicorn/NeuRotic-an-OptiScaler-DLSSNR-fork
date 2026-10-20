#include "../contracts/Test002Support.h"
#if __has_include(<nr/protocol/ProfileRegistry.h>)
#include <nr/protocol/ProfileRegistry.h>
#define NR_PROTOCOL_REGISTRY 1
#endif
using namespace NrTest002;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("protocol-registry",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_PROTOCOL_REGISTRY
    namespace P=Neurotic::Protocol;
    const auto* native=P::LookupProfile(P::Family::LegacyCompatibleNativeDlssNr,1);
    const auto* bypass=P::LookupProfile(P::Family::NoOpBypass,1);
    r.Check("T25.registry-two-families",native&&bypass&&native->modelEvaluation&&!bypass->modelEvaluation,true,"LookupProfile");
    r.Check("T25.future-profile-refused",!P::LookupProfile(P::Family::LegacyCompatibleNativeDlssNr,2),true,"LookupProfile");
    r.Check("T25.unknown-profile-refused",!P::LookupProfile(static_cast<P::Family>(99),1),true,"LookupProfile");
    if(!native||!bypass)return r.Finish();
    P::RuntimeContract runtime;runtime.source={C::OwnerDomain::Provider,Sym("NR.BackendContract"),{1,0},Key("backend",1),1};
    runtime.available=Known(true);runtime.family=Sym("AlphaFeature18");runtime.abiVersion=1;runtime.featureKind=18;
    const auto key=P::BuildProfileKey(*native,runtime,C::GraphicsApi::D3D12,C::Placement::NativeBefore,Sym("NativeTemporal"));
    r.Check("T25.profile-key",key&&key->consumer==Sym("LegacyCompatibleNativeDlssNr")&&key->version==1,true,"BuildProfileKey");
    r.Check("T25.key-deterministic",key==P::BuildProfileKey(*native,runtime,C::GraphicsApi::D3D12,C::Placement::NativeBefore,Sym("NativeTemporal")),true,"BuildProfileKey");
    r.Check("T25.placement-in-key",key!=P::BuildProfileKey(*native,runtime,C::GraphicsApi::D3D12,C::Placement::NativeAfter,Sym("NativeTemporal")),true,"BuildProfileKey");
    r.Check("T25.api-not-covered",!P::BuildProfileKey(*native,runtime,C::GraphicsApi::Vulkan,C::Placement::NativeBefore,Sym("NativeTemporal")),true,"BuildProfileKey");
    r.Check("T25.present-not-covered",!P::BuildProfileKey(*native,runtime,C::GraphicsApi::D3D12,C::Placement::UnifiedPresent,Sym("NativeTemporal")),true,"BuildProfileKey");
    runtime.modelRevision=Known(C::VersionNumber{1,0});
    r.Check("T25.model-revision-in-key",key!=P::BuildProfileKey(*native,runtime,C::GraphicsApi::D3D12,C::Placement::NativeBefore,Sym("NativeTemporal")),true,"BuildProfileKey");
    runtime.modelRevision={};runtime.runtimeRevision=Known(C::VersionNumber{1,0});
    r.Check("T25.runtime-revision-in-key",key!=P::BuildProfileKey(*native,runtime,C::GraphicsApi::D3D12,C::Placement::NativeBefore,Sym("NativeTemporal")),true,"BuildProfileKey");
    runtime.runtimeRevision={};runtime.abiVersion=2;
    r.Check("T25.unknown-abi-refused",!P::BuildProfileKey(*native,runtime,C::GraphicsApi::D3D12,C::Placement::NativeBefore,Sym("NativeTemporal")),true,"BuildProfileKey");
    r.Check("T26.auto-skin-is-scalar",P::Support(*native,P::Purpose::AutoSkinMaskEnable)==P::PurposeSupport::ScalarControl,true,"Support");
    r.Check("T26.ui-unbound",P::Support(*native,P::Purpose::UILayer)==P::PurposeSupport::ExplicitNull,true,"Support");
    r.Check("T26.control-mask-unknown",P::Support(*native,P::Purpose::ControlMask)==P::PurposeSupport::Unknown,true,"Support");
    r.Check("T26.reactive-unsupported",P::Support(*native,P::Purpose::ReactiveMask)==P::PurposeSupport::Unsupported,true,"Support");
    r.Check("T26.transparency-unsupported",P::Support(*native,P::Purpose::TransparencyCompositionMask)==P::PurposeSupport::Unsupported,true,"Support");
    r.Check("T26.application-unsupported",P::Support(*native,P::Purpose::ApplicationMask)==P::PurposeSupport::Unsupported,true,"Support");
    r.Check("T26.ui-exclusion-unsupported",P::Support(*native,P::Purpose::UIExclusionMask)==P::PurposeSupport::Unsupported,true,"Support");
    r.Check("T26.generic-history-unsupported",P::Support(*native,P::Purpose::GenericHistoryRejectionMask)==P::PurposeSupport::Unsupported,true,"Support");
    r.Check("T26.motion-not-rejection",P::Support(*native,P::Purpose::MotionVectorGuide)==P::PurposeSupport::Required&&P::Support(*native,P::Purpose::MotionRejectionMask)==P::PurposeSupport::Unsupported,true,"Support");
    r.Check("T26.bypass-no-bindings",P::Support(*bypass,P::Purpose::ColorModelInput)==P::PurposeSupport::Unsupported,true,"Support");
    r.Check("T26.reactive-purpose-distinct",C::EnumName(P::Purpose::ReactiveMask)=="ReactiveMask",true,"PurposeIdentity");
    r.Check("T26.transparency-purpose-distinct",C::EnumName(P::Purpose::TransparencyCompositionMask)=="TransparencyCompositionMask",true,"PurposeIdentity");
    r.Check("T26.control-purpose-distinct",C::EnumName(P::Purpose::ControlMask)=="ControlMask",true,"PurposeIdentity");
    r.Check("T26.autoskin-purpose-distinct",C::EnumName(P::Purpose::AutoSkinMaskEnable)=="AutoSkinMaskEnable",true,"PurposeIdentity");
    r.Check("T26.motion-purpose-distinct",C::EnumName(P::Purpose::MotionVectorGuide)=="MotionVectorGuide",true,"PurposeIdentity");
    r.Check("T26.uialpha-purpose-distinct",C::EnumName(P::Purpose::UIAlpha)=="UIAlpha",true,"PurposeIdentity");
#else
    r.Check("T25.registry-production",false,true,"LookupProfile");
#endif
    return r.Finish();});
}
