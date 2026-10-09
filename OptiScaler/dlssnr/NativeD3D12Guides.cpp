#include "NativeD3D12Guides.h"
#include "NativeGuideProcessor.h"
#include "NativeGuideRoute.h"
#include "FinalFallbackControl.h"
#include "NativeIdentity.h"
#ifndef NR_NATIVE12_TESTING
#include "HdrObservation.h"
#include "PreparedGuideStatusV2.h"
#include "connections/ConnectionPolicy.h"
#include "connections/D3D12Connection.h"
#include <SysUtils.h>
#endif
#include <detours/detours.h>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <array>
#include <atomic>
#include <limits>
#include <TlHelp32.h>

namespace DlssNr::NativeD3D12Guides {
using Microsoft::WRL::ComPtr;
namespace P=Neurotic::Feed::Prepared;
#ifndef NR_NATIVE12_TESTING
namespace C=Connections;
#endif
namespace {
thread_local bool internal=false;
struct Guard {bool old=internal;Guard(){internal=true;}~Guard(){internal=old;}};
enum class Kind {Draw,Clear,Barrier};
constexpr unsigned MaxDescriptors=1024,MaxRecordings=64,MaxEvents=256,MaxResources=64;
constexpr uint64_t MaxSourceBytes=1024ull*1024*1024;
struct Budget {std::atomic<uint64_t> bytes{0};uint64_t limit=MaxSourceBytes;};
struct Resource {ComPtr<ID3D12Resource> image;std::shared_ptr<Budget> budget;uint64_t bytes=0,lastUse=0;~Resource(){if(budget)budget->bytes.fetch_sub(bytes);}};
struct Image {std::shared_ptr<Resource> owner;ID3D12Resource* Get()const{return owner?owner->image.Get():nullptr;}ID3D12Resource*operator->()const{return Get();}explicit operator bool()const{return bool(owner);}void Reset(){owner.reset();}};
struct Event {Kind kind{};Image image;uint64_t draws=0;int direction=-1;D3D12_RESOURCE_STATES state{};bool known=false;D3D12_RESOURCE_STATES before{};};
struct Recording {ComPtr<ID3D12GraphicsCommandList> owner;Image depth;std::vector<Event> events;bool closed=false,unsupported=false;};
struct Candidate {Image image;uint64_t draws=0;int direction=-1;D3D12_RESOURCE_STATES state{};bool known=false;ComPtr<ID3D12CommandQueue> queue;};
struct Owner {
 std::recursive_mutex mutex;
 std::shared_ptr<Budget> budget=std::make_shared<Budget>();
 std::unordered_map<ID3D12Resource*,std::weak_ptr<Resource>> resources;
 ComPtr<ID3D12Device> observationDevice;ComPtr<IUnknown> selectedChain;
 std::unordered_map<SIZE_T,Image> descriptors;
 std::unordered_map<ID3D12GraphicsCommandList*,Recording> recordings;
 std::unordered_map<ID3D12Resource*,Candidate> submitted;
#ifndef NR_NATIVE12_TESTING
 std::unique_ptr<C::D3D12Connection> connection;
 NativeGuides::Processor processor;C::PendingFrame pending;nrpg::CpuMainlineClient::GpuOutput output;
#endif
 ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;
 uint64_t serial=0,capture=0,generation=1,inputEpoch=0;unsigned width=0,height=0;bool installed=false,unsafe=false;
 uint64_t observationStamp=0;unsigned excludedLast=0;bool resourcePressure=false,recordingPressure=false;
#ifndef NR_NATIVE12_TESTING
 PreparedGuides::StatusV2 status;
 uint64_t lastLogTick=0;std::string lastLogReason;
 unsigned presentWidth=0,presentHeight=0,presentFormat=0,presentColorSpace=0;
#endif
};
Owner& State(){static auto* state=new Owner;return *state;}
constexpr uint64_t Token=0x4e52443344313201ull;
bool Observing(){return NativeGuides::ObserveBuiltIn();}
bool Selected(){const auto s=NativeGuides::SelectedSource();return s==0||s==2;}
bool DeviceMatches(ID3D12Device* device){return device&&State().observationDevice&&NativeIdentity::CompareDevices(device,State().observationDevice.Get()).equal;}
uint64_t Touch(){auto&s=State();if(s.observationStamp==UINT64_MAX){for(auto&[_,weak]:s.resources)if(auto resource=weak.lock())resource->lastUse=0;s.observationStamp=0;}return ++s.observationStamp;}
bool Active(ID3D12Resource* image){auto&s=State();auto candidate=s.submitted.find(image);if(candidate!=s.submitted.end()&&candidate->second.draws)return true;
 for(const auto&[_,recording]:s.recordings){if(recording.depth.Get()==image)return true;for(const auto&e:recording.events)if(e.image.Get()==image)return true;}return false;}
// These are observation references only. Captured/canonical/pending resources
// belong to the connection/Processor and are never reachable through this cache.
bool EvictInactive(){auto&s=State();ID3D12Resource* victim=nullptr;uint64_t oldest=UINT64_MAX;
 for(auto i=s.resources.begin();i!=s.resources.end();){if(auto resource=i->second.lock()){if(!Active(i->first)&&resource->lastUse<=oldest){victim=i->first;oldest=resource->lastUse;}++i;}else i=s.resources.erase(i);}
 if(!victim)return false;for(auto i=s.descriptors.begin();i!=s.descriptors.end();)if(i->second.Get()==victim)i=s.descriptors.erase(i);else ++i;
 s.submitted.erase(victim);s.resources.erase(victim);return true;}
bool DescriptorRoom(){auto&s=State();if(s.descriptors.size()<MaxDescriptors)return true;
 // Evict only the lookup entry. Active recordings/candidates keep their Image
 // lease; a later bind of an evicted descriptor cleanly has no association.
 auto oldest=s.descriptors.begin();for(auto i=s.descriptors.begin();i!=s.descriptors.end();++i)if(i->second.owner->lastUse<oldest->second.owner->lastUse)oldest=i;
 s.descriptors.erase(oldest);return true;}
void ConsumeFrame(){auto&s=State();for(auto&[_,candidate]:s.submitted)candidate.draws=0;}
Image Retain(ID3D12Resource* image){auto&s=State();if(!image)return {};auto found=s.resources.find(image);if(found!=s.resources.end())if(auto existing=found->second.lock()){existing->lastUse=Touch();s.resourcePressure=false;return {existing};}
 for(auto i=s.resources.begin();i!=s.resources.end();)if(i->second.expired())i=s.resources.erase(i);else ++i;
 ComPtr<ID3D12Device> device;if(FAILED(image->GetDevice(IID_PPV_ARGS(&device)))||!DeviceMatches(device.Get()))return {};
 const auto d=image->GetDesc();if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.MipLevels!=1||d.DepthOrArraySize!=1||d.SampleDesc.Count!=1||!(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))return {};
 const auto allocation=device->GetResourceAllocationInfo(0,1,&d);if(!allocation.SizeInBytes||allocation.SizeInBytes==UINT64_MAX)return {};
 if(allocation.SizeInBytes>s.budget->limit){s.resourcePressure=true;return {};}
 while(s.resources.size()>=MaxResources||s.budget->bytes.load()>s.budget->limit-allocation.SizeInBytes)if(!EvictInactive()){s.resourcePressure=true;return {};}
 auto tracked=std::make_shared<Resource>();tracked->image=image;tracked->budget=s.budget;tracked->bytes=allocation.SizeInBytes;tracked->lastUse=Touch();s.budget->bytes.fetch_add(tracked->bytes);s.resources[image]=tracked;s.resourcePressure=false;return {tracked};}
void Unsupported(Recording&r){r.unsupported=true;r.depth.Reset();r.events.clear();}
using CreateView=void(STDMETHODCALLTYPE*)(ID3D12Device*,ID3D12Resource*,const D3D12_DEPTH_STENCIL_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
using CopySimple=void(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_DESCRIPTOR_HEAP_TYPE);
using CopyMany=void(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,D3D12_DESCRIPTOR_HEAP_TYPE);
using Bind=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,BOOL,const D3D12_CPU_DESCRIPTOR_HANDLE*);
using Draw=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT,UINT);
using DrawIndexed=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT,INT,UINT);
using Clear=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CLEAR_FLAGS,FLOAT,UINT8,UINT,const D3D12_RECT*);
using Barriers=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
using Reset=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
using Close=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*);
using Execute=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
using Bundle=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12GraphicsCommandList*);
using Enhanced=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList7*,UINT,const D3D12_BARRIER_GROUP*);
using BeginPass=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList4*,UINT,const D3D12_RENDER_PASS_RENDER_TARGET_DESC*,const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC*,D3D12_RENDER_PASS_FLAGS);
CreateView originalView=nullptr;CopySimple originalSimple=nullptr;CopyMany originalMany=nullptr;
Bind originalBind=nullptr;Draw originalDraw=nullptr;DrawIndexed originalIndexed=nullptr;Clear originalClear=nullptr;
Barriers originalBarriers=nullptr;Reset originalReset=nullptr;Close originalClose=nullptr;Execute originalExecute=nullptr;Bundle originalBundle=nullptr;Enhanced originalEnhanced=nullptr;
BeginPass originalBeginPass=nullptr;
Recording* Record(ID3D12GraphicsCommandList* list){auto& s=State();auto found=s.recordings.find(list);return found==s.recordings.end()?nullptr:&found->second;}
void Append(Recording& r,Event e){if(r.unsupported)return;if(!e.image||r.closed||r.events.size()>=MaxEvents){Unsupported(r);return;}r.events.push_back(std::move(e));}
void STDMETHODCALLTYPE ViewHook(ID3D12Device* dev,ID3D12Resource* image,const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,D3D12_CPU_DESCRIPTOR_HANDLE handle){
 originalView(dev,image,desc,handle);if(internal||!Observing())return;auto& s=State();std::lock_guard lock(s.mutex);if(!DeviceMatches(dev))return;
 s.descriptors.erase(handle.ptr);if(!image||!DescriptorRoom())return;
 const auto shape=image->GetDesc();if(shape.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||shape.MipLevels!=1||shape.DepthOrArraySize!=1||shape.SampleDesc.Count!=1)return;
 if(desc&&(desc->ViewDimension!=D3D12_DSV_DIMENSION_TEXTURE2D||desc->Texture2D.MipSlice))return;
 auto retained=Retain(image);if(retained)s.descriptors[handle.ptr]=retained;
}
void CopyViews(ID3D12Device* dev,UINT count,D3D12_CPU_DESCRIPTOR_HANDLE dst,D3D12_CPU_DESCRIPTOR_HANDLE src){
 auto& s=State();const auto stride=dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
 if(!DeviceMatches(dev)||!stride)return;
 if(count>MaxDescriptors){s.descriptors.clear();return;}
 std::vector<Image> values(count);for(UINT i=0;i<count;++i){auto f=s.descriptors.find(src.ptr+SIZE_T(i)*stride);if(f!=s.descriptors.end())values[i]=f->second;}
 for(UINT i=0;i<count;++i){const auto address=dst.ptr+SIZE_T(i)*stride;s.descriptors.erase(address);if(values[i]&&DescriptorRoom())s.descriptors[address]=values[i];}
}
void STDMETHODCALLTYPE SimpleHook(ID3D12Device* dev,UINT n,D3D12_CPU_DESCRIPTOR_HANDLE dst,D3D12_CPU_DESCRIPTOR_HANDLE src,D3D12_DESCRIPTOR_HEAP_TYPE type){
 originalSimple(dev,n,dst,src,type);if(!internal&&Observing()&&type==D3D12_DESCRIPTOR_HEAP_TYPE_DSV){std::lock_guard lock(State().mutex);CopyViews(dev,n,dst,src);}}
void STDMETHODCALLTYPE ManyHook(ID3D12Device* dev,UINT nd,const D3D12_CPU_DESCRIPTOR_HANDLE* dst,const UINT* ds,UINT ns,const D3D12_CPU_DESCRIPTOR_HANDLE* src,const UINT* ss,D3D12_DESCRIPTOR_HEAP_TYPE type){
 originalMany(dev,nd,dst,ds,ns,src,ss,type);if(internal||!Observing()||type!=D3D12_DESCRIPTOR_HEAP_TYPE_DSV)return;
 auto& s=State();std::lock_guard lock(s.mutex);if(!DeviceMatches(dev))return;if(nd>MaxDescriptors||ns>MaxDescriptors){s.descriptors.clear();return;}
 const auto stride=dev->GetDescriptorHandleIncrementSize(type);std::vector<Image> values;
 for(UINT i=0;i<ns;++i)for(UINT j=0;j<(ss?ss[i]:1);++j){if(values.size()>=MaxDescriptors){s.descriptors.clear();return;}auto f=s.descriptors.find(src[i].ptr+SIZE_T(j)*stride);values.push_back(f==s.descriptors.end()?Image{}:f->second);}
 size_t index=0;for(UINT i=0;i<nd;++i)for(UINT j=0;j<(ds?ds[i]:1);++j){if(index>=values.size()){s.descriptors.clear();return;}auto address=dst[i].ptr+SIZE_T(j)*stride;s.descriptors.erase(address);if(values[index]&&DescriptorRoom())s.descriptors[address]=values[index];++index;}
}
void STDMETHODCALLTYPE BindHook(ID3D12GraphicsCommandList* list,UINT n,const D3D12_CPU_DESCRIPTOR_HANDLE* rt,BOOL contiguous,const D3D12_CPU_DESCRIPTOR_HANDLE* ds){
 if(!internal&&Observing()){auto& s=State();std::lock_guard lock(s.mutex);if(auto* r=Record(list)){r->depth.Reset();if(ds){auto f=s.descriptors.find(ds->ptr);if(f!=s.descriptors.end())r->depth=f->second;}}}
 originalBind(list,n,rt,contiguous,ds);
}
void ObserveDraw(ID3D12GraphicsCommandList* list,UINT n,UINT instances){if(internal||!Observing()||!n||!instances)return;std::lock_guard lock(State().mutex);if(auto* r=Record(list);r&&r->depth)Append(*r,{Kind::Draw,r->depth,uint64_t(n)*instances});}
void STDMETHODCALLTYPE DrawHook(ID3D12GraphicsCommandList* l,UINT a,UINT b,UINT c,UINT d){ObserveDraw(l,a,b);originalDraw(l,a,b,c,d);}
void STDMETHODCALLTYPE IndexedHook(ID3D12GraphicsCommandList* l,UINT a,UINT b,UINT c,INT d,UINT e){ObserveDraw(l,a,b);originalIndexed(l,a,b,c,d,e);}
void STDMETHODCALLTYPE ClearHook(ID3D12GraphicsCommandList* list,D3D12_CPU_DESCRIPTOR_HANDLE ds,D3D12_CLEAR_FLAGS flags,FLOAT depth,UINT8 stencil,UINT n,const D3D12_RECT* rects){
 if(!internal&&Observing()&&(flags&D3D12_CLEAR_FLAG_DEPTH)){auto& s=State();std::lock_guard lock(s.mutex);auto f=s.descriptors.find(ds.ptr);if(auto* r=Record(list);r&&f!=s.descriptors.end())Append(*r,{Kind::Clear,f->second,0,n==0&&depth==0?1:n==0&&depth==1?0:-1});}
 originalClear(list,ds,flags,depth,stencil,n,rects);
}
void STDMETHODCALLTYPE BarrierHook(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
 if(!internal&&Observing()){std::lock_guard lock(State().mutex);if(auto* r=Record(list))for(UINT i=0;i<count;++i){const auto& b=barriers[i];if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION&&b.Transition.pResource){const auto shape=b.Transition.pResource->GetDesc();if(shape.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)Append(*r,{Kind::Barrier,Retain(b.Transition.pResource),0,-1,b.Transition.StateAfter,b.Flags==D3D12_RESOURCE_BARRIER_FLAG_NONE&&(b.Transition.Subresource==0||b.Transition.Subresource==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES),b.Transition.StateBefore});}else if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING)Unsupported(*r);}}
 originalBarriers(list,count,barriers);
}
HRESULT STDMETHODCALLTYPE ResetHook(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* a,ID3D12PipelineState* p){const auto hr=originalReset(list,a,p);if(!internal&&Observing()){auto& s=State();std::lock_guard lock(s.mutex);s.recordings.erase(list);ComPtr<ID3D12Device> device;if(SUCCEEDED(hr)&&list->GetType()==D3D12_COMMAND_LIST_TYPE_DIRECT&&SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device)))&&DeviceMatches(device.Get())){if(s.recordings.size()>=MaxRecordings)s.recordingPressure=true;else {Recording r;r.owner=list;s.recordings.emplace(list,std::move(r));s.recordingPressure=false;}}}return hr;}
HRESULT STDMETHODCALLTYPE CloseHook(ID3D12GraphicsCommandList* list){const auto hr=originalClose(list);if(!internal&&Observing()){std::lock_guard lock(State().mutex);if(auto* r=Record(list))r->closed=SUCCEEDED(hr);}return hr;}
void STDMETHODCALLTYPE BundleHook(ID3D12GraphicsCommandList* list,ID3D12GraphicsCommandList* bundle){if(!internal&&Observing()){std::lock_guard lock(State().mutex);if(auto* r=Record(list))Unsupported(*r);}originalBundle(list,bundle);}
void STDMETHODCALLTYPE EnhancedHook(ID3D12GraphicsCommandList7* list,UINT n,const D3D12_BARRIER_GROUP* groups){if(!internal&&Observing()){std::lock_guard lock(State().mutex);if(auto* r=Record(list))Unsupported(*r);}originalEnhanced(list,n,groups);}
void STDMETHODCALLTYPE BeginPassHook(ID3D12GraphicsCommandList4* list,UINT n,const D3D12_RENDER_PASS_RENDER_TARGET_DESC* targets,const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* depth,D3D12_RENDER_PASS_FLAGS flags){
 if(!internal&&Observing()){std::lock_guard lock(State().mutex);if(auto*r=Record(list))Unsupported(*r);}originalBeginPass(list,n,targets,depth,flags);
}
void STDMETHODCALLTYPE ExecuteHook(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* lists){
 if(internal||!Observing()){originalExecute(queue,count,lists);return;}auto& s=State();std::lock_guard lock(s.mutex);originalExecute(queue,count,lists);
 ComPtr<ID3D12Device> device;if(queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT||FAILED(queue->GetDevice(IID_PPV_ARGS(&device)))||!DeviceMatches(device.Get()))return;
 s.excludedLast=0;
 for(UINT i=0;i<count;++i){ComPtr<ID3D12GraphicsCommandList> list;if(FAILED(lists[i]->QueryInterface(IID_PPV_ARGS(&list))))continue;auto* r=Record(list.Get());if(!r||!r->closed||r->unsupported){s.submitted.clear();s.excludedLast=1;s.recordings.erase(list.Get());s.recordingPressure=s.recordings.size()>=MaxRecordings;continue;}
  for(const auto& e:r->events){auto f=s.submitted.find(e.image.Get());if(f==s.submitted.end()){if(s.submitted.size()>=MaxResources)continue;f=s.submitted.emplace(e.image.Get(),Candidate{}).first;f->second.image=e.image;}
   auto& c=f->second;if(c.queue&&c.queue.Get()!=queue){c.known=false;c.draws=0;}c.queue=queue;
   if(e.kind==Kind::Barrier){const bool matches=!c.known||c.state==e.before;c.state=e.state;c.known=e.known&&matches;}else if(e.kind==Kind::Clear){c.draws=0;c.direction=e.direction;}else if(e.draws>UINT64_MAX-c.draws){c.draws=0;c.known=false;}else c.draws+=e.draws;
  }
  // Observation metadata is consumed once, independently of the application's
  // GPU lifetime. Replays need a fresh Reset/recording or are safely excluded.
  s.recordings.erase(list.Get());s.recordingPressure=false;
 }
}
#ifndef NR_NATIVE12_TESTING
void Publish(const char* reason,bool delivered=false){auto& s=State();auto& st=s.status;st.session=Token;st.capture=s.capture;st.updatedTickMs=GetTickCount64();st.selectedSource=C::Source::BuiltIn;st.effectiveTransport=C::Transport::GPUOnly;
 st.sourceApi=static_cast<uint32_t>(P::C::GraphicsApi::D3D12);st.producerBits=64;st.creationReady=s.installed;st.restartRequired=s.unsafe;st.outputValid=delivered;
 st.stage=delivered?PreparedGuides::Stage::Delivered:PreparedGuides::Stage::Blocked;strncpy_s(st.reason,reason,_TRUNCATE);PreparedGuides::PublishStatusV2(st);
 // Report changing refusal reasons without emitting a line per Present. These
 // are observer facts, not proof of native motion or displayed model output.
 if(!s.lastLogTick||(st.updatedTickMs-s.lastLogTick>=5000&&s.lastLogReason!=reason)){
  LOG_INFO("Built-in D3D12 guides: {}; source={} installed={} unsafe={} color={}x{} format={} colorSpace={} depthCandidates={} descriptors={} recordings={}",
   reason,NativeGuides::SelectedSource(),s.installed,s.unsafe,s.presentWidth,s.presentHeight,s.presentFormat,s.presentColorSpace,s.submitted.size(),s.descriptors.size(),s.recordings.size());
  s.lastLogTick=st.updatedTickMs;s.lastLogReason=reason;
 }}
#endif
}
void Install(ID3D12Device* device){
 if(!device||!Observing()||internal)return;auto& s=State();std::lock_guard lock(s.mutex);if(s.installed||s.unsafe)return;Guard guard;
 s.observationDevice=device;
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};
 if(FAILED(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)))||FAILED(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))||FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))))return;
 list->Close();auto** dv=*reinterpret_cast<void***>(device);auto** lv=*reinterpret_cast<void***>(list.Get());auto** qv=*reinterpret_cast<void***>(queue.Get());
 originalView=reinterpret_cast<CreateView>(dv[21]);originalSimple=reinterpret_cast<CopySimple>(dv[24]);originalMany=reinterpret_cast<CopyMany>(dv[23]);
 originalBind=reinterpret_cast<Bind>(lv[46]);originalDraw=reinterpret_cast<Draw>(lv[12]);originalIndexed=reinterpret_cast<DrawIndexed>(lv[13]);originalClear=reinterpret_cast<Clear>(lv[47]);originalBarriers=reinterpret_cast<Barriers>(lv[26]);originalReset=reinterpret_cast<Reset>(lv[10]);originalClose=reinterpret_cast<Close>(lv[9]);originalBundle=reinterpret_cast<Bundle>(lv[27]);originalExecute=reinterpret_cast<Execute>(qv[10]);
 ComPtr<ID3D12GraphicsCommandList7> seven;if(SUCCEEDED(list.As(&seven)))originalEnhanced=reinterpret_cast<Enhanced>((*reinterpret_cast<void***>(seven.Get()))[80]);
 ComPtr<ID3D12GraphicsCommandList4> four;if(SUCCEEDED(list.As(&four)))originalBeginPass=reinterpret_cast<BeginPass>((*reinterpret_cast<void***>(four.Get()))[68]);
 if(DetourTransactionBegin()!=NO_ERROR){s.unsafe=true;return;}
 struct Threads {std::vector<HANDLE> handles;~Threads(){for(auto h:handles)CloseHandle(h);}} threads;
 bool attachFailed=DetourUpdateThread(GetCurrentThread())!=NO_ERROR;
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE)attachFailed=true;
 else {THREADENTRY32 entry{};entry.dwSize=sizeof(entry);for(BOOL next=Thread32First(snapshot,&entry);next;next=Thread32Next(snapshot,&entry))if(entry.th32OwnerProcessID==GetCurrentProcessId()&&entry.th32ThreadID!=GetCurrentThreadId()){
  HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
  if(!thread){attachFailed=true;break;}threads.handles.push_back(thread);}
  CloseHandle(snapshot);}
 if(!attachFailed)for(auto thread:threads.handles)if(DetourUpdateThread(thread)!=NO_ERROR){attachFailed=true;break;}
#define NR_ATTACH(original,hook) do {if(DetourAttach(reinterpret_cast<PVOID*>(&(original)),reinterpret_cast<PVOID>(hook))!=NO_ERROR)attachFailed=true;}while(false)
 NR_ATTACH(originalView,ViewHook);NR_ATTACH(originalSimple,SimpleHook);NR_ATTACH(originalMany,ManyHook);NR_ATTACH(originalBind,BindHook);NR_ATTACH(originalDraw,DrawHook);NR_ATTACH(originalIndexed,IndexedHook);NR_ATTACH(originalClear,ClearHook);NR_ATTACH(originalBarriers,BarrierHook);NR_ATTACH(originalReset,ResetHook);NR_ATTACH(originalClose,CloseHook);NR_ATTACH(originalBundle,BundleHook);NR_ATTACH(originalExecute,ExecuteHook);if(originalEnhanced)NR_ATTACH(originalEnhanced,EnhancedHook);if(originalBeginPass)NR_ATTACH(originalBeginPass,BeginPassHook);
#undef NR_ATTACH
 if(attachFailed){DetourTransactionAbort();s.unsafe=true;return;}
 if(DetourTransactionCommit()==NO_ERROR)s.installed=true;else s.unsafe=true;
}
bool CanYieldOutput(std::string& reason){
 auto& s=State();std::unique_lock lock(s.mutex,std::try_to_lock);
 if(!lock.owns_lock()){reason="D3D12 capture owner is busy";return false;}
 const auto refuse=[&](const char* text){reason=text;return false;};
 if(s.unsafe)return refuse("D3D12 capture completion is quarantined");
 if(s.fence){const auto done=s.fence->GetCompletedValue();
  if(done==UINT64_MAX||done<s.serial)return refuse("D3D12 source completion is pending or unknown");}
#ifndef NR_NATIVE12_TESTING
 if(s.pending.ownerLease)return refuse("D3D12 capture lease remains active");
 if(s.connection&&s.connection->RequiresRestart())return refuse("D3D12 capture owner is quarantined");
 if(!s.processor.CanYieldOutput(reason))return false;
#endif
 reason.clear();return true;
}
void Present(IDXGISwapChain* swapchain,ID3D12CommandQueue* queue,bool enabled){
#ifndef NR_NATIVE12_TESTING
 if(!Observing()||!swapchain||!queue||internal)return;enabled=enabled&&Selected();auto& s=State();std::lock_guard lock(s.mutex);Guard guard;
 s.status.guideReady=s.status.outputValid=s.status.modelPreparing=0;
 auto identity=NativeIdentity::ResolveSwapchainIdentity(swapchain);ComPtr<IUnknown> chainIdentity;if(!identity.object||FAILED(identity.object.As(&chainIdentity)))return;
 if(s.selectedChain&&s.selectedChain.Get()!=chainIdentity.Get()){Publish("Another swapchain is selected; source transfer requires restart");return;}
 if(s.queue&&s.queue.Get()!=queue){Publish("Presentation queue changed; source transfer requires restart");return;}
 struct Consume {~Consume(){ConsumeFrame();}} consume;
 if(!enabled){Publish("Built-in capture is not selected for this Present");return;}
 if(s.unsafe){Publish("Built-in capture unavailable. Restart game.");return;}
 if(!s.installed){Publish("Built-in D3D12 capture hooks unavailable.");return;}
 ComPtr<IDXGISwapChain3> chain;ComPtr<ID3D12Resource> color;ComPtr<ID3D12Device> device;
 const auto hdr=HdrObservation::Registry::Instance().Read(swapchain);
 s.presentColorSpace=static_cast<unsigned>(hdr.colorSpace);
 if(!hdr.registered||hdr.transitioning){Publish("Waiting for stable display color information.");return;}
 if(hdr.colorSpace!=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709){Publish("Built-in D3D12 capture requires SDR output.");return;}
 if(FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&chain)))||FAILED(chain->GetBuffer(chain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&color)))||FAILED(queue->GetDevice(IID_PPV_ARGS(&device)))){Publish("Presentation buffer or device unavailable.");return;}
 if(queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT||!DeviceMatches(device.Get()))return;
 ComPtr<ID3D12Device> colorDevice;if(FAILED(color->GetDevice(IID_PPV_ARGS(&colorDevice)))||!NativeIdentity::CompareDevices(device.Get(),colorDevice.Get()).equal)return;
 const auto desc=color->GetDesc();s.presentWidth=static_cast<unsigned>(desc.Width);s.presentHeight=desc.Height;s.presentFormat=static_cast<unsigned>(desc.Format);Candidate* selected=nullptr;uint64_t runnerUp=0;bool differentRaster=false;
 for(const auto& [_,c]:s.submitted)if(c.queue.Get()==queue&&c.known&&c.draws){const auto d=c.image->GetDesc();differentRaster|=d.Width!=desc.Width||d.Height!=desc.Height;}
 for(auto& [_,c]:s.submitted){const auto d=c.image->GetDesc();if(c.queue.Get()!=queue||!c.known||!c.draws||d.Width!=desc.Width||d.Height!=desc.Height)continue;
  if(!selected||c.draws>selected->draws){if(selected)runnerUp=selected->draws;selected=&c;}else runnerUp=(std::max)(runnerUp,c.draws);}
 if(!selected||selected->draws<3||(runnerUp&&runnerUp>selected->draws/2)){Publish(s.resourcePressure?"Depth observation cache is bounded by active resource references; retry after frame retirement":s.recordingPressure?"Depth observation recording limit reached; retry after active lists are submitted":s.excludedLast?"Submitted depth recording was unsupported, replayed or exceeded the event bound":!selected&&differentRaster?"Observed depth size differs from the presentation buffer.":"No unique fresh submitted scene depth on the presentation queue");return;}
 const int overrideDirection=NativeGuides::depthDirection.load();const int direction=overrideDirection>=0?overrideDirection:selected->direction;
 if(direction<0){for(auto& [_,c]:s.submitted)c.draws=0;Publish("Depth direction requires endpoint clear or explicit override");return;}
 if(!NeuRotic_ClaimPreparedConnectionV1(2,1,Token)){Publish("Another input route is selected for this Present");return;}
 struct RetireEpisode {Owner& s;~RetireEpisode(){
  const bool clean=!s.unsafe&&!s.pending.ownerLease&&(!s.connection||!s.connection->RequiresRestart());
  NeuRotic_RetirePreparedConnectionV1(2,Token,clean?1:0);
 }} retirement{s};
 if(s.capture>=UINT64_MAX-1||s.serial>=UINT64_MAX-1||s.generation>=UINT64_MAX-1){s.unsafe=true;Publish("Source identity/timeline exhausted; restart required");return;}
 if(s.device&&!NativeIdentity::CompareDevices(s.device.Get(),device.Get()).equal){s.unsafe=true;Publish("Device changed; restart required");return;}
 if(!s.connection){s.device=device;s.queue=queue;s.selectedChain=chainIdentity;if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s.fence)))){s.unsafe=true;Publish("Source fence allocation failed");return;}s.connection=std::make_unique<C::D3D12Connection>(device.Get());}
 C::D3D12ObservedFrame frame;frame.color=frame.outputTarget=color;frame.depth=selected->image.Get();frame.colorState=frame.outputState=D3D12_RESOURCE_STATE_PRESENT;frame.depthState=selected->state;frame.statesKnown=true;frame.sourceQueue=queue;frame.producer=s.fence;
 frame.lease=std::make_shared<ComPtr<ID3D12Resource>>(frame.depth);
 const auto epoch=FinalFallback::CurrentInputEpoch();
 const bool reset=s.inputEpoch!=epoch||s.width!=desc.Width||s.height!=desc.Height||s.status.candidateId!=reinterpret_cast<uint64_t>(frame.depth.Get());
 s.inputEpoch=epoch;
 if(reset){++s.generation;s.width=static_cast<unsigned>(desc.Width);s.height=desc.Height;}
 auto& d=frame.description;d.producer=d.session=d.stream=Token;d.device=reinterpret_cast<uint64_t>(device.Get());d.generation=s.generation;d.previousCapture=reset?0:s.capture;d.capture=++s.capture;d.flags=P::ZeroJitterPolicy|P::HudIncluded|(reset?P::Reset:0u);d.depthReversed=direction;d.sourceApi=P::C::GraphicsApi::D3D12;d.color=d.depth={s.width,s.height,0,0,s.width,s.height};
 for(auto& [_,c]:s.submitted)c.draws=0;
 frame.producerValue=++s.serial;if(FAILED(queue->Signal(s.fence.Get(),s.serial))){s.unsafe=true;Publish("Source submission fence failed");return;}
 std::string reason;if(!s.connection->Capture(frame,s.pending,reason)){s.unsafe=s.connection->RequiresRestart();Publish(reason.c_str());return;}
 ++s.status.inputFrames;s.status.guideReady=1;s.status.depthOrigin=PreparedGuides::Origin::Observed;s.status.motionOrigin=PreparedGuides::Origin::Derived;
 s.status.captureWidth=s.width;s.status.captureHeight=s.height;s.status.depthDirection=direction;s.status.candidateId=reinterpret_cast<uint64_t>(frame.depth.Get());s.status.generation=s.generation;s.status.candidateConfidence=1;
 const auto outcome=s.processor.ProcessTextures(s.pending.textures,s.output,reason);
 if(outcome==NativeGuides::Outcome::Unsafe){s.unsafe=true;Publish(reason.c_str());NeuRotic_RetirePreparedConnectionV1(2,Token,0);return;}
 if(outcome!=NativeGuides::Outcome::Delivered){s.status.modelPreparing=outcome==NativeGuides::Outcome::Preparing;std::string retirement;if(!s.connection->Cancel(s.pending,retirement)){s.unsafe=true;reason=retirement;}Publish(reason.c_str());return;}
 ++s.status.modelCompletions;C::CopybackReceipt receipt;
 if(!s.connection->Finish(s.pending,s.output,receipt,reason)||!receipt.completed||receipt.capture!=s.capture){s.unsafe=true;Publish(reason.c_str());NeuRotic_RetirePreparedConnectionV1(2,Token,0);return;}
 ++s.status.copybackCompletions;s.status.outputWidth=s.width;s.status.outputHeight=s.height;s.status.modelPreparing=0;
 Publish("Captured submitted scene depth and derived motion; exact output copied back (estimated scene association)",true);
#else
 (void)swapchain;(void)queue;(void)enabled;
#endif
}
#ifdef NR_NATIVE12_TESTING
Snapshot Inspect(ID3D12CommandQueue* selected){auto&s=State();std::lock_guard lock(s.mutex);Snapshot result;result.installed=s.installed;result.retainedBytes=s.budget->bytes.load();result.descriptors=static_cast<unsigned>(s.descriptors.size());result.recordings=static_cast<unsigned>(s.recordings.size());result.unsupported=s.excludedLast;result.resourcePressure=s.resourcePressure;result.recordingPressure=s.recordingPressure;result.restartRequired=s.unsafe;for(const auto&[_,r]:s.recordings)result.unsupported+=r.unsupported;for(const auto&[_,c]:s.submitted)if(c.known&&c.draws&&(!selected||c.queue.Get()==selected)){++result.candidates;result.draws+=c.draws;}return result;}
void ClearObservations(){auto&s=State();std::lock_guard lock(s.mutex);s.descriptors.clear();s.recordings.clear();s.submitted.clear();s.resources.clear();s.resourcePressure=s.recordingPressure=false;s.excludedLast=0;}
void EndObservationFrame(){auto&s=State();std::lock_guard lock(s.mutex);ConsumeFrame();}
bool SetTestBudget(uint64_t bytes){auto&s=State();std::lock_guard lock(s.mutex);if(s.budget->bytes.load()||bytes>MaxSourceBytes)return false;s.budget->limit=bytes;return true;}
#endif
}
