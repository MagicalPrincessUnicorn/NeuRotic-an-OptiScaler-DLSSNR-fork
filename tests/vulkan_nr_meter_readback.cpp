#include "../OptiScaler/dlssnr/VulkanNrExposureScan.h"
#include "../OptiScaler/dlssnr/VulkanNrReservations.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace DlssNr;
static constexpr uint32_t kMeterSide=kDlssNrMeterGrid;
template<class... Args> static void MeterLog(const char*,const Args&...) {}
#define LOG_INFO MeterLog
struct State {
 std::array<void*,VkNrReservationPool::HardCap> meterMapped{};
 std::array<bool,VkNrReservationPool::HardCap> meterPublished{},meterGrid{},meterAutomatic{},meterScan{};
 std::array<VkNrUseId,VkNrReservationPool::HardCap> meterUses{};
 std::array<VkNrFrameContract,VkNrReservationPool::HardCap> meterContexts{};
 VkNrAutomaticWhitePoint automaticWhitePoint;
 VulkanNrExposureScan scan;
 uint64_t automaticReadbacks=0,gameExposureSequence=0;
 uint32_t sceneMeterLogCount=0;
 float gameExposure=0;
 explicit State(VkNrRecordingOwner& owner):scan(owner){}
};
static State* active;
static State& ActiveVk(){return *active;}
static void Publish(VkNrRecordingOwner& recordingOwner) {
 for(uint32_t slot=0;slot<VkNrReservationPool::HardCap;++slot) {
#include "meter_readback_actual.inc"
 }
}
static void Check(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
int main(){
 VkNrRecordingOwner owner;State state(owner);active=&state;
 VkNrReservationPool meter(owner),composition(owner);
 auto device=reinterpret_cast<VkDevice>(1);auto command=reinterpret_cast<VkCommandBuffer>(2);
 auto pool=reinterpret_cast<VkCommandPool>(3);auto queue=reinterpret_cast<VkQueue>(4);
 owner.OnAllocate(device,pool,command,0);
 VkNrFrameContract context;context.deviceGeneration=1;context.route=VkNrRoute::Native;
 context.output={3840,2160};context.work={1920,1080};context.routeEpoch=3;
 state.automaticWhitePoint.Configure(context);
 // Returned scene: all tiles positive, P90 about 60, a few dozen extreme peaks.
 std::array<float,kMeterSide*kMeterSide> tiles;tiles.fill(12);
 std::fill(tiles.begin()+3000,tiles.end(),60.f);
 std::fill_n(tiles.begin(),40,6000.f);
 for(unsigned frame=0;frame<8;++frame){
  owner.OnBegin(command,0,VK_SUCCESS);
  const auto use=*owner.Reserve(command,1);
  auto shader=composition.Reserve(use,1),reservation=meter.Reserve(use,1);
  Check(shader&&reservation,"actual reservation owners retain use");
  const auto slot=reservation->slots[0];
  state.meterMapped[slot]=tiles.data();state.meterPublished[slot]=false;
  state.meterAutomatic[slot]=state.meterGrid[slot]=true;
  state.meterUses[slot]=use;state.meterContexts[slot]=context;
  Publish(owner);Check(state.automaticReadbacks==frame,"unsubmitted mapped memory is not published");
  const auto submission=*owner.PrepareSubmission({queue,1,{use}});
  owner.SubmissionReturned(submission,VK_SUCCESS);owner.CheckpointReturned(submission,VK_SUCCESS);
  Publish(owner);Check(state.automaticReadbacks==frame,"pending exact use is not published");
  owner.ObserveFence(submission,VK_SUCCESS);
  Publish(owner);Check(state.automaticReadbacks==frame,"completion alone does not exclude replay");
  owner.OnReset(command,VK_SUCCESS);composition.ReleaseCompleted(owner);
  Check(owner.GpuComplete(use),"meter retains use when shader observer is released");
  Publish(owner);Check(state.automaticReadbacks==frame+1,"actual production block publishes released complete use");
  Publish(owner);Check(state.automaticReadbacks==frame+1,"completed readback publishes once");
  meter.ReleaseCompleted(owner);
 }
 Check(state.automaticWhitePoint.Value()==60.f,"production readback normalizes a stable scene despite sparse extreme highlights");
 ExposureGuard::WhitePointHold hold;
 Check(ResolveVkNrWhitePoint(1,1,0,1,1,{},hold,state.automaticWhitePoint.Value())==60,"production-derived value reaches encode white point resolver");
 auto changed=context;changed.settingsRevision=1;
 state.automaticWhitePoint.Configure(changed);
 Check(!state.automaticWhitePoint.ObserveCompleted(context,100,tiles)&&
  std::strcmp(state.automaticWhitePoint.LastObservation().outcome,"context-mismatch")==0,"late context is named and refused");
 tiles.fill(24);tiles[0]=16000;
 for(uint64_t i=101;i<=108;++i)state.automaticWhitePoint.ObserveCompleted(changed,i,tiles);
 const auto& outlier=state.automaticWhitePoint.LastObservation();
 Check(state.automaticWhitePoint.Value()==24.f&&std::strcmp(outlier.outcome,"accepted")==0&&outlier.percentile==24&&outlier.peak==16000&&outlier.lit==tiles.size(),
  "single extreme tile cannot veto the supported percentile");
 state.automaticWhitePoint.Reset();state.automaticWhitePoint.Configure(changed);
 tiles.fill(0);std::fill_n(tiles.begin(),600,60.f);
 for(uint64_t i=1;i<=8;++i)state.automaticWhitePoint.ObserveCompleted(changed,i,tiles);
 Check(!state.automaticWhitePoint.Value()&&std::strcmp(state.automaticWhitePoint.LastObservation().outcome,"highlight-dominated")==0,
  "sparse lights covering less than one fifth of black frame remain rejected");
 tiles.fill(.001f);std::fill_n(tiles.begin(),600,60.f);
 for(uint64_t i=9;i<=16;++i)state.automaticWhitePoint.ObserveCompleted(changed,i,tiles);
 Check(!state.automaticWhitePoint.Value()&&std::strcmp(state.automaticWhitePoint.LastObservation().outcome,"highlight-dominated")==0,
  "positive near-black floor does not qualify sparse lights");
 std::puts("PASS extracted production readback: exact completion, robust scene scale with extreme highlights, sparse-scene rejection and context outcomes");
}
