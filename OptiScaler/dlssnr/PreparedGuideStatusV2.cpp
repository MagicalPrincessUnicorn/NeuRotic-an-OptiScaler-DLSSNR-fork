#define NR_STATUS_V2_IMPLEMENTATION
#include "PreparedGuideStatusV2.h"
#include "connections/ConnectionPolicy.h"
#include <cstring>
#include <mutex>
namespace DlssNr::PreparedGuides {
namespace { std::mutex mutex; StatusV2 latest; bool published=false; }
SnapshotV2 ValidateStatusV2(StatusV2 s,uint64_t now) noexcept {
 SnapshotV2 result;
 if(s.size!=sizeof(s)||s.version!=2||uint32_t(s.selectedSource)>4||uint32_t(s.effectiveTransport)>2||uint32_t(s.stage)>6||
    uint32_t(s.depthOrigin)>4||uint32_t(s.motionOrigin)>4||uint32_t(s.cameraOrigin)>4||uint32_t(s.timingDomain)>2||
    s.creationReady>1||s.guideReady>1||s.modelPreparing>1||s.outputValid>1||s.restartRequired>1||s.displayObserved>1||
    s.depthDirection < -1||s.depthDirection>1||s.candidateConfidence>100)return result;
 s.reason[159]=0;for(auto& c:s.reason){if(!c)break;if(static_cast<unsigned char>(c)<32||static_cast<unsigned char>(c)>126)c=' ';}
 result.available=true;result.fresh=s.updatedTickMs&&now>=s.updatedTickMs&&now-s.updatedTickMs<=3000;
 if(!result.fresh){s.creationReady=s.guideReady=s.modelPreparing=s.outputValid=s.displayObserved=0;s.stage=Stage::Waiting;}
 if(!s.session||!s.capture){s.guideReady=s.outputValid=s.displayObserved=0;}
 result.status=s;return result;
}
Status ProjectV1(const StatusV2& s) noexcept {
 Status v;v.session=s.session;v.capture=s.capture;v.updatedTickMs=s.updatedTickMs;v.inputFrames=s.inputFrames;
 v.modelCompletions=s.modelCompletions;v.copybackCompletions=s.copybackCompletions;v.sourceApi=s.sourceApi;
 v.depthReady=s.guideReady&&s.depthOrigin!=Origin::Unknown;v.motionReady=s.guideReady&&s.motionOrigin!=Origin::Unknown;
 v.active=s.creationReady&&s.session;v.stage=s.stage;std::memcpy(v.reason,s.reason,sizeof(v.reason));v.reason[159]=0;return v;
}
void PublishStatusV2(const StatusV2& s) noexcept {if(!ValidateStatusV2(s,s.updatedTickMs).available)return;std::lock_guard lock(mutex);latest=s;published=true;}
SnapshotV2 QueryStatusV2(uint64_t now) noexcept {std::lock_guard lock(mutex);return published?ValidateStatusV2(latest,now):SnapshotV2{};}
}
extern "C" unsigned NeuRotic_QueryPreparedGuidesV2(void* output,uint32_t bytes) {
 if(!output||bytes!=sizeof(DlssNr::PreparedGuides::StatusV2))return 0;
#if defined(_WIN32)
 const auto snapshot=DlssNr::PreparedGuides::QueryStatusV2(GetTickCount64());
#else
 const auto snapshot=DlssNr::PreparedGuides::QueryStatusV2(0);
#endif
 if(!snapshot.available)return 0;std::memcpy(output,&snapshot.status,sizeof(snapshot.status));return 1;
}

extern "C" unsigned NeuRotic_PublishPreparedGuidesV2(const void* input,uint32_t bytes) {
 using namespace DlssNr;
 if(!input||bytes!=sizeof(PreparedGuides::StatusV2))return 0;
 PreparedGuides::StatusV2 s;std::memcpy(&s,input,sizeof(s));
 Connections::PolicyWire policy;
 if(!NeuRotic_QueryConnectionPolicyV1(&policy,sizeof(policy))||policy.claimedSource!=uint32_t(s.selectedSource))return 0;
 if(!PreparedGuides::ValidateStatusV2(s,s.updatedTickMs).available)return 0;
 PreparedGuides::PublishStatusV2(s);return 1;
}
