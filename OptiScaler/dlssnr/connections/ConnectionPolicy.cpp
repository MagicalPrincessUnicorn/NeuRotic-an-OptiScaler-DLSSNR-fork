#define NR_CONNECTION_IMPLEMENTATION
#include "ConnectionPolicy.h"
#include "ConnectionSelection.h"
#include "../NativeGuideRoute.h"
#include "../FinalFallbackControl.h"
#include <mutex>
#include <cstring>
namespace {std::mutex policyMutex;uint32_t claimed=0;uint64_t claimedToken=0;bool unsafe=false,nativeUsable=false;thread_local bool presentHandled=false;
DlssNr::Connections::InputSelectionSnapshot selection;}
namespace {DlssNr::Connections::Mixed::PolicyStore signalPolicy;
DlssNr::Connections::SignalStatusWireV2 signalStatus,signalUsed;uint64_t signalStatusTime=0,signalUsedTime=0;
DlssNr::Connections::SignalStatusWireV2 completedSignals;uint64_t completedTime=0,completedEpoch=0,completedInputEpoch=0;
std::atomic<uint64_t> signalEpoch{1};std::atomic<bool> signalReceiptsClosed{false};uint64_t signalAdmissionEpoch=0,signalInputEpoch=0,signalUsedEpoch=0;}
namespace DlssNr::Connections {
void InvalidateSignalReceipts() noexcept {
 if(!signalPolicy.Invalidate())signalReceiptsClosed.store(true);
 signalEpoch.fetch_add(1,std::memory_order_seq_cst);
}
bool RequestSignalPreferences(int depth,int motion,bool strict,int color) noexcept {return signalPolicy.Request(depth,motion,strict,color);}
bool RequestSavedSignalPreferences(int depth,int motion,bool strict,int color) noexcept {return signalPolicy.RequestPersisted(depth,motion,strict,color);}
Mixed::Policy SignalPreferences() noexcept {return signalPolicy.Read();}
SignalStatusSnapshot QueryInputSignals(uint64_t now) noexcept {
 std::lock_guard lock(policyMutex);const auto revision=signalPolicy.Read().revision;
 const auto fresh=[&](uint64_t time,uint64_t rev,uint64_t epoch){return !unsafe&&!FinalFallback::inputSwitchPending.load()&&time&&now>=time&&now-time<500&&rev==revision&&epoch==signalEpoch.load()&&signalInputEpoch==FinalFallback::CurrentInputEpoch();};
 return {signalStatus,fresh(signalStatusTime,signalStatus.revision,signalAdmissionEpoch),signalUsed,fresh(signalUsedTime,signalUsed.revision,signalUsedEpoch)};
}
SignalStatusSnapshot QueryCompletedInputSignals(uint64_t now) noexcept {
 std::lock_guard lock(policyMutex);
 const bool fresh=!unsafe&&!signalReceiptsClosed.load()&&!FinalFallback::inputSwitchPending.load()&&
  completedTime&&now>=completedTime&&now-completedTime<500&&completedSignals.revision==signalPolicy.Read().revision&&
  completedEpoch==signalEpoch.load()&&completedInputEpoch==FinalFallback::CurrentInputEpoch();
 return {completedSignals,fresh,completedSignals,fresh};
}
bool RequestInputSelection(int source,int transport,bool fallback){
 if(source<0||source>4||transport<0||transport>2)return false;
 std::lock_guard lock(policyMutex);if(selection.serial==UINT64_MAX)return false;
 InvalidateSignalReceipts();
 ++selection.serial;selection.requestedSource=source;selection.requestedTransport=transport;
 selection.allowCpuFallback=fallback;selection.reason.clear();
 selection.phase=(source==NativeGuides::SelectedSource()&&transport==NativeGuides::SelectedTransport())?InputPhase::Applied:InputPhase::Waiting;
 if(selection.phase==InputPhase::Applied)NativeGuides::ConfigureCpuFallback(fallback);
 return true;
}
InputSelectionSnapshot QueryInputSelection(){
 std::lock_guard lock(policyMutex);auto out=selection;
 out.appliedSource=NativeGuides::SelectedSource();out.appliedTransport=NativeGuides::SelectedTransport();
 if(!out.serial){out.requestedSource=out.appliedSource;out.requestedTransport=out.appliedTransport;out.allowCpuFallback=NativeGuides::SelectedCpuFallback();}
 return out;
}
bool BeginInputSelection(uint64_t serial){
 std::lock_guard lock(policyMutex);
 if(!serial||serial!=selection.serial||selection.phase!=InputPhase::Waiting||unsafe)return false;
 FinalFallback::inputSwitchPending.store(true,std::memory_order_seq_cst);
 FinalFallback::inputInterruptionEpoch.store(serial,std::memory_order_seq_cst);
 selection.phase=InputPhase::Switching;selection.reason="Waiting for NR GPU work to finish";return true;
}
bool CommitInputSelection(uint64_t serial, bool (*advanceSession)() noexcept){
 std::lock_guard lock(policyMutex);
 if(serial!=selection.serial||selection.phase!=InputPhase::Switching||unsafe||claimed>=2||
    !FinalFallback::inputSwitchPending.load(std::memory_order_seq_cst)||FinalFallback::callbacks.load(std::memory_order_seq_cst))return false;
 if(advanceSession&&!advanceSession())return false;
 NativeGuides::ApplyInputSelection(selection.requestedSource,selection.requestedTransport,selection.serial);
 NativeGuides::ConfigureCpuFallback(selection.allowCpuFallback);
 claimed=0;claimedToken=0;selection.phase=InputPhase::Applied;selection.reason.clear();return true;
}
void RefuseInputSelection(uint64_t serial,InputPhase phase,const std::string& reason){
 std::lock_guard lock(policyMutex);if(serial!=selection.serial)return;
 selection.phase=phase;selection.reason=reason;
}
void EndInputSelection() noexcept {FinalFallback::inputSwitchPending.store(false,std::memory_order_seq_cst);}
bool PreparedPresentHandled() noexcept {return presentHandled;}
PresentArbitrationScope::PresentArbitrationScope() noexcept : previous(presentHandled) {presentHandled=false;}
PresentArbitrationScope::~PresentArbitrationScope() {presentHandled=previous;}
void SetNativeUsable(bool usable) noexcept {std::lock_guard lock(policyMutex);nativeUsable=usable;}
bool NativeUsable() noexcept {std::lock_guard lock(policyMutex);return nativeUsable;}
bool PreparedOwnerClaimed() noexcept {std::lock_guard lock(policyMutex);return claimed>=2;}
bool ReserveNativeOwner(uint64_t token) noexcept {
 std::lock_guard lock(policyMutex);const auto requested=NativeGuides::SelectedSource();
 if(!token||unsafe||FinalFallback::inputSwitchPending.load()||(requested!=0&&requested!=1)||(claimed&&(claimed!=1||claimedToken!=token)))return false;
 claimed=1;claimedToken=token;return true;
}
}
NR_CONNECTION_EXPORT NeuRotic_QueryConnectionPolicyV1(void* out,uint32_t bytes) {
 using namespace DlssNr; if(!out||bytes!=sizeof(Connections::PolicyWire))return 0;
 std::lock_guard lock(policyMutex);Connections::PolicyWire p;
 p.source=NativeGuides::SelectedSource();p.transport=NativeGuides::SelectedTransport();
 p.allowCpuFallback=NativeGuides::SelectedCpuFallback()?1:0;p.claimedSource=claimed;p.unsafe=unsafe?1:0;p.nativeUsable=nativeUsable?1:0;
 std::memcpy(out,&p,sizeof(p));return 1;
}
NR_CONNECTION_EXPORT NeuRotic_ClaimPreparedConnectionV1(uint32_t source,uint32_t transport,uint64_t ownerToken) {
 using namespace DlssNr;std::lock_guard lock(policyMutex);
 if(!ownerToken||source<2||source>4||transport<1||transport>2||unsafe||FinalFallback::inputSwitchPending.load())return 0;
 const auto requested=NativeGuides::SelectedSource();
 if(requested==1||(requested!=0&&uint32_t(requested)!=source))return 0;
 const auto policyTransport=NativeGuides::SelectedTransport();
 if((policyTransport!=0&&uint32_t(policyTransport)!=transport)||(policyTransport==0&&transport==2&&!NativeGuides::SelectedCpuFallback()))return 0;
 if(claimed&&(claimed!=source||claimedToken!=ownerToken))return 0;
 // The existing owner must finish before a newly ready source can take over.
 if(!claimed){
  const Connections::RequestedPolicy policy{static_cast<Connections::Source>(requested),static_cast<Connections::Transport>(policyTransport),NativeGuides::SelectedCpuFallback()};
  const Connections::RouteCapability routes[]{
   {Connections::Source::Native,nativeUsable,nativeUsable,nativeUsable,nativeUsable,false,nativeUsable,true},
   {static_cast<Connections::Source>(source),true,true,true,transport==1,transport==2,true,true}};
  const auto choice=Connections::Decide(policy,routes,{});
  if(!choice.usable||static_cast<uint32_t>(choice.source)!=source||static_cast<uint32_t>(choice.transport)!=transport)return 0;
 }
 claimed=source;claimedToken=ownerToken;if(source==2)presentHandled=true;return 1;
}
NR_CONNECTION_EXPORT NeuRotic_RetirePreparedConnectionV1(uint32_t source,uint64_t ownerToken,uint32_t clean) {
 std::lock_guard lock(policyMutex);if(!ownerToken||!claimed||claimed!=source||claimedToken!=ownerToken||clean>1)return 0;
 signalStatusTime=signalUsedTime=0;
 if(!clean){unsafe=true;return 0;}
 if(unsafe)return 0;claimed=0;claimedToken=0;return 1;
}
NR_CONNECTION_EXPORT NeuRotic_QuerySignalPolicyV1(void* output,uint32_t bytes) {
 using namespace DlssNr::Connections;if(!output||bytes!=sizeof(SignalPolicyWire))return 0;
 const auto p=signalPolicy.Read();SignalPolicyWire wire;wire.depth=uint32_t(p.depth);wire.motion=uint32_t(p.motion);
 // Soft None is an unmatched preference; only explicit enforcement pins it.
 wire.strict=p.strict;wire.revision=p.revision;
 // Older readers cannot honor a color preference. Refuse their packet rather
 // than silently ignore Native/None/strict intent.
 if(p.color!=Source::Automatic){wire.depth=7;wire.strict=1;}
 std::memcpy(output,&wire,sizeof(wire));return 1;
}
NR_CONNECTION_EXPORT NeuRotic_QuerySignalPolicyV2(void* output,uint32_t bytes) {
 using namespace DlssNr::Connections;if(!output||bytes!=sizeof(SignalPolicyWireV2))return 0;
 const auto p=signalPolicy.Read();SignalPolicyWireV2 wire;
 wire.color=uint32_t(p.color);wire.depth=uint32_t(p.depth);wire.motion=uint32_t(p.motion);
 wire.strict=p.strict;wire.revision=p.revision;std::memcpy(output,&wire,sizeof(wire));return 1;
}
static unsigned PublishSignals(uint32_t source,uint64_t token,const DlssNr::Connections::SignalStatusWireV2& wire,bool legacy) {
 using namespace DlssNr::Connections;
 if(wire.reserved||wire.reserved2||wire.color>4||wire.depth>4||wire.motion>4||wire.consumed>1||
    wire.colorOrigin>6||wire.depthOrigin>6||wire.motionOrigin>6||
    (wire.consumed&&(!wire.depth||!wire.motion||(!legacy&&!wire.color)))||
    wire.colorReason>uint32_t(Mixed::Reason::IntentionallyDisabled)||
    wire.depthReason>uint32_t(Mixed::Reason::IntentionallyDisabled)||wire.motionReason>uint32_t(Mixed::Reason::IntentionallyDisabled)||!wire.capture||!wire.generation)return 0;
 std::lock_guard lock(policyMutex);if(unsafe||claimed!=source||claimedToken!=token||!token)return 0;
 const auto policy=signalPolicy.Read();
 if(legacy&&policy.color!=Source::Automatic)return 0;
 const auto epoch=signalEpoch.load();const auto inputEpoch=DlssNr::FinalFallback::CurrentInputEpoch();
 if(signalReceiptsClosed.load()||wire.revision!=policy.revision||DlssNr::FinalFallback::inputSwitchPending.load())return 0;
 if(wire.consumed&&(signalAdmissionEpoch!=epoch||signalInputEpoch!=inputEpoch||
    signalStatus.consumed||wire.capture!=signalStatus.capture||wire.generation!=signalStatus.generation||
    wire.revision!=signalStatus.revision||wire.color!=signalStatus.color||wire.depth!=signalStatus.depth||wire.motion!=signalStatus.motion||
    wire.colorOrigin!=signalStatus.colorOrigin||wire.depthOrigin!=signalStatus.depthOrigin||wire.motionOrigin!=signalStatus.motionOrigin))return 0;
 if(!wire.consumed){signalAdmissionEpoch=epoch;signalInputEpoch=inputEpoch;}
 signalStatus=wire;signalStatusTime=GetTickCount64();
 if(wire.consumed){signalUsed=wire;signalUsedTime=signalStatusTime;signalUsedEpoch=epoch;
  completedSignals=wire;completedTime=signalStatusTime;completedEpoch=epoch;completedInputEpoch=inputEpoch;}
 return 1;
}
NR_CONNECTION_EXPORT NeuRotic_PublishInputSignalsV1(uint32_t source,uint64_t token,const void* input,uint32_t bytes) {
 using namespace DlssNr::Connections;if(!input||bytes!=sizeof(SignalStatusWire))return 0;
 SignalStatusWire wire;std::memcpy(&wire,input,sizeof(wire));
 if(wire.size!=sizeof(wire)||wire.version!=1||wire.reserved||wire.depth>4||wire.motion>4||wire.consumed>1||wire.depthOrigin>6||wire.motionOrigin>6||
    (wire.consumed&&(!wire.depth||!wire.motion))||
    wire.depthReason>uint32_t(Mixed::Reason::IntentionallyDisabled)||wire.motionReason>uint32_t(Mixed::Reason::IntentionallyDisabled)||!wire.capture||!wire.generation)return 0;
 SignalStatusWireV2 extended;static_cast<SignalStatusWire&>(extended)=wire;
 extended.size=sizeof(extended);extended.version=2;
 return PublishSignals(source,token,extended,true);
}
NR_CONNECTION_EXPORT NeuRotic_PublishInputSignalsV2(uint32_t source,uint64_t token,const void* input,uint32_t bytes) {
 using namespace DlssNr::Connections;if(!input||bytes!=sizeof(SignalStatusWireV2))return 0;
 SignalStatusWireV2 wire;std::memcpy(&wire,input,sizeof(wire));
 if(wire.size!=sizeof(wire)||wire.version!=2)return 0;
 return PublishSignals(source,token,wire,false);
}
