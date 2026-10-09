#pragma once
#include "PreparedGuideStatus.h"
#include "connections/ConnectionSelection.h"
namespace DlssNr::PreparedGuides {
enum class Origin : uint32_t { Unknown, Native, Observed, Derived, External };
enum class ClockDomain : uint32_t { Unknown, CpuQpc, GpuTimestamp };
struct alignas(8) StatusV2 {
 uint32_t size=sizeof(StatusV2),version=2;
 uint64_t session=0,capture=0,updatedTickMs=0,producerIdentity=0,candidateId=0,generation=0;
 uint64_t captureAgeMs=0,inputFrames=0,modelCompletions=0,copybackCompletions=0;
 uint64_t decodeAllocations=0,decodeReuses=0,pairAllocations=0,pairReuses=0,liveBytes=0;
 uint32_t sourceApi=0,producerBits=0;
 Connections::Source selectedSource=Connections::Source::Automatic;
 Connections::Transport effectiveTransport=Connections::Transport::Automatic;
 Origin depthOrigin=Origin::Unknown,motionOrigin=Origin::Unknown,cameraOrigin=Origin::Unknown;
 uint32_t creationReady=0,guideReady=0,modelPreparing=0,outputValid=0,restartRequired=0,displayObserved=0;
 uint32_t captureWidth=0,captureHeight=0,workWidth=0,workHeight=0,outputWidth=0,outputHeight=0;
 int32_t depthDirection=-1;
 uint32_t candidateConfidence=0,flowProvider=0,modelProvider=0;
 ClockDomain timingDomain=ClockDomain::Unknown;
 uint64_t timingInterval=0,captureNs=0,normalizeNs=0,modelNs=0,copybackNs=0;
 Stage stage=Stage::Idle;
 uint32_t reserved=0;
 char reason[160]{};
};
static_assert(std::is_standard_layout_v<StatusV2> && std::is_trivially_copyable_v<StatusV2>);
static_assert(sizeof(StatusV2)==432 && offsetof(StatusV2,reason)==272);
struct SnapshotV2 { StatusV2 status; bool available=false,fresh=false; };
SnapshotV2 ValidateStatusV2(StatusV2,uint64_t nowMs) noexcept;
Status ProjectV1(const StatusV2&) noexcept;
// Actual selected owner publishes facts; the aggregate owns no GPU lifetimes.
void PublishStatusV2(const StatusV2&) noexcept;
SnapshotV2 QueryStatusV2(uint64_t nowMs) noexcept;
}
#if defined(_WIN32) && defined(NR_STATUS_V2_IMPLEMENTATION)
extern "C" __declspec(dllexport) unsigned __cdecl NeuRotic_QueryPreparedGuidesV2(void*,uint32_t bytes);
#else
extern "C" unsigned NeuRotic_QueryPreparedGuidesV2(void*,uint32_t bytes);
#endif

#if defined(_WIN32) && defined(NR_STATUS_V2_IMPLEMENTATION)
extern "C" __declspec(dllexport) unsigned __cdecl NeuRotic_PublishPreparedGuidesV2(const void*,uint32_t bytes);
#else
extern "C" unsigned NeuRotic_PublishPreparedGuidesV2(const void*,uint32_t bytes);
#endif
