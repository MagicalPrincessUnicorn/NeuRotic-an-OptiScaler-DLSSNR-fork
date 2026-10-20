// Production adapter exercised with fake functions only; never LoadLibrary/provider.
#define NR_FORWARDER_CONTRACT_TEST
#include <dlssnr/forwarder/dlssnr_forwarder.cpp>
#include <shaders/dlssnr/DlssNr_Common.h>
#include <dlssnr/DlssNr_PresentHistory.h>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <atomic>
#include <array>
#include <vector>
#include <cmath>
namespace {
std::map<std::string,unsigned long long> resources;
std::map<std::string,unsigned> uints;
std::map<std::string,float> floats;
struct Observation {
 const void* feature=nullptr;
 std::map<std::string,unsigned long long> resources;
 std::map<std::string,unsigned> uints;
 std::map<std::string,float> floats;
};
std::vector<Observation> observations;
bool captureCalls=false;
// Mutates only the fake provider's recorded observation, never production code.
bool negativeStaleJitter=false;
bool mutateThisObservation=false;
void __thiscall PutResource(void*,const char* k,unsigned long long v){resources[k]=v;}
void __thiscall PutUInt(void*,const char* k,unsigned v){uints[k]=v;}
void __thiscall PutFloat(void*,const char* k,float v){floats[k]=v;}
int __cdecl Init(unsigned long long,const wchar_t*,ID3D12Device*,int,const void*){return 1;}
int __cdecl Create(ID3D12GraphicsCommandList*,int,const void*,void**){return 1;}
int __cdecl Shutdown(){return 1;}
int releases=0;int releaseResult=1;bool throwRelease=false,sehRelease=false;
int __cdecl Release(void*){++releases;if(sehRelease)RaiseException(0xE0000042u,0,0,nullptr);if(throwRelease)throw 1;return releaseResult;}
std::atomic<int> active=0,maxActive=0,evals=0;
int evaluateResult=1;bool throwEvaluate=false;
int __cdecl Evaluate(ID3D12GraphicsCommandList*,const void* feature,const void*,void*){
 const auto now=++active;if(now>maxActive)maxActive=now;
 // Production's serialized borrow covers all setters and this snapshot.
 if(captureCalls){
  observations.push_back({feature,resources,uints,floats});
  if(mutateThisObservation)observations.back().floats["Jitter.Offset.X"]=.25f;
 }
 std::this_thread::yield();++evals;--active;if(throwEvaluate)throw 2;return evaluateResult;
}
struct Entry {int enter=0,returned=0;bool allowed=true,success=false;};
bool Enter(void* p)noexcept{auto& e=*static_cast<Entry*>(p);++e.enter;return e.allowed;}
void Returned(void* p,bool ok)noexcept{auto& e=*static_cast<Entry*>(p);++e.returned;e.success=ok;}
}
int main(int argc,char** argv){
 if(argc==2&&std::string(argv[1])=="--negative-stale-jitter")negativeStaleJitter=true;
 else if(argc!=1){std::cerr<<"Only --negative-stale-jitter is supported\n";return 2;}
 int checks=0,failed=0;auto check=[&](bool ok,const char* s){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<s<<'\n';}};
#ifdef NR_FEATURE18_HOST_CONTRACT_V1
 void* table[16]{};void** params=table;
 table[VT_SET_ULL]=reinterpret_cast<void*>(&PutResource);table[VT_SET_UINT]=reinterpret_cast<void*>(&PutUInt);
 table[1]=reinterpret_cast<void*>(&PutFloat);g_floatSlot=1;
 g_snip.module=GetModuleHandleW(nullptr);g_snip.init=Init;g_snip.create=Create;g_snip.evaluate=Evaluate;g_snip.release=Release;g_snip.shutdown=Shutdown;g_snip.initialised=true;
 auto* feature=reinterpret_cast<void*>(42);unsigned origins[4]{};
 auto invoke=[&](Entry& entry,unsigned width){DlssNr::ProviderEntryCallbacks callbacks{&entry,Enter,Returned};
  return dlssnr_call_evaluate_guides_v4(nullptr,feature,&params,nullptr,nullptr,nullptr,nullptr,width,32,32,32,32,32,
   0,0,1,0,1,1,1,0,1,1,0,0,origins,&callbacks);};
 g_contractTestIdentity=false;
 check(!canonicalBeforeLoad(L"CONTROLLED_FAKE_MODULE"),"test create refuses unobserved sentinel");
 check(dlssnr_call_host_recorded_contract_v1()==nullptr,"unobserved provider has no adapter contract");
 Entry denied;check(invoke(denied,32)==0&&denied.enter==0&&evals==0,"identity refusal before entry");
 g_contractTestIdentity=true;
 check(canonicalBeforeLoad(L"CONTROLLED_FAKE_MODULE"),"test create admits only controlled executable sentinel");
 check(!canonicalBeforeLoad(L"nvngx_dlssnr.dll"),"test create cannot open a provider path");
 check(std::string_view(dlssnr_call_host_recorded_contract_v1())==DlssNr::Feature18HostContractIdentity,"observed selected DLL reports generic host policy");
 resources["DLSSNR.UI"]=123;resources["DLSSNR.UIAlpha"]=124;resources["DLSSNR.Backbuffer"]=125;
 Entry entry;check(invoke(entry,64)==1&&entry.enter==1&&entry.returned==1&&entry.success,"one entry and return around actual fake call");
 check(resources["DLSSNR.UI"]==0&&resources["DLSSNR.UIAlpha"]==0&&resources["DLSSNR.Backbuffer"]==0,"matched native route clears absent extras within call");
 check(uints["DLSSNR.Width"]==64,"per-invocation map rebound");
 Entry blocked;blocked.allowed=false;check(invoke(blocked,99)==0&&uints["DLSSNR.Width"]==64&&blocked.returned==0,"refused entry cannot mutate parameters");
 Entry a,b;std::thread t1([&]{for(int i=0;i<40;++i)invoke(a,32);});
 std::thread t2([&]{for(int i=0;i<40;++i)invoke(b,32);});t1.join();t2.join();
 check(evals==81&&maxActive==1,"parameter mutation and calls serialized");
 evaluateResult=static_cast<int>(0xBAD00003u);Entry failedEntry;
 check(invoke(failedEntry,32)==evaluateResult&&failedEntry.returned==1&&!failedEntry.success,"post-entry native error is not evaluation success");
 throwEvaluate=true;Entry exceptionalEntry;
 check(invoke(exceptionalEntry,32)==0&&exceptionalEntry.returned==1&&!exceptionalEntry.success,"exception closes CPU borrow without crossing the C ABI or success claim");
 throwEvaluate=false;evaluateResult=1;
 DlssNr::FeatureReleaseResult result;
 check(dlssnr_call_release_checked_v1(feature,&result)==1&&result.nativeResult==1&&releases==1,"new checked ABI retains native success");
 releaseResult=static_cast<int>(0xBAD00002u);
 check(dlssnr_call_release_checked_v1(feature,&result)==1&&result.nativeResult==releaseResult,"checked ABI retains exact native failure");
 throwRelease=true;
 check(dlssnr_call_release_checked_v1(feature,&result)==1&&result.disposition==DlssNr::FeatureReleaseDisposition::Exceptional,"checked ABI catches exceptional release");
 throwRelease=false;sehRelease=true;
 check(dlssnr_call_release_checked_v1(feature,&result)==1&&result.disposition==DlssNr::FeatureReleaseDisposition::Exceptional&&result.exceptionCode==0xE0000042u,"checked release retains exact SEH code");
 sehRelease=false;result.size=0;auto countBefore=releases;
 check(dlssnr_call_release_checked_v1(feature,&result)==0&&releases==countBefore,"incompatible result ABI refuses before provider entry");
 auto oldReleases=releases;dlssnr_call_release(feature);
 check(releases==oldReleases+1,"legacy void export remains callable with old signature");

 // Six-frame wire-value witness. These are real production exports and temporal
 // helpers; opaque resources are never dereferenced. CPU epochs below are authored
 // fixture metadata, NOT GPU contents, allocation generations or provider history.
 // Removing/reordering a production setter, sharing stale parameter values, losing
 // an origin, or double-scaling the declared conversion must fail these assertions.
 enum class Route { PreLegacy, PostFullV4, PresentNativeFullV4 };
 struct Frame {
  float jitterX,jitterY,mvX,mvY;
  bool sourceReset;
  unsigned epoch;
 };
 const std::array<Frame,6> frames{{
  {.25f,-.375f,24.f,-8.f,true,1},
  {-.25f,.375f,12.f,-4.f,false,1},
  {.375f,.125f,6.f,-2.f,false,1},
  {-.375f,-.125f,3.f,-1.f,false,1},
  {.125f,-.25f,48.f,-16.f,true,2},
  {-.125f,.25f,24.f,-8.f,false,2}
 }};
 // Literal independent oracle: Present retains the 12x4 motion texture's scale
 // and maps 24x8 jitter to the 48x16 output. No helper derives expected values.
 const std::array<std::array<float,4>,6> presentWanted{{
  {{24.f,-8.f,.5f,-.75f}},{{12.f,-4.f,-.5f,.75f}},
  {{6.f,-2.f,.75f,.25f}},{{3.f,-1.f,-.75f,-.25f}},
  {{48.f,-16.f,.25f,-.5f}},{{24.f,-8.f,-.25f,.5f}}
 }};
 const std::array<unsigned,6> nativeResets{{1,0,0,0,1,0}};
 const std::array<unsigned,6> presentResets{{1,0,0,1,1,0}};
 struct Token {unsigned frame,role,epoch;};
 std::array<std::array<Token,4>,6> tokens{};
 std::map<unsigned long long,Token> identities;
 for(unsigned f=0;f<6;++f)for(unsigned role=0;role<4;++role){
  tokens[f][role]={f,role,frames[f].epoch};
  identities.emplace(reinterpret_cast<unsigned long long>(&tokens[f][role]),tokens[f][role]);
 }
 const std::array<const char*,4> resourceKeys{{"DLSSNR.Color","DLSSNR.Depth","DLSSNR.MVec","DLSSNR.Output"}};
 int sequenceChecks=0,sequenceFailures=0;
 const auto seqCheck=[&](bool ok,const char* name,unsigned route,unsigned frameIndex){
  ++sequenceChecks;++checks;
  if(!ok){++failed;++sequenceFailures;std::cerr<<"FAIL sequence route="<<route<<" frame="<<frameIndex<<" "<<name<<'\n';}
 };
 captureCalls=true;
 for(const Route route:{Route::PreLegacy,Route::PostFullV4,Route::PresentNativeFullV4}){
  const bool pre=route==Route::PreLegacy,present=route==Route::PresentNativeFullV4;
  const unsigned routeNumber=static_cast<unsigned>(route);
  // Real Continuity helper, driven by authored events; not the production Present
  // callback or renderer admission path. Source reset is independently ORed below.
  DlssNr::PresentHistory::Continuity continuity;
  std::array<unsigned,2> sessionTokens{{routeNumber*10+1,routeNumber*10+2}};
  for(unsigned f=0;f<6;++f){
   const auto& input=frames[f];
   // Simulate upstream overwrites of the shared parameter map between calls.
   for(auto& v:resources)v.second=0xdead;
   for(auto& v:uints)v.second=999;
   for(auto& v:floats)v.second=-99.f;
   DlssNrFrameInfo temporal{};
   temporal.RenderSubrectWidth=24;temporal.RenderSubrectHeight=8;
   temporal.DepthSubrectWidth=24;temporal.DepthSubrectHeight=8;
   temporal.MotionSubrectWidth=pre?24u:12u;temporal.MotionSubrectHeight=pre?8u:4u;
   temporal.DepthSubrectX=2;temporal.DepthSubrectY=3;
   temporal.MotionSubrectX=4;temporal.MotionSubrectY=5;
   temporal.MvScaleX=input.mvX;temporal.MvScaleY=input.mvY;
   temporal.JitterX=input.jitterX;temporal.JitterY=input.jitterY;
   temporal.Reset=input.sourceReset;
   if(present)temporal=DlssNrProjectPresentGuides(temporal,48,16,continuity.ResetForNextEvaluation());
   const unsigned width=pre?24u:48u,height=pre?8u:16u;
   const auto converted=DlssNrModelTemporal(
    {{temporal.MvScaleX,temporal.MvScaleY},{temporal.JitterX,temporal.JitterY}},
    temporal.PresentNativeGuides,DlssNrWorkingMotionScale(width,height,width,height),width,height,width,height);
   auto* session=&sessionTokens[input.epoch-1];
   const auto resource=[&](unsigned role){return reinterpret_cast<ID3D12Resource*>(&tokens[f][role]);};
   Entry current;DlssNr::ProviderEntryCallbacks callbacks{&current,Enter,Returned};
   const unsigned guideOrigins[4]={2,3,4,5};
   mutateThisObservation=negativeStaleJitter&&route==Route::PostFullV4&&f==1;
   const auto before=observations.size();
   const int returned=pre?
    dlssnr_call_evaluate(nullptr,session,&params,resource(0),resource(1),resource(2),resource(3),
     width,height,24,8,0,temporal.Reset?1:0,1,0,1,1,1,0,
     converted.values.motionScale.x,converted.values.motionScale.y,converted.values.jitter.x,converted.values.jitter.y):
    dlssnr_call_evaluate_guides_v4(nullptr,session,&params,resource(0),resource(1),resource(2),resource(3),
     width,height,24,8,12,4,0,temporal.Reset?1:0,1,0,1,1,1,0,
     converted.values.motionScale.x,converted.values.motionScale.y,converted.values.jitter.x,converted.values.jitter.y,
     guideOrigins,&callbacks);
   seqCheck(returned==1&&observations.size()==before+1,"one successful provider entry",routeNumber,f);
   if(observations.size()!=before+1)continue;
   const auto& got=observations.back();
   const auto gotU=[&](const char* key){const auto it=got.uints.find(key);return it==got.uints.end()?999u:it->second;};
   const auto gotF=[&](const char* key){const auto it=got.floats.find(key);return it==got.floats.end()?-99.f:it->second;};
   seqCheck(got.feature==session,"opaque caller session preserved",routeNumber,f);
   seqCheck(pre||(current.enter==1&&current.returned==1&&current.success),"V4 entry/return observed",routeNumber,f);
   for(unsigned role=0;role<4;++role){
    const auto it=got.resources.find(resourceKeys[role]);
    const auto id=it==got.resources.end()?0:it->second;
    const auto known=identities.find(id);
    seqCheck(id==reinterpret_cast<unsigned long long>(resource(role))&&known!=identities.end()&&
     known->second.frame==f&&known->second.role==role&&known->second.epoch==input.epoch,
     resourceKeys[role],routeNumber,f);
   }
   seqCheck(gotU("DLSSNR.Reset")== (present?presentResets[f]:nativeResets[f]),"submitted reset cadence",routeNumber,f);
   seqCheck(gotU("DLSSNR.Width")==width&&gotU("DLSSNR.Height")==height&&
    gotU("DLSSNR.ColorSubrectWidth")==width&&gotU("DLSSNR.ColorSubrectHeight")==height&&
    gotU("DLSSNR.OutputSubrectWidth")==width&&gotU("DLSSNR.OutputSubrectHeight")==height,
    "color/output extents rebound",routeNumber,f);
   seqCheck(gotU("DLSSNR.DepthSubrectWidth")==24&&gotU("DLSSNR.DepthSubrectHeight")==8&&
    gotU("DLSSNR.MVecSubrectWidth")== (pre?24u:12u)&&gotU("DLSSNR.MVecSubrectHeight")== (pre?8u:4u),
    "independent active guide extents",routeNumber,f);
   seqCheck(gotU("DLSSNR.DepthSubrectBaseX")== (pre?0u:2u)&&gotU("DLSSNR.DepthSubrectBaseY")== (pre?0u:3u)&&
    gotU("DLSSNR.MVecSubrectBaseX")== (pre?0u:4u)&&gotU("DLSSNR.MVecSubrectBaseY")== (pre?0u:5u),
    "legacy zero or explicit guide origins",routeNumber,f);
   const std::array<float,4> wanted=present?presentWanted[f]:
    std::array<float,4>{{input.mvX,input.mvY,input.jitterX,input.jitterY}};
   const std::array<const char*,4> keys{{"DLSSNR.MVecScaleX","DLSSNR.MVecScaleY","Jitter.Offset.X","Jitter.Offset.Y"}};
   for(unsigned k=0;k<4;++k)seqCheck(std::abs(gotF(keys[k])-wanted[k])<.000001f,keys[k],routeNumber,f);
   std::cout<<"FRAME route="<<routeNumber<<" source="<<f<<" authored_epoch="<<input.epoch
    <<" reset="<<gotU("DLSSNR.Reset")<<" mv="<<gotF(keys[0])<<','<<gotF(keys[1])
    <<" jitter="<<gotF(keys[2])<<','<<gotF(keys[3])<<" resource_scope=cpu-sentinels\n";
   if(present){if(f==2)continuity.Invalidate("authored original Present failure");else continuity.CompleteOutputPresent();}
  }
 }
 mutateThisObservation=false;
 const auto savedResources=resources;const auto savedUints=uints;const auto savedFloats=floats;
 const auto observedBefore=observations.size();const int callsBefore=evals.load();
 Entry deniedSequence;deniedSequence.allowed=false;
 const int deniedResult=invoke(deniedSequence,97);
 check(deniedResult==0&&deniedSequence.enter==1&&deniedSequence.returned==0&&
  resources==savedResources&&uints==savedUints&&floats==savedFloats&&
  observations.size()==observedBefore&&evals==callsBefore,
  "denied actual V4 entry preserves all parameter maps and provider count");
 check(observations.size()==18,"all three six-frame routes reached actual fake-provider boundary");
 captureCalls=false;
 std::cout<<"SIXFRAME evaluations="<<observations.size()<<" checks="<<sequenceChecks<<" failures="<<sequenceFailures
  <<" negative="<<(negativeStaleJitter?"stale-jitter":"none")
  <<" scope=actual-forwarder-plus-temporal-helpers; no-renderer-GPU-private-model\n";
#else
 check(false,"approved adapter v4 and checked release missing");
#endif
 std::cout<<"Feature18 forwarder: "<<checks<<" checks, "<<failed<<" failures; fake provider CPU only\n";return failed?1:0;
}
