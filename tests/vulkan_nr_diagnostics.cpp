#include "../OptiScaler/dlssnr/VulkanNrExposureScan.h"
#include "fixtures/vulkan_nr_pass_settings.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool v,const char* s){if(!v){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(){
 Check(ParseVkNrAnchors("0.0000001:8;100000:8").empty(),"donor exposure scan floor and ceiling preserved");
 VkNrRecordingOwner owner;auto device=reinterpret_cast<VkDevice>(1);auto cb=reinterpret_cast<VkCommandBuffer>(2);
 owner.OnAllocate(device,reinterpret_cast<VkCommandPool>(3),cb,0);owner.OnBegin(cb,0,VK_SUCCESS);
 auto use=*owner.Reserve(cb,1);VkFrameRequest frame;frame.use=use;frame.contract.deviceGeneration=1;
 frame.contract.routeEpoch=1;frame.contract.output={64,64};frame.contract.work={64,64};
 VulkanNrExposureScan scan(owner);scan.Configure(frame.contract,"2:8;8:2",false,1);
 Check(scan.Record(frame,use),"scan captures immutable frame and exact use");
 Check(!scan.CompletedWhitePoint(owner),"anchored_scan_waits_exact_use");
 Check(!scan.Readback(use,4,false),"host visibility required independently of completion");
 Check(scan.Readback(use,4,true),"host visible measurement attached to exact use");
 auto submission=*owner.PrepareSubmission({reinterpret_cast<VkQueue>(4),1,{use}});
 owner.SubmissionReturned(submission,VK_SUCCESS);owner.CheckpointReturned(submission,VK_SUCCESS);
 Check(!scan.CompletedWhitePoint(owner),"submission alone is not completion");owner.ObserveFence(submission,VK_SUCCESS);
 Check(std::abs(*scan.CompletedWhitePoint(owner)-4)<1e-4f,"donor log interpolation");
 auto changed=frame.contract;changed.routeEpoch=2;scan.Configure(changed,"2:8",false,1);
 Check(!scan.CompletedWhitePoint(owner),"late readback cannot change another generation");
 auto retuned=frame.contract;retuned.settingsRevision=1;
 Check(!SameVkNrDiagnosticContext(frame.contract,retuned),"old tuning readback cannot qualify current diagnostics or GPU timing");
 ExposureGuard::WhitePointHold hold;
 Check(ResolveVkNrWhitePoint(0,32,2,8,1,4.f,hold)==32,"manual source");
 Check(ResolveVkNrWhitePoint(1,32,2,8,1,4.f,hold)==4,"game source");
 Check(ResolveVkNrWhitePoint(2,32,2,8,1,6.f,hold)==6,"scan source distinct");
 Check(ResolveVkNrWhitePoint(1,32,0,8,1,6.f,hold)==4,"invalid game exposure retains supported value");
 ExposureGuard::WhitePointHold noExposure;
 Check(ResolveVkNrWhitePoint(1,1,0,1,1,{},noExposure,16.f)==16,"missing game exposure uses derived scene scale");
 Check(ResolveVkNrWhitePoint(0,3,0,1,1,{},noExposure,16.f)==3,"automatic scale never changes manual source");
 Check(ResolveVkNrWhitePoint(2,3,0,1,1,6.f,noExposure,16.f)==6,"automatic scale never changes anchored source");
 Check(ResolveVkNrWhitePoint(1,32,0,1,1,{},noExposure,16.f)==32,"automatic scale preserves manual normalization floor");
 Check(ResolveVkNrWhitePoint(1,1,0,1,2,{},noExposure,16.f)==32,"automatic scale honors exposure trim");
 Check(ResolveVkNrWhitePoint(1,1,2,8,1,{},noExposure,16.f)==4,"authentic exposure wins over scene estimate");
 Check(ResolveVkNrWhitePoint(1,1,0,8,1,{},noExposure,16.f)==4,"held authentic exposure wins over scene estimate");
 VkNrAutomaticWhitePoint automatic;automatic.Configure(frame.contract);
 std::vector<float> grid(kDlssNrMeterGrid*kDlssNrMeterGrid,16.f);
 for(uint64_t i=1;i<8;++i) Check(!automatic.ObserveCompleted(frame.contract,i,grid),"automatic scale waits for stable evidence");
 Check(!automatic.Value(),"unstable startup has no derived provenance");
 Check(automatic.ObserveCompleted(frame.contract,8,grid)&&*automatic.Value()==16,"stable bright grid qualifies scene scale");
 grid.assign(grid.size(),32.f);
 Check(!automatic.ObserveCompleted(frame.contract,8,grid)&&*automatic.Value()==16,"duplicate completion cannot adapt scale");
 Check(!automatic.ObserveCompleted(changed,9,grid)&&*automatic.Value()==16,"foreign generation cannot adapt scale");
 for(uint64_t i=9;i<16;++i)automatic.ObserveCompleted(frame.contract,i,grid);
 Check(*automatic.Value()==16,"sudden scene change holds until stable");
 Check(automatic.ObserveCompleted(frame.contract,16,grid)&&*automatic.Value()>16&&*automatic.Value()<=16*std::exp2(1.f/60.f)+1e-5f,"adaptation capped to one sixtieth stop per completed sample");
 auto prior=*automatic.Value();grid.assign(grid.size(),0.f);
 Check(!automatic.ObserveCompleted(frame.contract,17,grid)&&*automatic.Value()==prior,"dark grid holds prior normalization");
 grid[0]=16.f;
 Check(!automatic.ObserveCompleted(frame.contract,18,grid),"isolated highlight cannot determine scene scale");
 grid.assign(grid.size(),16.f);grid[0]=std::numeric_limits<float>::infinity();
 Check(!automatic.ObserveCompleted(frame.contract,19,grid),"nonfinite readback rejected");
 grid.assign(grid.size(),5000.f);
 Check(!automatic.ObserveCompleted(frame.contract,20,grid),"out of domain readback rejected rather than clipped");
 Check(!automatic.ObserveCompleted(frame.contract,21,{}),"empty readback rejected");
 automatic.Configure(changed);Check(!automatic.Value(),"context change clears derived normalization");
 grid.assign(grid.size(),16.f);grid[0]=64.f;
 for(uint64_t i=1;i<=8;++i)automatic.ObserveCompleted(changed,i,grid);
 Check(automatic.Value()&&*automatic.Value()==16,"percentile resists an isolated highlight and sequence restarts in new context");
 automatic.Reset();Check(!automatic.Value(),"reset clears derived provenance");
 VkNrHeldFrame held;NVSDK_NGX_Resource_VK image{};frame.color=&image;frame.contract.temporal=VkNrTemporalMetadata{};
 frame.contract.temporal->frameToken=5;
 Check(held.Capture(frame,12),"hold preserves color and metadata");auto frozen=held.Select(frame.contract);
 Check(frozen&&frozen->color==&image&&frozen->contract.temporal->frameToken==5&&held.WhitePoint()==12,"hold preserves immutable matching metadata");
 Check(!held.Select(changed),"held route revokes new uses");auto resized=frame.contract;resized.output.width=128;
 Check(!held.Select(resized),"held resize revokes new uses");
 struct Settings : VkPassTestSettings { VkPassValue<float> DlssNrWhitePointScale{1}; }; Settings cfg;cfg.DlssNrDebugView=3u;cfg.DlssNrWhitePointScale=16.f;DlssNrConstants constants{};
 PopulateVkNrDiagnosticConstants(cfg,frame.contract,constants);
 Check(constants.DebugView==3&&constants.DebugScale==16,"proxy/model/difference controls effective");
 for(unsigned i=1;i<=3;++i){cfg.DlssNrDebugView=i;PopulateVkNrDiagnosticConstants(cfg,frame.contract,constants);Check(constants.DebugView==i,"all diagnostic modes");}
 std::puts("anchored scan exact completion, exposure sources, immutable hold and diagnostic controls verified");
}
