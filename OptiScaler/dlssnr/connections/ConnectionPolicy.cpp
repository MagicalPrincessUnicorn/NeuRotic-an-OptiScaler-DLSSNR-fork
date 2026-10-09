#define NR_CONNECTION_IMPLEMENTATION
#include "ConnectionPolicy.h"
#include "ConnectionSelection.h"
#include "../NativeGuideRoute.h"
#include "../FinalFallbackControl.h"
#include <mutex>
#include <cstring>
namespace {std::mutex policyMutex;uint32_t claimed=0;uint64_t claimedToken=0;bool unsafe=false,nativeUsable=false;thread_local bool presentHandled=false;
DlssNr::Connections::InputSelectionSnapshot selection;}
namespace DlssNr::Connections {
bool RequestInputSelection(int source,int transport,bool fallback){
 if(source<0||source>4||transport<0||transport>2)return false;
 std::lock_guard lock(policyMutex);if(selection.serial==UINT64_MAX)return false;
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
 if(!clean){unsafe=true;return 0;}
 if(unsafe)return 0;claimed=0;claimedToken=0;return 1;
}
