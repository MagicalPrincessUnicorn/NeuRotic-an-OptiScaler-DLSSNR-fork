#include "../OptiScaler/dlssnr/VulkanNrCapture.h"
#include "../OptiScaler/dlssnr/VulkanNrPerformanceCapture.h"
#include "../OptiScaler/dlssnr/VulkanNrPixelProbe.h"
#include "fixtures/vulkan_nr_test_params.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool v,const char* s){if(!v){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
struct Pixels : VkNrCapturePayload { bool visible=false;unsigned* files;explicit Pixels(unsigned& count):files(&count){bytes=256;}
 bool HostVisible() const override{return visible;}bool Publish(const VkNrCaptureRequest& frame) override{Check(frame.use.value&&frame.frame.representation.recipeRevision==7,"retained original recipe/use");*files+=2;return true;}
};
int main(){
 using namespace PixelProbe;
 const uint16_t half[]={0x3c00,0xbc00,0x0001,0x0000};
 const auto pixel=Decode(VK_FORMAT_R16G16B16A16_SFLOAT,half);
 Check(pixel&&(*pixel)[0]==1.f&&(*pixel)[1]==-1.f&&(*pixel)[2]==std::ldexp(1.f,-24)&&(*pixel)[3]==0.f,"half normal, signed, subnormal and zero alpha decode");
 const uint16_t exceptional[]={0x7c00,0xfc00,0x7e00,0x3c00};const auto invalid=Decode(VK_FORMAT_R16G16B16A16_SFLOAT,exceptional);
 Check(invalid&&std::isinf((*invalid)[0])&&std::isinf((*invalid)[1])&&std::isnan((*invalid)[2]),"half exceptional values are preserved for finite counts");
 const unsigned char bgra[]={255,128,0,64};const auto packed=Decode(VK_FORMAT_B8G8R8A8_UNORM,bgra);
 Check(packed&&(*packed)[0]==0&&(*packed)[2]==1&&std::abs((*packed)[3]-64.f/255.f)<1e-7f,"BGRA storage channels decode correctly");
 Check(!Decode(VK_FORMAT_UNDEFINED,bgra)&&!Decode(VK_FORMAT_R32G32B32A32_SFLOAT,nullptr),"unsupported format and null bytes refused");
 const float floats[]={-12.5f,1000.f,0.25f,1.f};const auto full=Decode(VK_FORMAT_R32G32B32A32_SFLOAT,floats);
 Check(full&&(*full)[0]==-12.5f&&(*full)[1]==1000.f&&(*full)[3]==1.f,"float storage is neither clamped nor tone mapped");
 const Pixel a{1,2,3,0},b{1.25f,1.5f,3,1};Summary summary;summary.Add(a);summary.Add(b);summary.Add(*invalid);
 Check(summary.finiteRgb==6&&summary.finiteAlpha==3&&summary.alphaMin==0&&summary.alphaMax==1,"summary excludes nonfinite RGB and retains alpha zero/one");
 Difference delta;delta.Add(a,b);delta.Add(a,*invalid);
 Check(delta.finiteChannels==3&&delta.maximum==.5&&delta.Mean()==.25,"delta uses independently authored RGB differences and excludes nonfinite pairs");
 for(uint32_t size:{1u,2u,1270u,2160u})for(uint32_t i=0;i<4;++i)Check(Coordinate(size,i)<size,"normalized-grid coordinates remain inside small and odd rasters");
 Check(Coordinate(3840,0)==480&&Coordinate(1920,0)==240&&Coordinate(2160,3)==1890,"grid uses matching normalized centers for full/reduced rasters");
 Budget budget;Check(!budget.Try(0,true)&&budget.Try(0,false)&&!budget.Try(999,false)&&budget.Try(1000,false),"one pending probe and allocation-failure backoff");
 for(uint32_t i=2;i<Budget::MaximumAttempts;++i)Check(budget.Try(uint64_t(i)*1000,false),"bounded attempts admit spaced probes");
 Check(!budget.Try(100000,false),"process-wide attempt cap holds");
 VkNrRecordingOwner owner;auto d=reinterpret_cast<VkDevice>(1),q=reinterpret_cast<VkDevice>(2);auto cb=reinterpret_cast<VkCommandBuffer>(3);
 owner.OnAllocate(d,reinterpret_cast<VkCommandPool>(4),cb,0);owner.OnBegin(cb,0,VK_SUCCESS);auto use=*owner.Reserve(cb,1);
 StrictParams params;std::array<unsigned,12> signature{};
 for(unsigned i=0;i<6;++i)params.Set(VkNrPerformanceCreationKeys[i],i+1);
 Check(ReadVkNrPerformanceSignature(&params,signature),"complete actual SR creation signature qualifies private comparison");
 params.Set("Color",reinterpret_cast<void*>(41));params.Set("Output",reinterpret_cast<void*>(42));params.Set("Reset",0);
 unsigned evaluated=0;
 Check(Screenshots::FreshSrPair<NVSDK_NGX_Parameter,void>(&params,reinterpret_cast<void*>(43),reinterpret_cast<void*>(44),
   reinterpret_cast<void*>(45),reinterpret_cast<void*>(46),[&]{void* color=nullptr;int reset=0;params.Get("Color",&color);params.Get("Reset",&reset);
   Check(color==reinterpret_cast<void*>(uintptr_t(43+evaluated))&&reset==1,"each private evaluation receives distinct matched color and reset history");++evaluated;return true;}),"private Performance pair completes");
 void* restored=nullptr;params.Get("Color",&restored);Check(evaluated==2&&restored==reinterpret_cast<void*>(41),"live feature parameters restored after private pair");
 unsigned files=0,releases=0;Pixels* pixels=nullptr;
 VulkanNrCapture capture(owner,[&](const VkNrCaptureRequest&,VkImage,VkImage,uint64_t){auto p=std::make_unique<Pixels>(files);pixels=p.get();return VkNrCaptureCreation{std::move(p),true,{}};},[&](VkNrCapturePayload&,bool alive){Check(alive,"live release");++releases;return true;});
 VkNrCaptureRequest request;request.use=use;request.frame.routeEpoch=1;request.frame.representation.recipeRevision=7;request.requestedPasses=request.completedPasses=4;
 capture.Arm(1);Check(capture.Record(request,reinterpret_cast<VkImage>(11),reinterpret_cast<VkImage>(12)),"comparison pair same use");
 capture.PublishCompleted(owner);Check(files==0,"recorded is not completed or scanout");
 capture.RevokeNewRequests(2);auto submission=*owner.PrepareSubmission({reinterpret_cast<VkQueue>(q),1,{use}});
 owner.SubmissionReturned(submission,VK_SUCCESS);owner.CheckpointReturned(submission,VK_SUCCESS);owner.ObserveFence(submission,VK_SUCCESS);
 capture.PublishCompleted(owner);Check(files==0,"completion does not establish host visibility");pixels->visible=true;
 capture.PublishCompleted(owner);Check(files==0&&releases==0,"replayable recordings cannot race host file publication");
 owner.OnReset(cb,VK_SUCCESS);capture.PublishCompleted(owner);Check(files==2&&releases==1,"original recipe publishes and readbacks retire after recording ends");
 capture.Arm(2);request.frame.routeEpoch=2;request.completedPasses=3;Check(!capture.Record(request,reinterpret_cast<VkImage>(11),reinterpret_cast<VkImage>(12)),"partial chains never matched comparisons");
 DlssNrConstants c{};PopulateVkNrComparisonConstants(2,.3f,1.5f,true,c);
 Check(c.CompareMode==2&&c.CompareSplit==.3f&&c.CompareZoom==1.5f&&c.CompareSwap==1,"wipe zoom swap controls effective");
 VulkanNrCapture failure(owner,[](const auto&,VkImage,VkImage,uint64_t){return VkNrCaptureCreation{{},false,"readback allocation failed"};},[](auto&,bool){return true;});
 owner.OnBegin(cb,0,VK_SUCCESS);request.use=*owner.Reserve(cb,1);request.completedPasses=4;failure.Arm(2);
 Check(!failure.Record(request,reinterpret_cast<VkImage>(11),reinterpret_cast<VkImage>(12)),"failed readback is diagnostic failure independent from rendering");
 Check(!failure.Busy()&&failure.PrivateBytes()==0,"failed allocation leaves no armed request or private bytes");
 VulkanNrCapture throwing(owner,[](const auto&,VkImage,VkImage,uint64_t)->VkNrCaptureCreation{throw std::bad_alloc();},[](auto&,bool){return true;});
 throwing.Arm(2);Check(!throwing.Record(request,reinterpret_cast<VkImage>(11),reinterpret_cast<VkImage>(12))&&!throwing.Busy(),"creator allocation exception remains a diagnostic refusal");
 unsigned abandoned=0,published=0;
 VulkanNrCapture lost(owner,[&](const auto&,VkImage,VkImage,uint64_t){return VkNrCaptureCreation{std::make_unique<Pixels>(published),true,{}};},[&](auto&,bool alive){Check(!alive,"lost-device probe abandons without driver release");++abandoned;return true;});
 lost.Arm(2);Check(lost.Record(request,reinterpret_cast<VkImage>(11),reinterpret_cast<VkImage>(12)),"pending probe owned before submission");
 VkNrRecordingOwner unrelated;lost.PublishCompleted(unrelated);Check(!published&&lost.PrivateBytes()==256,"unrelated owner cannot publish pending probe");
 lost.AbandonDevice();Check(abandoned==1&&!published&&!lost.Busy()&&lost.PrivateBytes()==0,"unsubmitted lost-device probe never publishes and clears owned bytes");
 std::puts("matched capture identity, exact completion/visibility, route changes and final comparison controls verified");}
