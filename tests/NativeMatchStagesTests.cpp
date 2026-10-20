#include "NrConfigFixture.h"
#include <dlssnr/DlssNr_StageUi.h>
#include <dlssnr/NativeNrResolution.h>
#include <dlssnr/VulkanNrResolution.h>
#include <cstdio>
#include <map>
#include <string>

// All selection, sizing and persistence operations below are production code.
using Settings=TestConfig;
struct Ini {
    std::map<std::string,std::string> values;
    void SetBoolValue(const char*,const char* key,bool value){values[key]=value?"1":"0";}
    void SetDoubleValue(const char*,const char* key,double value){values[key]=std::to_string(value);}
    void SetLongValue(const char*,const char* key,long value){values[key]=std::to_string(value);}
};
unsigned checks=0,failures=0;
void Check(bool good,const char* name){++checks;if(!good)++failures;std::printf("%s %s\n",good?"PASS":"FAIL",name);}
int main(){
    namespace U=DlssNr::StageUi;namespace N=DlssNr::NativeResolution;
    Settings c;c.DlssNrRoute=0u;
    U::SelectUnifiedResolution(c,1);
    Check(c.DlssNrRunBeforeSr.value_or_default()&&U::ResolutionChoiceSelection(c)==1,
          "existing Match Native identity retains Pre Super Resolution");
    U::SelectUnifiedResolution(c,4); // Append after historical Full0/Pre1/Manual2/Legacy3.
    Check(!c.DlssNrRunBeforeSr.value_or_default()&&c.DlssNrNativeResolution.value_or_default()==1&&
          U::ResolutionChoiceSelection(c)==4,
          "new Post Super Resolution choice stores distinct automatic-native identity and After stage");
    // Independent stage/size requirement: input1600x900, SRoutput3840x2160.
    c.DlssNrRunBeforeSr=false;c.DlssNrRenderingMode=0;c.DlssNrNativeResolution=1u;
    c.DlssNrWorkingScale=1.0f;c.DlssNrUiManualResolution=false;
    Check(N::MatchAfterReconstruction(c,false,false),
          "D3D12 and bridge Post selection uses observed pre-SR raster without requiring RR");
    auto size=N::MatchWorkSize(1600,900,3840,2160);
    Check(size.width==1600&&size.height==896&&!size.reason,
          "automatic raster follows native input rather than SR output with existing alignment");
    DlssNr::VkNrFrameContract frame;frame.route=DlssNr::VkNrRoute::Native;
    frame.placement=DlssNr::VkNrPlacement::AfterSR;frame.output={3840,2160};
    DlssNr::VkNrTemporalMetadata metadata;metadata.color=metadata.depth=metadata.motion={0,0,1600,900};
    metadata.featureGeneration=7;metadata.frameToken=11;
    frame.evaluation={reinterpret_cast<VkCommandBuffer>(1),1,11,7};frame.temporal=metadata;
    const auto post=DlssNr::ResolveVkNrWorkload(c,frame,metadata);
    Check(post.width==1600&&post.height==896&&!post.reason,
          "Vulkan Post stage follows observed pre-SR native dimensions rather than SR output");
    metadata.color=metadata.depth=metadata.motion={0,0,1280,720};frame.temporal=metadata;
    const auto dynamic=DlssNr::ResolveVkNrWorkload(c,frame,metadata);
    Check(dynamic.width==1280&&dynamic.height==720&&!dynamic.reason,
          "Vulkan Post follows current dynamic native size");
    Check(DlssNr::ResolveVkNrWorkload(c,frame,{}).reason!=nullptr,
          "missing current native dimensions never silently become Full output");
    Ini ini;U::SaveHints(ini,c);
    Check(ini.values.contains("NativeResolution")&&ini.values["NativeResolution"]=="1",
          "Post automatic-native identity persists separately from legacy selection keys");
    U::SelectUnifiedResolution(c,2);
    Check(!c.DlssNrRunBeforeSr.value_or_default()&&U::Manual(c)&&c.DlssNrNativeResolution.value_or_default()==0,
          "Manual remains Post and clears automatic-native selection without losing its scale");
    c.DlssNrRoute=2u;const auto old=DlssNr::PresentResolution::Selected(c);
    U::SelectUnifiedResolution(c,4);const auto unchanged=DlssNr::PresentResolution::Selected(c);
    Check(old.mode==unchanged.mode&&old.scale==unchanged.scale,
          "Native-only Post option cannot modify Present settings");
    c.DlssNrRoute=0u;
    U::SelectUnifiedResolution(c,2);U::SelectUnifiedResolutionScale(c,.625f);
    U::SelectUnifiedResolution(c,4);U::SelectUnifiedResolution(c,2);
    Check(c.DlssNrWorkingScale.value_or_default()==.625f,"Manual scale survives automatic Post roundtrip");
    U::SelectUnifiedResolution(c,4);
    Ini saved;U::SaveHints(saved,c);Settings loaded;loaded.DlssNrRoute=0u;
    N::LoadSelection(loaded,static_cast<unsigned>(std::stoul(saved.values.at("NativeResolution"))));
    NrConfigState::SetRoutingMode(loaded.DlssNrRenderingMode,loaded.DlssNrRunBeforeSr,0);
    loaded.DlssNrWorkingScale=1.f;loaded.DlssNrUiManualResolution=false;
    Check(U::ResolutionChoiceSelection(loaded)==4&&!N::SelectionRefusal(loaded),"actual load helper preserves saved Post identity");
    NrConfigSnapshot<Settings> snap(loaded);loaded.DlssNrNativeResolution=0u;
    Check(snap.DlssNrNativeResolution.value_or_default()==1,"owned snapshot retains immutable Post selection");
    Settings future;future.DlssNrRoute=0u;N::LoadSelection(future,99u);
    Ini unknown;U::SaveHints(unknown,future);
    Check(N::SelectionRefusal(future)&&U::ResolutionChoiceSelection(future)==-1&&unknown.values.at("NativeResolution")=="99",
          "unknown saved preference survives read save and refuses without normalization");
    U::SelectUnifiedResolution(future,1);
    Check(!N::SelectionRefusal(future)&&future.DlssNrRunBeforeSr.value_or_default(),"explicit Pre choice replaces unknown saved preference");
    U::SelectUnifiedResolution(loaded,1);
    N::LoadSelection(loaded,std::nullopt);
    Check(N::Selection(loaded)==0&&U::ResolutionChoiceSelection(loaded)==1,"legacy missing key retains Pre identity");
    U::SelectUnifiedResolution(c,4);c.DlssNrRunBeforeSr=true;
    Check(N::SelectionRefusal(c)!=nullptr,"inconsistent saved Post placement safely refuses");
    U::SelectUnifiedResolution(c,4);
    auto observed=N::ObserveRender(1600,900,true,2048,1024,32,24,41,0,0);
    auto actual=N::MatchObservedSize(&observed,41,3840,2160);
    Check(actual.width==1600&&actual.height==896&&!actual.reason,"D3D12 current pre-SR observation selects input raster with offsets");
    Check(N::MatchObservedSize(&observed,42,3840,2160).reason!=nullptr,"changed output identity cannot consume old pre-SR observation");
    Check(N::MatchObservedSize(nullptr,41,3840,2160).reason!=nullptr,"missing observation refuses without cached dimensions");
    for(auto invalid:{N::ObserveRender(1600,900,false,2048,1024,0,0,41,0,0),
                     N::ObserveRender(1600,900,true,1024,1024,0,0,41,0,0),
                     N::ObserveRender(1600,900,true,2048,1024,500,0,41,0,0),
                     N::ObserveRender(1600,900,true,2048,1024,0,0,41,1,0)})
        Check(N::MatchObservedSize(&invalid,41,3840,2160).reason!=nullptr,"missing or out-of-bounds native subrect refuses");
    metadata.color=metadata.depth=metadata.motion={0,0,1280,720};frame.temporal=metadata;
    auto stale=metadata;stale.frameToken++;
    Check(DlssNr::ResolveVkNrWorkload(c,frame,stale).reason!=nullptr,"Vulkan prior or different invocation metadata refuses");
    stale=metadata;stale.featureGeneration++;
    Check(DlssNr::ResolveVkNrWorkload(c,frame,stale).reason!=nullptr,"Vulkan different feature generation refuses");
    auto missing=frame;missing.evaluation={};
    Check(DlssNr::ResolveVkNrWorkload(c,missing,metadata).reason!=nullptr,"Vulkan missing evaluation identity refuses");
    frame.placement=DlssNr::VkNrPlacement::BeforeSR;
    Check(DlssNr::ResolveVkNrWorkload(c,frame,metadata).reason!=nullptr,"Post cannot silently run at Before stage");
    frame.placement=DlssNr::VkNrPlacement::AfterRR;
    Check(!DlssNr::ResolveVkNrWorkload(c,frame,metadata).reason,"Post uses native input raster after RR");
    c.DlssNrMultipassEnabled=true;auto profile=c.DlssNrBasicMultipass.value_or_default();
    profile.advanced=false;profile.resolution=.5f;c.DlssNrBasicMultipass=profile;
    auto derived=TryNrConfigSnapshot(c);
    Check(derived&&derived->DlssNrWorkingScale.value_or_default()==.5f&&
          derived->DlssNrNativeResolution.value_or_default()==0&&c.DlssNrNativeResolution.value_or_default()==1,
          "Basic owns effective resolution while saved automatic Post remains intact");
    c.DlssNrMultipassEnabled=false;U::SelectUnifiedResolution(c,1);
    Check(N::MatchAfterReconstruction(c,true,false)&&!N::MatchAfterReconstruction(c,false,false),
          "legacy Pre retains existing after-RR exception only");
    U::SelectUnifiedResolution(c,0);
    Check(!N::PostSelected(c)&&!N::MatchAfterReconstruction(c,false,false)&&!c.DlssNrRunBeforeSr.value_or_default(),
          "Full output retains After stage and no native-size override");
    auto supersampled=N::ObserveRender(2560,1440,true,2560,1440,0,0,41,0,0);
    auto larger=N::MatchObservedSize(&supersampled,41,1920,1080);
    Check(larger.width==2560&&larger.height==1440&&!larger.reason,
          "Post accepts authentic input larger than SR output through existing resampler");
    Check(N::MatchPostWorkSize(8193,1440,1920,1080).reason!=nullptr,
          "Post preserves actual private resource size limit");
    U::SelectUnifiedResolution(c,4);c.DlssNrNativeProtocol=true;
    Check(!N::SelectionRefusal(c),"typed path never refuses only because automatic Post is selected");
    Check(!N::PreparedRasterRefusal(N::MatchPostWorkSize(1920,1080,1920,1080),1920,1080),
          "typed prepared full-size resources accept authentic equal native input raster");
    Check(N::PreparedRasterRefusal(N::MatchPostWorkSize(1280,720,1920,1080),1920,1080)!=nullptr,
          "typed existing full-size capability refuses actual private raster mismatch");
    Check(N::PreparedRasterRefusal({0,0,"missing current observation"},1920,1080)!=nullptr,
          "typed prepared resources cannot bypass missing current observation");
    c.DlssNrNativeProtocol=false;
    std::printf("RESULT checks=%u failures=%u CPU_ONLY=true\n",checks,failures);return failures?1:0;
}
