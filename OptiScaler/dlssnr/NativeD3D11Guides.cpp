#include "NativeD3D11Guides.h"
#include "NativeGuideRoute.h"
#include "NativeGuideProcessor.h"
#include "PreparedGuideStatusV2.h"
#include "FinalFallbackControl.h"
#include "connections/ConnectionPolicy.h"
#include "connections/D3D11Connection.h"
#include "connections/GpuBudget.h"
#include <cstring>

#include <d3d11_4.h>
#include <detours/detours.h>
#include <wrl/client.h>
#include <array>
#include <map>
#include <mutex>
#include <algorithm>
#include <atomic>
namespace DlssNr::NativeD3D11Guides {
namespace {
using Microsoft::WRL::ComPtr;
std::recursive_mutex mutex;
thread_local bool injecting=false;
struct ApiScope {
 ComPtr<ID3D11Multithread> multi;
 explicit ApiScope(ID3D11DeviceContext* c){if(c&&SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&multi))))multi->Enter();}
 ~ApiScope(){if(multi)multi->Leave();}
};
struct Guard {bool prior=injecting;Guard(){injecting=true;}~Guard(){injecting=prior;}};
uint64_t snapshotBytes=0;constexpr uint64_t SnapshotBudget=128ull*1024*1024;
#ifdef NR_D3D11_OBSERVER_TEST
ID3D11DeviceContext* refusedCompletionContext=nullptr;
bool failNextHookCommit=false;
void (*observerAdmissionHook)()=nullptr;
void TestObserverAdmission(){if(auto hook=observerAdmissionHook){observerAdmissionHook=nullptr;hook();}}
#endif
LONG CommitHookTransaction(){
#ifdef NR_D3D11_OBSERVER_TEST
 if(failNextHookCommit){failNextHookCommit=false;DetourTransactionAbort();return ERROR_INVALID_OPERATION;}
#endif
 return DetourTransactionCommit();
}
struct SnapshotLease {
 std::shared_ptr<Connections::GpuBudget::Reservation> budget;
 ComPtr<ID3D11Texture2D> texture;
 uint64_t bytes=0;
 bool completed=false;
 ~SnapshotLease(){snapshotBytes-=bytes;}
};
struct Candidate {
 ComPtr<ID3D11Texture2D> depth;
 std::shared_ptr<SnapshotLease> snapshot;
 uint64_t draws=0,savedDraws=0;int direction=-1,savedDirection=-1;
};
struct Context {ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11DepthStencilView> bound;std::array<Candidate,2> candidates;bool overflow=false;};
struct Recording {ComPtr<ID3D11CommandList> list;std::array<Candidate,2> candidates;bool overflow=false;};
std::map<ID3D11DeviceContext*,Context> contexts;
std::map<ID3D11CommandList*,Recording> lists;
#ifdef NR_D3D11_OBSERVER_TEST
int processor=0;
#else
NativeGuides::Processor processor;
#endif
#ifndef NR_D3D11_OBSERVER_TEST
std::unique_ptr<Connections::D3D11Connection> connection;
#endif
ComPtr<ID3D11Device> selectedDevice;
ComPtr<IDXGISwapChain> selectedSwapchain;
uint64_t capture=0,generation=1;unsigned lastWidth=0,lastHeight=0;
ComPtr<ID3D11Texture2D> historyDepth;
int historyDirection=-1;
bool historyValid=false;
uint64_t historyCapture=0,inputEpoch=0;
bool SourceReset(ID3D11Texture2D* depth,unsigned width,unsigned height,int direction){
 const auto epoch=FinalFallback::CurrentInputEpoch();
 const bool changed=inputEpoch!=epoch||historyDepth.Get()!=depth||lastWidth!=width||lastHeight!=height||historyDirection!=direction;
 inputEpoch=epoch;
 if(changed){++generation;historyDepth=depth;lastWidth=width;lastHeight=height;historyDirection=direction;}
 return changed||!historyValid;
}
std::atomic<bool> unsafe{false};
std::atomic<bool> captureActive{false};
#ifdef NR_D3D11_OBSERVER_TEST
ID3D11DeviceContext* refusedSerializationContext=nullptr;
#endif
HRESULT QuerySerialization(ID3D11DeviceContext* context,ID3D11Multithread** multi){
#ifdef NR_D3D11_OBSERVER_TEST
 if(context==refusedSerializationContext){*multi=nullptr;return E_NOINTERFACE;}
#endif
 return context?context->QueryInterface(IID_PPV_ARGS(multi)):E_POINTER;
}
PreparedGuides::StatusV2 status;
uint64_t Token(){return reinterpret_cast<uint64_t>(&processor);}
bool Enabled(){return !injecting&&!unsafe&&NativeGuides::ObserveBuiltIn();}
void Reason(const char* text){size_t n=0;while(n+1<sizeof(status.reason)&&text[n]){status.reason[n]=text[n];++n;}status.reason[n]=0;status.updatedTickMs=GetTickCount64();PreparedGuides::PublishStatusV2(status);}
Candidate* Find(Context& c,ID3D11DepthStencilView* view) {
 if(!view)return nullptr;D3D11_DEPTH_STENCIL_VIEW_DESC vd{};view->GetDesc(&vd);
 if(vd.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D||vd.Texture2D.MipSlice)return nullptr;
 ComPtr<ID3D11Resource> resource;view->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))return nullptr;
 D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);if(d.ArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1)return nullptr;
 if(d.Format!=DXGI_FORMAT_D16_UNORM&&d.Format!=DXGI_FORMAT_R16_TYPELESS&&d.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT&&d.Format!=DXGI_FORMAT_R24G8_TYPELESS&&d.Format!=DXGI_FORMAT_D32_FLOAT&&d.Format!=DXGI_FORMAT_R32_TYPELESS)return nullptr;
 for(auto& item:c.candidates)if(item.depth==texture)return &item;
 for(auto& item:c.candidates)if(!item.depth){item.depth=texture;return &item;}
 // Rebind only an inactive candidate whose snapshot was proved complete and
 // whose recording/capture aliases have all returned. Never add a third slot.
 for(auto& item:c.candidates)if(!item.draws&&!item.savedDraws&&
    (!item.snapshot||(item.snapshot->completed&&item.snapshot.use_count()==1))){
  item={};item.depth=texture;return &item;
 }
 c.overflow=true;return nullptr;
}
Candidate* Select(Context& state,unsigned width,unsigned height,bool& saved,int& direction){
 if(state.overflow)return nullptr;
 Candidate* best=nullptr;uint64_t score=0;bool tie=false;
 for(auto& candidate:state.candidates){if(!candidate.depth)continue;D3D11_TEXTURE2D_DESC d{};candidate.depth->GetDesc(&d);auto draws=(std::max)(candidate.draws,candidate.savedDraws);if(d.Width!=width||d.Height!=height||!draws)continue;if(draws>score){best=&candidate;score=draws;tie=false;}else if(draws==score)tie=true;}
 if(!best||tie)return nullptr;saved=best->savedDraws>best->draws;
 direction=NativeGuides::depthDirection.load();if(direction<0)direction=saved?best->savedDirection:best->direction;
 return direction<0?nullptr:best;
}
void ResetInterval(Context& state){for(auto& c:state.candidates){c.draws=c.savedDraws=0;c.direction=c.savedDirection=-1;}state.overflow=false;}
bool CompleteInterval(Context& state,bool sourceCompleted=false){
 bool pending=false;for(const auto& item:state.candidates)if(item.snapshot&&!item.snapshot->completed)pending=true;
 if(pending&&!sourceCompleted){
#ifdef NR_D3D11_OBSERVER_TEST
  if(state.context.Get()==refusedCompletionContext)return false;
#endif
  if(state.context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
  Guard guard;ComPtr<ID3D11Device> device;state.context->GetDevice(&device);
  D3D11_QUERY_DESC description{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> query;
  if(FAILED(device->CreateQuery(&description,&query)))return false;
  state.context->End(query.Get());state.context->Flush();const auto deadline=GetTickCount64()+2000;
  HRESULT result=S_FALSE;BOOL done=FALSE;
  do {result=state.context->GetData(query.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(result!=S_FALSE)break;SwitchToThread();}while(GetTickCount64()<deadline);
  if(result!=S_OK||!done)return false;
 }
 for(auto& item:state.candidates){
  if(item.snapshot)item.snapshot->completed=true;
  // Deferred owners retain their snapshot for reuse. Release the completed
  // immediate alias so the deferred owner can retire/rebind it next interval.
  if(item.snapshot&&item.snapshot.use_count()>1)item={};
 }
 ResetInterval(state);return true;
}
Context* Get(ID3D11DeviceContext* c){auto i=contexts.find(c);return i==contexts.end()?nullptr:&i->second;}
Context* PresentContext(ID3D11DeviceContext* c){
 auto* state=Get(c);if(state)return state;
 // Capability absence belongs to this presenting context, not the process's
 // uncertain GPU ownership quarantine. Never claim that a restart repairs it.
 status.creationReady=status.guideReady=status.outputValid=status.modelPreparing=status.restartRequired=status.displayObserved=0;
 status.session=status.producerIdentity=status.capture=status.candidateId=0;status.candidateConfidence=0;
 status.sourceApi=0xb000;status.selectedSource=Connections::Source::BuiltIn;status.stage=PreparedGuides::Stage::Blocked;
 ComPtr<ID3D11Multithread> multi;
 const bool serializable=SUCCEEDED(QuerySerialization(c,&multi))&&multi->GetMultithreadProtected();
 Reason(serializable?"D3D11 presenting context is not registered for built-in capture":"D3D11 presenting context cannot serialize built-in capture; use NR Anything");
 return nullptr;
}
void Bind(ID3D11DeviceContext* c,ID3D11DepthStencilView* v){if(!Enabled())return;std::lock_guard lock(mutex);if(auto* state=Get(c))state->bound=v;}
void Draw(ID3D11DeviceContext* c){if(!Enabled()||!captureActive.load())return;
#ifdef NR_D3D11_OBSERVER_TEST
 TestObserverAdmission();
#endif
 std::lock_guard lock(mutex);
 // The fast admission read may precede a completed handoff on another thread.
 // Serialize the final check with CanYieldOutput before mutating capture state.
 if(!captureActive.load()||!FinalFallback::InGameAllowed())return;
 auto* state=Get(c);if(!state)return;
 ComPtr<ID3D11DepthStencilState> depthState;UINT stencil=0;c->OMGetDepthStencilState(&depthState,&stencil);
 if(depthState){D3D11_DEPTH_STENCIL_DESC d{};depthState->GetDesc(&d);if(!d.DepthEnable||d.DepthWriteMask!=D3D11_DEPTH_WRITE_MASK_ALL)return;}
 if(auto* item=Find(*state,state->bound.Get())){++item->draws;}
}
void Clear(ID3D11DeviceContext* c,ID3D11DepthStencilView* v,UINT flags,FLOAT depth){
 if(!Enabled()||!captureActive.load()||!(flags&D3D11_CLEAR_DEPTH))return;
#ifdef NR_D3D11_OBSERVER_TEST
 TestObserverAdmission();
#endif
 std::lock_guard lock(mutex);
 if(!captureActive.load()||!FinalFallback::InGameAllowed())return;
 auto* state=Get(c);if(!state)return;auto* item=Find(*state,v);if(!item)return;
 // Preserve the rendered scene before UI/next-pass clears destroy it. Deferred
 // contexts record this copy; only ExecuteCommandList makes it current.
 if(item->draws>item->savedDraws){
  if(item->snapshot&&item->snapshot.use_count()>1){state->overflow=true;return;}
  if(!item->snapshot){D3D11_TEXTURE2D_DESC d{};item->depth->GetDesc(&d);
   const uint64_t bytes=(((uint64_t(d.Width)*4+255)&~255ull)*((uint64_t(d.Height)+63)&~63ull)+65535)&~65535ull; // conservative D3D11 row/tile/allocation padding
   if(bytes>SnapshotBudget-snapshotBytes){state->overflow=true;return;}
   d.BindFlags=0;d.MiscFlags=0;d.CPUAccessFlags=0;d.Usage=D3D11_USAGE_DEFAULT;ComPtr<ID3D11Device> device;c->GetDevice(&device);
   auto snapshot=std::make_shared<SnapshotLease>();
   snapshot->budget=Connections::GpuBudget::Reserve(bytes);if(!snapshot->budget){state->overflow=true;return;}
   if(FAILED(device->CreateTexture2D(&d,nullptr,&snapshot->texture))){state->overflow=true;return;}
   snapshot->bytes=bytes;snapshotBytes+=bytes;item->snapshot=std::move(snapshot);
  }
  item->snapshot->completed=false;Guard guard;c->CopyResource(item->snapshot->texture.Get(),item->depth.Get());item->savedDraws=item->draws;item->savedDirection=item->direction;
 }
 item->draws=0;item->direction=depth==0.f?1:depth==1.f?0:-1;
}
using SH=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11DeviceChild*,ID3D11ClassInstance*const*,UINT);
using TO=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,D3D11_PRIMITIVE_TOPOLOGY);
using IL=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11InputLayout*);
using OM=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView*const*,ID3D11DepthStencilView*);
using OMU=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView*const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView*const*,const UINT*);
using D=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
using DI=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using DIN=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
using DN=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
using DA=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using DX=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);
using CD=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11DepthStencilView*,UINT,FLOAT,UINT8);
using EX=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);
using FN=HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,BOOL,ID3D11CommandList**);
using CS=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using DC=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,UINT,ID3D11DeviceContext**);
std::map<std::pair<unsigned,void*>,void*> drawTargets;
void RefreshDrawHooks(ID3D11DeviceContext*);
template<class F> F DrawOriginal(ID3D11DeviceContext*c,unsigned index,F fallback){std::lock_guard lock(mutex);auto key=std::pair{index,(*reinterpret_cast<void***>(c))[index]};auto i=drawTargets.find(key);return i==drawTargets.end()?fallback:reinterpret_cast<F>(i->second);}
SH vsSet=nullptr,psSet=nullptr,gsSet=nullptr,hsSet=nullptr,dsSet=nullptr,dvsSet=nullptr,dpsSet=nullptr,dgsSet=nullptr,dhsSet=nullptr,ddsSet=nullptr;TO topology=nullptr,dtopology=nullptr;IL layout=nullptr,dlayout=nullptr;
void* immediateEntries[115]{};
OM om=nullptr;OMU omu=nullptr;D draw=nullptr;DI indexed=nullptr;DIN indexedInst=nullptr;DN inst=nullptr;DA automatic=nullptr;DX indexedIndirect=nullptr,indirect=nullptr;CD clear=nullptr;EX execute=nullptr;FN finish=nullptr;DC deferred=nullptr;CS clearState=nullptr;
OM dom=nullptr;OMU domu=nullptr;D ddraw=nullptr;DI dindexed=nullptr;DIN dindexedInst=nullptr;DN dinst=nullptr;DA dautomatic=nullptr;DX dindexedIndirect=nullptr,dindirect=nullptr;CD dclear=nullptr;EX dexecute=nullptr;FN dfinish=nullptr;CS dclearState=nullptr;
bool deferredHooked=false;
template<class F> F Original(ID3D11DeviceContext*c,F immediate,F def){return c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED&&def?def:immediate;}
void HookDeferredContext(ID3D11DeviceContext*);
void STDMETHODCALLTYPE HOm(ID3D11DeviceContext*c,UINT n,ID3D11RenderTargetView*const*r,ID3D11DepthStencilView*d){ApiScope api(c);Bind(c,d);DrawOriginal(c,33,om)(c,n,r,d);RefreshDrawHooks(c);}
void STDMETHODCALLTYPE HOmU(ID3D11DeviceContext*c,UINT n,ID3D11RenderTargetView*const*r,ID3D11DepthStencilView*d,UINT start,UINT count,ID3D11UnorderedAccessView*const*u,const UINT*v){ApiScope api(c);if(n!=D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)Bind(c,d);DrawOriginal(c,34,omu)(c,n,r,d,start,count,u,v);RefreshDrawHooks(c);}
void STDMETHODCALLTYPE HD(ID3D11DeviceContext*c,UINT a,UINT b){ApiScope api(c);if(a)Draw(c);DrawOriginal(c,13,draw)(c,a,b);}
void STDMETHODCALLTYPE HDI(ID3D11DeviceContext*c,UINT a,UINT b,INT d){ApiScope api(c);if(a)Draw(c);DrawOriginal(c,12,indexed)(c,a,b,d);}
void STDMETHODCALLTYPE HDIN(ID3D11DeviceContext*c,UINT a,UINT b,UINT d,INT e,UINT f){ApiScope api(c);if(a&&b)Draw(c);DrawOriginal(c,20,indexedInst)(c,a,b,d,e,f);}
void STDMETHODCALLTYPE HDN(ID3D11DeviceContext*c,UINT a,UINT b,UINT d,UINT e){ApiScope api(c);if(a&&b)Draw(c);DrawOriginal(c,21,inst)(c,a,b,d,e);}
void STDMETHODCALLTYPE HDA(ID3D11DeviceContext*c){ApiScope api(c);Draw(c);DrawOriginal(c,38,automatic)(c);}
void STDMETHODCALLTYPE HDX(ID3D11DeviceContext*c,ID3D11Buffer*b,UINT n){ApiScope api(c);Draw(c);DrawOriginal(c,39,indexedIndirect)(c,b,n);}
void STDMETHODCALLTYPE HDX2(ID3D11DeviceContext*c,ID3D11Buffer*b,UINT n){ApiScope api(c);Draw(c);DrawOriginal(c,40,indirect)(c,b,n);}
#define SHADER_HOOK(name,index) void STDMETHODCALLTYPE H##name(ID3D11DeviceContext*c,ID3D11DeviceChild*s,ID3D11ClassInstance*const*i,UINT n){ApiScope api(c);DrawOriginal(c,index,name)(c,s,i,n);RefreshDrawHooks(c);}
SHADER_HOOK(vsSet,11) SHADER_HOOK(psSet,9) SHADER_HOOK(gsSet,23) SHADER_HOOK(hsSet,60) SHADER_HOOK(dsSet,64)
#undef SHADER_HOOK
void STDMETHODCALLTYPE HTopology(ID3D11DeviceContext*c,D3D11_PRIMITIVE_TOPOLOGY t){ApiScope api(c);DrawOriginal(c,24,topology)(c,t);RefreshDrawHooks(c);}
void STDMETHODCALLTYPE HLayout(ID3D11DeviceContext*c,ID3D11InputLayout*l){ApiScope api(c);DrawOriginal(c,17,layout)(c,l);RefreshDrawHooks(c);}
void STDMETHODCALLTYPE HClear(ID3D11DeviceContext*c,ID3D11DepthStencilView*v,UINT f,FLOAT d,UINT8 s){ApiScope api(c);Clear(c,v,f,d);DrawOriginal(c,53,clear)(c,v,f,d,s);RefreshDrawHooks(c);}
HRESULT STDMETHODCALLTYPE HFinish(ID3D11DeviceContext*c,BOOL restore,ID3D11CommandList**out){ApiScope api(c);
 const auto hr=DrawOriginal(c,114,finish)(c,restore,out);RefreshDrawHooks(c);if(!Enabled())return hr;std::lock_guard lock(mutex);auto* state=Get(c);if(!state)return hr;
 if(SUCCEEDED(hr)&&out&&*out){if(lists.size()>=32)state->overflow=true;else lists[*out]={*out,state->candidates,state->overflow};}
 ResetInterval(*state);if(!restore)state->bound.Reset();return hr;
}
void STDMETHODCALLTYPE HExecute(ID3D11DeviceContext*c,ID3D11CommandList*list,BOOL restore){ApiScope api(c);
 DrawOriginal(c,58,execute)(c,list,restore);RefreshDrawHooks(c);if(!Enabled())return;std::lock_guard lock(mutex);auto* state=Get(c);if(!state)return;auto i=lists.find(list);
 if(i==lists.end()){state->overflow=true;return;}
 if(i->second.overflow)state->overflow=true;
 for(const auto& candidate:i->second.candidates){if(!candidate.depth)continue;auto slot=std::find_if(state->candidates.begin(),state->candidates.end(),[&](const Candidate& v){return !v.depth||v.depth==candidate.depth||(!v.draws&&!v.savedDraws&&(!v.snapshot||(v.snapshot->completed&&v.snapshot.use_count()==1)));});if(slot==state->candidates.end())state->overflow=true;else *slot=candidate;}
 lists.erase(i);if(!restore)state->bound.Reset();
}
void STDMETHODCALLTYPE HClearState(ID3D11DeviceContext*c){ApiScope api(c);DrawOriginal(c,110,clearState)(c);Bind(c,nullptr);RefreshDrawHooks(c);}
void ObserveContext(ID3D11DeviceContext*c){if(!c)return;std::lock_guard lock(mutex);if(contexts.size()>=16&&!contexts.contains(c))return;contexts[c].context=c;}
HRESULT STDMETHODCALLTYPE HDeferred(ID3D11Device*d,UINT flags,ID3D11DeviceContext**out){auto hr=deferred(d,flags,out);if(SUCCEEDED(hr)&&out&&*out){
 ComPtr<ID3D11DeviceContext> immediate;d->GetImmediateContext(&immediate);std::lock_guard lock(mutex);
 if(Get(immediate.Get())){ObserveContext(*out);HookDeferredContext(*out);}
 }return hr;}
void RefreshDrawHooks(ID3D11DeviceContext*c){
 if(!Enabled())return;std::lock_guard lock(mutex);if(!Get(c))return;auto v=*reinterpret_cast<void***>(c);
 const std::array<unsigned,20> indices{33,34,13,12,20,21,38,39,40,53,58,114,110,11,9,23,60,64,24,17};
 const std::array<void*,20> hooks{reinterpret_cast<void*>(HOm),reinterpret_cast<void*>(HOmU),reinterpret_cast<void*>(HD),reinterpret_cast<void*>(HDI),reinterpret_cast<void*>(HDIN),reinterpret_cast<void*>(HDN),reinterpret_cast<void*>(HDA),reinterpret_cast<void*>(HDX),reinterpret_cast<void*>(HDX2),reinterpret_cast<void*>(HClear),reinterpret_cast<void*>(HExecute),reinterpret_cast<void*>(HFinish),reinterpret_cast<void*>(HClearState),reinterpret_cast<void*>(HvsSet),reinterpret_cast<void*>(HpsSet),reinterpret_cast<void*>(HgsSet),reinterpret_cast<void*>(HhsSet),reinterpret_cast<void*>(HdsSet),reinterpret_cast<void*>(HTopology),reinterpret_cast<void*>(HLayout)};
 bool pending=false;for(auto index:indices)if(!drawTargets.contains(std::pair{index,v[index]})){pending=true;break;}
 if(!pending)return;pending=false;DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
 for(unsigned n=0;n<indices.size();++n){auto key=std::pair{indices[n],v[indices[n]]};if(drawTargets.contains(key))continue;if(drawTargets.size()>=256){unsafe=true;break;}auto [entry,inserted]=drawTargets.emplace(key,v[indices[n]]);DetourAttach(&entry->second,hooks[n]);pending=true;}
 if(!pending){DetourTransactionAbort();return;}if(CommitHookTransaction()!=NO_ERROR){unsafe=true;Reason("D3D11 dispatch observation unavailable; restart required");}
}
void HookDeferredContext(ID3D11DeviceContext*c){RefreshDrawHooks(c);}

}
void HookDevice(ID3D11Device* device){
 if(!device||!Enabled())return;std::lock_guard lock(mutex);ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);ComPtr<ID3D11Multithread> multi;
 if(FAILED(QuerySerialization(context.Get(),&multi)))return;
 multi->SetMultithreadProtected(TRUE);if(!multi->GetMultithreadProtected())return;
 ObserveContext(context.Get());if(!Get(context.Get()))return;if(om){RefreshDrawHooks(context.Get());return;}
 auto v=*reinterpret_cast<void***>(context.Get());auto dv=*reinterpret_cast<void***>(device);
 std::copy_n(v,115,immediateEntries);om=reinterpret_cast<OM>(v[33]);omu=reinterpret_cast<OMU>(v[34]);draw=reinterpret_cast<D>(v[13]);indexed=reinterpret_cast<DI>(v[12]);indexedInst=reinterpret_cast<DIN>(v[20]);inst=reinterpret_cast<DN>(v[21]);automatic=reinterpret_cast<DA>(v[38]);indexedIndirect=reinterpret_cast<DX>(v[39]);indirect=reinterpret_cast<DX>(v[40]);clear=reinterpret_cast<CD>(v[53]);execute=reinterpret_cast<EX>(v[58]);finish=reinterpret_cast<FN>(v[114]);deferred=reinterpret_cast<DC>(dv[27]);clearState=reinterpret_cast<CS>(v[110]);
 vsSet=reinterpret_cast<SH>(v[11]);psSet=reinterpret_cast<SH>(v[9]);gsSet=reinterpret_cast<SH>(v[23]);hsSet=reinterpret_cast<SH>(v[60]);dsSet=reinterpret_cast<SH>(v[64]);topology=reinterpret_cast<TO>(v[24]);layout=reinterpret_cast<IL>(v[17]);
 DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
 DetourAttach(reinterpret_cast<PVOID*>(&deferred),HDeferred);
 if(CommitHookTransaction()!=NO_ERROR){unsafe=true;Reason("D3D11 observation hooks unavailable; restart required");}
 RefreshDrawHooks(context.Get());
}
bool CanYieldOutput(std::string& reason){
 std::unique_lock lock(mutex,std::try_to_lock);
 if(!lock.owns_lock()){reason="D3D11 capture owner is busy";return false;}
 const auto refuse=[&](const char* text){reason=text;return false;};
 if(unsafe)return refuse("D3D11 capture completion is quarantined");
 if(captureActive.load())return refuse("D3D11 capture interval is still enabled");
 for(const auto& [_,state]:contexts)for(const auto& candidate:state.candidates)
  if(candidate.snapshot&&!candidate.snapshot->completed)return refuse("D3D11 snapshot completion is pending");
 for(const auto& [_,recording]:lists)for(const auto& candidate:recording.candidates)
  if(candidate.snapshot&&!candidate.snapshot->completed)return refuse("D3D11 deferred snapshot completion is pending");
#ifndef NR_D3D11_OBSERVER_TEST
 if(connection&&connection->RequiresRestart())return refuse("D3D11 capture owner is quarantined");
 // Capture/Finish/Cancel are synchronous under this lock; any incomplete
 // episode latches unsafe above. Retained completed model leases may stay.
 if(!processor.CanYieldOutput(reason))return false;
#endif
 reason.clear();return true;
}
#ifdef NR_D3D11_OBSERVER_TEST
void TestRefuseSerialization(ID3D11DeviceContext* context){refusedSerializationContext=context;}
void TestSetObserverAdmissionHook(void (*hook)()){observerAdmissionHook=hook;}
void TestRefuseCompletion(ID3D11DeviceContext* context){refusedCompletionContext=context;}
void TestFailNextHookCommit(){failNextHookCommit=true;}
bool TestCaptureEnabled(){return Enabled();}
bool TestContextRegistered(ID3D11DeviceContext* context){std::lock_guard lock(mutex);return Get(context)!=nullptr;}
bool TestPresentContext(ID3D11DeviceContext* context){std::lock_guard lock(mutex);return PresentContext(context)!=nullptr;}
bool TestObservedDepth(ID3D11DeviceContext* c,unsigned width,unsigned height,ID3D11Texture2D** out,int& direction){
 ApiScope api(c);std::lock_guard lock(mutex);auto* state=Get(c);if(!state)return false;bool saved=false;auto* best=Select(*state,width,height,saved,direction);
 if(!best)return false;*out=(saved?best->snapshot->texture:best->depth).Get();(*out)->AddRef();return true;
}
bool TestSourceReset(ID3D11Texture2D* depth,unsigned width,unsigned height,int direction,uint64_t& currentGeneration){auto reset=SourceReset(depth,width,height,direction);currentGeneration=generation;return reset;}
void TestAcceptHistory(bool accepted){historyValid=accepted;}
void TestSetCaptureActive(bool enabled){captureActive=enabled;if(!enabled)historyValid=false;}
uint64_t TestSnapshotBytes(){return snapshotBytes;}
void TestConsumeInterval(ID3D11DeviceContext*c){ApiScope api(c);std::lock_guard lock(mutex);if(auto* state=Get(c))if(!CompleteInterval(*state))unsafe=true;}
#else
void Present(IDXGISwapChain* swapchain,ID3D11Device* device,bool nrEnabled){
 if(!device||!swapchain||!Enabled())return;
 ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);ApiScope api(context.Get());std::lock_guard lock(mutex);Guard guard;
 auto* state=PresentContext(context.Get());if(!state)return;
 nrEnabled=nrEnabled&&(NativeGuides::SelectedSource()==0||NativeGuides::SelectedSource()==2);
 const bool wasActive=captureActive.exchange(nrEnabled);
 status.guideReady=status.outputValid=status.modelPreparing=0;status.stage=PreparedGuides::Stage::Waiting;
 bool sourceCompleted=false,historyAccepted=false,episodeClaimed=false,captureRetired=true;
 struct Reset {Context& s;bool& completed;bool& accepted;bool& claimed;bool& retired;~Reset(){
  if(!CompleteInterval(s,completed)){unsafe=true;status.restartRequired=1;NeuRotic_RetirePreparedConnectionV1(2,Token(),0);Reason("D3D11 interval completion unknown; retained snapshots require restart");}
  if(claimed)NeuRotic_RetirePreparedConnectionV1(2,Token(),!unsafe&&retired?1:0);
  if(!accepted)historyValid=false;
 }} reset{*state,sourceCompleted,historyAccepted,episodeClaimed,captureRetired};
 // Enable changes apply to the following source interval. Never consume
 // observations accumulated while NR was off as a current frame.
 if(!nrEnabled||!wasActive){Reason("Built-in capture is not selected for this Present interval");return;}
 if(state->overflow){Reason("D3D11 depth association ambiguous or command list untracked");return;}
 if(selectedSwapchain&&selectedSwapchain.Get()!=swapchain){Reason("A different D3D11 swapchain owns this session");return;}
 ComPtr<ID3D11Texture2D> color;if(FAILED(swapchain->GetBuffer(0,IID_PPV_ARGS(&color))))return;
 D3D11_TEXTURE2D_DESC desc{};color->GetDesc(&desc);
 bool saved=false;int direction=-1;auto* best=Select(*state,desc.Width,desc.Height,saved,direction);
 if(!best){Reason("Waiting for unique D3D11 scene depth and observed direction");return;}
 if(!NeuRotic_ClaimPreparedConnectionV1(2,1,Token())){Reason("Another input route is selected for this Present");return;}
 episodeClaimed=true;
 selectedSwapchain=swapchain;
 status.selectedSource=Connections::Source::BuiltIn;status.effectiveTransport=Connections::Transport::GPUOnly;status.sourceApi=0xb000;status.producerBits=sizeof(void*)*8;
 status.producerIdentity=status.session=Token();status.creationReady=1;status.depthOrigin=PreparedGuides::Origin::Observed;status.motionOrigin=PreparedGuides::Origin::Derived;
 status.liveBytes=snapshotBytes;status.candidateId=reinterpret_cast<uint64_t>(best->depth.Get());status.candidateConfidence=50;status.depthDirection=direction;
 if(!connection){ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC ad{};if(FAILED(device->QueryInterface(IID_PPV_ARGS(&dx)))||FAILED(dx->GetAdapter(&adapter))||FAILED(adapter->GetDesc(&ad))){Reason("D3D11 adapter identity unavailable");return;}uint64_t luid=0;std::memcpy(&luid,&ad.AdapterLuid,8);std::string reason;auto* renderer=processor.SharedDevice(luid,reason);if(!renderer){Reason(reason.c_str());return;}connection=std::make_unique<Connections::D3D11Connection>(renderer);selectedDevice=device;}
 if(selectedDevice.Get()!=device){status.restartRequired=1;Reason("D3D11 source device changed; restart required");return;}
 const bool resetHistory=SourceReset(best->depth.Get(),desc.Width,desc.Height,direction);
 Connections::D3D11ObservedFrame frame;frame.context=context;frame.color=color;frame.depth=saved?best->snapshot->texture:best->depth;frame.outputTarget=color;
 // Synchronous Present scope excludes game-thread writes; retained COM textures
 // and the adapter's exact fence handshake cover every GPU reader.
 if(saved)frame.lease=best->snapshot;else frame.lease=std::make_shared<ComPtr<ID3D11Texture2D>>(frame.depth);
 auto& d=frame.description;d.producer=d.session=d.stream=Token();d.device=reinterpret_cast<uint64_t>(device);d.generation=generation;d.capture=++capture;d.sourceApi=Neurotic::Contracts::GraphicsApi::D3D11;
 d.color=d.depth=d.motion={desc.Width,desc.Height,0,0,desc.Width,desc.Height};d.depthReversed=direction;d.previousCapture=resetHistory?0:historyCapture;if(resetHistory)d.flags|=Neurotic::Feed::Prepared::Reset;
 status.capture=capture;status.generation=generation;status.captureWidth=status.workWidth=status.outputWidth=desc.Width;status.captureHeight=status.workHeight=status.outputHeight=desc.Height;status.outputValid=0;
 Connections::PendingFrame pending;std::string reason;
 if(!connection->Capture(frame,pending,reason)){unsafe=connection->RequiresRestart();status.restartRequired=unsafe;Reason(reason.c_str());if(unsafe)NeuRotic_RetirePreparedConnectionV1(2,Token(),0);return;}
 sourceCompleted=true;captureRetired=false;
 ++status.inputFrames;status.guideReady=1;nrpg::CpuMainlineClient::GpuOutput output;const auto outcome=processor.ProcessTextures(pending.textures,output,reason);
 status.modelPreparing=outcome==NativeGuides::Outcome::Preparing;
 historyAccepted=outcome==NativeGuides::Outcome::Preparing||outcome==NativeGuides::Outcome::Delivered;
 historyValid=historyAccepted;if(historyAccepted)historyCapture=d.capture;
 if(outcome==NativeGuides::Outcome::Delivered){++status.modelCompletions;Connections::CopybackReceipt receipt;if((captureRetired=connection->Finish(pending,output,receipt,reason)&&receipt.completed)){++status.copybackCompletions;status.outputValid=1;status.stage=PreparedGuides::Stage::Delivered;}}
 else if(outcome!=NativeGuides::Outcome::Unsafe)captureRetired=connection->Cancel(pending,reason);
 if(outcome==NativeGuides::Outcome::Delivered&&!status.outputValid){historyAccepted=historyValid=false;}
 unsafe=!captureRetired||outcome==NativeGuides::Outcome::Unsafe||connection->RequiresRestart();status.restartRequired=unsafe;if(unsafe)NeuRotic_RetirePreparedConnectionV1(2,Token(),0);
 Reason(reason.empty()?"D3D11 estimated scene association; output copyback completed, display unobserved":reason.c_str());
}
#endif
}
