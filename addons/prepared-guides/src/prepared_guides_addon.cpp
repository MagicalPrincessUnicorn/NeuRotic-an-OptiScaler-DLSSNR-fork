#include <dlssnr/connections/GpuBudget.h>
// NeuRotic prepared-guide caller. ReShade headers retain their upstream notices.
// ReShade API v20 (supplied archive) documents render_technique AFTER rendering.
// The selected provider's event therefore arms uniforms BEFORE our next technique.
// Provider-name mapping follows DLSS5 Feeder (c) 2026 Jean-Laurent ROUZIES,
// MIT license retained at ../LICENSE-DLSS5-FEEDER.txt; no provider implementation bundled.
#include <windows.h>
#include <d3d10_1.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <reshade.hpp>
#include <dlssnr/PreparedGuideDispatch.h>
#include <dlssnr/PreparedGuideStatus.h>
#include <dlssnr/PreparedGuideStatusV2.h>
#include <dlssnr/connections/ConnectionPolicy.h>
#include <dlssnr/connections/PreparedInputComposition.h>
#include <dlssnr/NativeIdentity.h>
#include <dlssnr/PreparedContentProbe.h>
#include "session_policy.h"
#include "depth_binding.h"
#include "helper_capture.h"
#include "CpuMainlineClient.h"
#include "ExternalFeederAbi.h"
#include <nr/context/PreparedGuideNormalization.h>
#ifdef _WIN64
#include "../flow/PreparedFlowD3D12.h"
#include "SharedGpuClient.h"
#include <dlssnr/NativeGuideProcessor.h>
#endif
#include "../helper/GpuRemoteConsumer.h"
#include <dlssnr/connections/D3D11Connection.h>
#include <dlssnr/connections/D3D10Connection.h>
#include <dlssnr/connections/GlConnection.h>
#include <filesystem>

#include <d3d11.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <limits>
#include <unordered_map>
#include <cstdlib>

extern "C" __declspec(dllexport) const char* NAME="NeuRotic Prepared Guides";
extern "C" __declspec(dllexport) const char* DESCRIPTION="Experimental D3D12 SDR captured depth and optical-flow ingress; private model distrust binding unsupported.";
namespace
{
namespace A=reshade::api;
namespace P=DlssNr::PreparedGuides;
namespace F=Neurotic::Feed::Prepared;
template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
constexpr char Effect[]="NeuRotic_PreparedGuides.fx";
constexpr std::size_t MaxSessions=4;
constexpr std::uint64_t MaxBytes=256ull*1024*1024;
constexpr D3D12_RESOURCE_STATES ShaderRead=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
struct Provider{int mode;const char* file;const char* technique;const char* texture;};
constexpr Provider Providers[]={
    {0,"MotionEstimation.fx","DRME","texMotionVectors"},
    {0,"qUINT_motionvectors.fx","MotionVectors","texMotionVectors"},
    {0,"dh_uber_motion.fx","DH_UBER_MOTION_020","texMotionVectors"},
    {1,"MartysMods_LAUNCHPAD.fx","MartysMods_Launchpad","Deferred::MotionVectorsTex"},
    {2,"vort_Motion.fx","vort_MotionEffects","MotVectTexVort"},
    {3,"lumenite_Kernel.fx","Lumenite_Kernel","Kernel::tFlow"},
    {4,"lumenite_QuantMotion.fx","QuantMotion","QuantMotion::tFlow"}};
struct Session
{
    bool currentMotionReady=false;
    struct CoreLease { HMODULE value=nullptr;~CoreLease(){if(value)FreeLibrary(value);} } coreLease;
    A::effect_runtime* runtime=nullptr;
    std::uint64_t id=0,deviceId=0;
    nrpg::Episode episode;
    P::Status status;std::string lastBlock;unsigned blockReports=0;
    nrpg::CpuMainlineClient mainline;
    A::device* apiDevice=nullptr;std::uint64_t nativeDevice=0;
    nrpg::HelperCapture helper;A::fence sourceFence{};std::uint64_t sourceValue=0;DlssNr::PreparedContent::Probe probe;F::Descriptor probeDescriptor;DlssNr::PreparedContent::Bindings probeBindings{};bool probePending=false;unsigned probeCaptures=0;
    bool helperRoute=false;std::vector<std::uint8_t> pendingUpload;std::array<Ptr<IUnknown>,4> helperAliases;
    Ptr<ID3D12Device> device;
    Ptr<ID3D12CommandQueue> queue;
    Ptr<ID3D12CommandAllocator> allocator;
    Ptr<ID3D12GraphicsCommandList> list;
    Ptr<ID3D12Fence> fence;
    HANDLE event=nullptr;
    HMODULE core=nullptr;
    P::PrepareFn prepare=nullptr;P::RecordFn record=nullptr;P::SealFn seal=nullptr;
    unsigned(__cdecl* queryPolicy)(void*,uint32_t)=nullptr;
    unsigned(__cdecl* querySignals)(void*,uint32_t)=nullptr;
    unsigned(__cdecl* publishSignals)(uint32_t,uint64_t,const void*,uint32_t)=nullptr;
    DlssNr::Connections::SignalStatusWireV2 inputSignals;
    unsigned(__cdecl* claimRoute)(uint32_t,uint32_t,uint64_t)=nullptr;
    unsigned(__cdecl* retireRoute)(uint32_t,uint64_t,uint32_t)=nullptr;
    unsigned(__cdecl* publishStatus)(const void*,uint32_t)=nullptr;
    uint32_t claimedSource=0,activeTransport=0;
    bool externalOwner=false,lastPreparing=false;
    F::Descriptor externalDescriptor{};
    std::uint64_t lastDeliveredCapture=0,lastDeliveredGeneration=0;
    P::StatusV2 statusV2;
    A::effect_technique guide={},producer={};
    const Provider* provider=nullptr;
    std::shared_ptr<DlssNr::Connections::GpuBudget::Reservation> imageBudget;std::shared_ptr<void> legacyCapture;
    std::array<Ptr<ID3D12Resource>,4> images,sources;
    std::array<D3D12_RESOURCE_DESC,4> shapes{};
    std::uint64_t value=0;
    std::uint64_t selectedDepth=0;
    unsigned depthBindingState=0,depthBindingReports=0;
    bool privateComplete=true,sourceReturned=true,inFrame=false,guideSeen=false;
    bool failed=false,depthReady=false,colorRefusalReported=false,builtin=false,consumerReset=false,sourceSaved=false;
#ifdef _WIN64
    std::unique_ptr<nrpg::SharedGpuClient> shared;
    DlssNr::NativeGuides::Processor textureProcessor;
#endif
    Ptr<ID3D12Device> transportDevice;
    std::unique_ptr<nrpg::gpu::GpuRemoteConsumer> remoteConsumer;
    std::uint64_t remoteGeneration=0;unsigned remoteWidth=0,remoteHeight=0;
    std::unique_ptr<DlssNr::Connections::D3D11Connection> connection11;
    std::unique_ptr<DlssNr::Connections::D3D10Connection> connection10;
    std::unique_ptr<DlssNr::Connections::D3D12Connection> connection12;
    std::unique_ptr<DlssNr::Connections::GlConnection> connectionGl;
    std::shared_ptr<DlssNr::Connections::GlContextLease> glContext;
    DlssNr::Connections::PendingFrame pendingTexture;
    nrpg::CpuMainlineClient::GpuOutput textureOutput;
#ifdef _WIN64
    Neurotic::PreparedFlow::Session flow;Neurotic::PreparedFlow::Output flowOutput;
    std::array<A::resource_view,2> flowViews{};bool flowInitialized=false,flowBound=false;unsigned flowWidth=0,flowHeight=0;
    std::uint64_t flowPrevious=0,flowGeneration=0;std::string flowBackend;
#endif
    ~Session()
    {
        // Keep the core lease through final COM release of its observed objects.
        helper.Dispose();if(apiDevice && sourceFence.handle)apiDevice->destroy_fence(sourceFence);
        sources={};images={};list.Reset();allocator.Reset();queue.Reset();fence.Reset();device.Reset();
        if(event)CloseHandle(event);
    }
};
// Intentionally heap-owned: uncertain sessions may outlive runtime/device teardown.
// A quarantined slot stays counted, and the addon/core module leases are pinned.
struct Registry{std::recursive_mutex mutex;std::vector<std::unique_ptr<Session>> sessions;std::vector<A::swapchain*> chains;std::vector<A::effect_runtime*> runtimes;std::atomic<std::uint64_t> next{1};
    std::unordered_map<A::device*,std::shared_ptr<DlssNr::Connections::GlContextLease>> glContexts;
};
Registry& Store(){static auto* value=new Registry;return *value;}
// Separate from the capture lock so the UI never waits for GPU/model work.
struct StatusRegistry{std::mutex mutex;P::Status latest;};
StatusRegistry& StatusStore(){static auto* value=new StatusRegistry;return *value;}
FARPROC Export(HMODULE module,const char* name){auto found=GetProcAddress(module,name);
#ifndef _WIN64
    if(!found){std::string decorated="_";decorated+=name;found=GetProcAddress(module,decorated.c_str());}
#endif
    return found;
}
bool NrEnabled(){
    static const bool enabled=[](){wchar_t value[3]{};return GetEnvironmentVariableW(L"NEUROTIC_PREPARED_GUIDES",value,3)==1&&value[0]==L'1';}();
    HMODULE modules[1024]{};DWORD bytes=0;if(!K32EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&bytes)||bytes>sizeof(modules))return false;
    bool loadedCore=false;
    for(DWORD i=0;i<bytes/sizeof(HMODULE);++i){
        auto eligibility=reinterpret_cast<unsigned(__cdecl*)()>(Export(modules[i],"NeuRotic_PreparedGuidesEnabledV1"));
        bool core=eligibility||Export(modules[i],"NeuRotic_RecordGuidesV1")||Export(modules[i],"NeuRotic_PrepareGuidesV1")||
            Export(modules[i],"OptiScaler_W03_QueryControlledFgBridgeV1");
        if(!core)continue;loadedCore=true;if(!eligibility)return false;HMODULE lease=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(eligibility),&lease))return false;
        auto accepted=eligibility();FreeLibrary(lease);if(!accepted)return false;
    }return loadedCore||enabled;
}
bool TransportDiagnostic(Session& s){return s.apiDevice->get_api()==A::device_api::vulkan&&nrpg::Config(s.runtime,"SharedGpu")=="1"&&nrpg::Config(s.runtime,"TransportOnly")=="1"&&nrpg::Config(s.runtime,"NgxPath").empty();}
void Log(const char* text){reshade::log::message(reshade::log::level::warning,text);}
void Publish(Session& s,P::Stage stage,const char* reason)
{
    auto& status=s.status;status.session=s.id;status.capture=s.externalOwner?s.externalDescriptor.capture:s.episode.capture;
    status.updatedTickMs=GetTickCount64();status.sourceApi=static_cast<unsigned>(s.apiDevice->get_api());
    status.depthReady=s.depthReady?1u:0u;status.motionReady=(s.externalOwner?s.currentMotionReady:s.episode.Armed())?1u:0u;
    status.active=!s.failed&&NrEnabled()?1u:0u;status.stage=stage;P::SetReason(status,reason);
    auto& shared=StatusStore();std::lock_guard lock(shared.mutex);shared.latest=status;
    if(s.publishStatus&&s.claimedSource){auto& v=s.statusV2;v.session=s.id;v.capture=status.capture;v.generation=s.externalOwner?s.externalDescriptor.generation:s.episode.generation;
        v.updatedTickMs=status.updatedTickMs;v.producerBits=sizeof(void*)*8;v.selectedSource=static_cast<DlssNr::Connections::Source>(s.claimedSource);
        v.effectiveTransport=static_cast<DlssNr::Connections::Transport>(s.activeTransport);v.depthOrigin=P::Origin::Observed;v.motionOrigin=P::Origin::Derived;
        v.guideReady=status.depthReady&&status.motionReady;v.outputValid=stage==P::Stage::Delivered;v.restartRequired=s.failed;
        v.inputFrames=status.inputFrames;v.modelCompletions=status.modelCompletions;v.copybackCompletions=status.copybackCompletions;v.stage=stage;
        strncpy_s(v.reason,reason,_TRUNCATE);s.publishStatus(&v,sizeof(v));}
}
void Block(Session& s,const char* reason){s.episode.Drop();Publish(s,P::Stage::Blocked,reason);
    if(s.blockReports<32 && s.lastBlock!=reason){s.lastBlock=reason;++s.blockReports;Log(reason);}}
void Quarantine(Session& s,const char* reason)
{
    if(s.claimedSource&&s.retireRoute)s.retireRoute(s.claimedSource,s.id,0);
    if(!s.failed){s.failed=true;s.episode.Quarantine();Log(reason);Publish(s,P::Stage::Blocked,reason);}
    HMODULE pin=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Quarantine),&pin);
}
bool Healthy(Session& s){if(s.device)return SUCCEEDED(s.device->GetDeviceRemovedReason());
    if(!s.apiDevice)return false;auto api=s.apiDevice->get_api();
    if(api==A::device_api::d3d10)return SUCCEEDED(reinterpret_cast<ID3D10Device*>(s.nativeDevice)->GetDeviceRemovedReason());
    if(api==A::device_api::d3d11)return SUCCEEDED(reinterpret_cast<ID3D11Device*>(s.nativeDevice)->GetDeviceRemovedReason());return true;}
bool Sdr(Session& s)
{
    auto current=s.runtime->get_current_back_buffer();
    for(auto* chain:Store().chains)
        if(chain->get_device()==s.runtime->get_device() && chain->get_current_back_buffer()==current)
            return nrpg::SdrAllowed(static_cast<unsigned>(chain->get_color_space()));
    return false; // Missing swapchain association cannot certify encoded SDR.
}
bool Owns(Session& s,ID3D12DeviceChild* object)
{
    Ptr<ID3D12Device> owner;
    return object && SUCCEEDED(object->GetDevice(IID_PPV_ARGS(&owner))) &&
        DlssNr::NativeIdentity::CompareDevices(owner.Get(),s.device.Get()).equal;
}
void Set(A::effect_runtime* r,const char* name,bool value)
{
    auto u=r->find_uniform_variable(Effect,name);
    if(u.handle)r->set_uniform_value_bool(u,&value,1);
}
void Set(A::effect_runtime* r,const char* name,std::uint32_t value)
{
    auto u=r->find_uniform_variable(Effect,name);
    if(u.handle)r->set_uniform_value_uint(u,&value,1);
}
std::uint32_t Frame(A::effect_runtime* r)
{
    std::uint32_t value=UINT32_MAX;auto u=r->find_uniform_variable(Effect,"NRPG_RuntimeFrame");
    if(u.handle)r->get_uniform_value_uint(u,&value,1);return value;
}
bool HostInterface(A::effect_runtime* r)
{
    for(const char* name:{"NRPG_RuntimeFrame","NRPG_HostEnabled","NRPG_HostFrame","NRPG_Capture",
        "NRPG_PreviousCapture","NRPG_Generation","NRPG_MotionReady","NRPG_DepthReady","NRPG_HistoryValid","NRPG_Reset"})
        if(!r->find_uniform_variable(Effect,name).handle)return false;
    return true;
}
A::resource Texture(A::effect_runtime* r,const char* name,const char* effect=Effect)
{
    auto variable=r->find_texture_variable(effect,name);A::resource_view srv{},srgb{};
    if(variable.handle)r->get_texture_binding(variable,&srv,&srgb);
    return srv.handle?r->get_device()->get_resource_from_view(srv):A::resource{};
}
A::resource_view TextureView(A::effect_runtime* r,const char* name)
{
    auto variable=r->find_texture_variable(Effect,name);A::resource_view srv{},srgb{};
    if(variable.handle)r->get_texture_binding(variable,&srv,&srgb);return srv;
}
bool DepthReady(Session& s)
{
    auto depth=nrpg::PreparedDepth(s.runtime,Effect);
    const unsigned state=depth.handle?1u:2u;
    if(s.depthBindingState!=state && s.depthBindingReports<16){
        ++s.depthBindingReports;
        Log(depth.handle?"NRPG: prepared shader DEPTH binding available; awaiting guide processing":
            "NRPG: prepared shader DEPTH binding unavailable or unsupported; handoff withheld");
    }
    s.depthBindingState=state;
    if(!depth.handle)return false;
    if(s.selectedDepth && s.selectedDepth!=depth.handle)s.episode.Drop();
    s.selectedDepth=depth.handle;return true;
}
void Host(Session& s,bool armed)
{
    auto r=s.runtime;armed=armed&&Sdr(s)&&(NrEnabled()||TransportDiagnostic(s));
    bool depth=armed && DepthReady(s);
    s.depthReady=depth;
    Set(r,"NRPG_HostEnabled",armed);Set(r,"NRPG_HostFrame",Frame(r));
    Set(r,"NRPG_Capture",s.episode.capture);Set(r,"NRPG_PreviousCapture",s.episode.previous);
    Set(r,"NRPG_Generation",s.episode.generation);
    Set(r,"NRPG_MotionReady",armed);Set(r,"NRPG_DepthReady",depth);
    Set(r,"NRPG_HistoryValid",armed && s.episode.History() && !s.consumerReset);Set(r,"NRPG_Reset",!s.episode.History() || s.consumerReset);
}
void ColorHost(Session& s,bool armed,bool processed=false)
{
    constexpr char effect[]="NeuRotic_SourceGuidedColor.fx";
    auto boolean=s.runtime->find_uniform_variable(effect,"NRSG_HostEnabled");if(boolean.handle)s.runtime->set_uniform_value_bool(boolean,&armed,1);
    for(auto entry:std::array<std::pair<const char*,std::uint32_t>,5>{{{"NRSG_HostFrame",Frame(s.runtime)}, {"NRSG_Capture",s.episode.capture},{"NRSG_Generation",s.episode.generation},
        {"NRSG_ProcessedCapture",processed?s.episode.capture:0},{"NRSG_ProcessedGeneration",processed?s.episode.generation:0}}}){
        auto uniform=s.runtime->find_uniform_variable(effect,entry.first);if(uniform.handle)s.runtime->set_uniform_value_uint(uniform,&entry.second,1);}
}
bool BindCore(Session& s)
{
    if(s.core)return true;
    HMODULE modules[1024]{};DWORD bytes=0;
    if(!K32EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&bytes) || bytes>sizeof(modules))return false;
    for(DWORD i=0;i<bytes/sizeof(HMODULE);++i)
    {
        auto prepare=reinterpret_cast<P::PrepareFn>(GetProcAddress(modules[i],"NeuRotic_PrepareGuidesV1"));
        auto record=reinterpret_cast<P::RecordFn>(GetProcAddress(modules[i],"NeuRotic_RecordGuidesV1"));
        auto seal=reinterpret_cast<P::SealFn>(GetProcAddress(modules[i],"NeuRotic_SealGuidesV1"));
        if(!prepare || !record || !seal)continue;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(prepare),&s.coreLease.value))return false;
        s.core=s.coreLease.value;s.prepare=prepare;s.record=record;s.seal=seal;
        s.queryPolicy=reinterpret_cast<decltype(s.queryPolicy)>(GetProcAddress(s.core,"NeuRotic_QueryConnectionPolicyV1"));
        s.querySignals=reinterpret_cast<decltype(s.querySignals)>(GetProcAddress(s.core,"NeuRotic_QuerySignalPolicyV2"));
        s.publishSignals=reinterpret_cast<decltype(s.publishSignals)>(GetProcAddress(s.core,"NeuRotic_PublishInputSignalsV2"));
        s.claimRoute=reinterpret_cast<decltype(s.claimRoute)>(GetProcAddress(s.core,"NeuRotic_ClaimPreparedConnectionV1"));
        s.retireRoute=reinterpret_cast<decltype(s.retireRoute)>(GetProcAddress(s.core,"NeuRotic_RetirePreparedConnectionV1"));
        s.publishStatus=reinterpret_cast<decltype(s.publishStatus)>(GetProcAddress(s.core,"NeuRotic_PublishPreparedGuidesV2"));
        return true;
    }
    return false;
}
Session* Get(A::effect_runtime* r)
{
    auto& store=Store();
    for(auto it=store.sessions.begin();it!=store.sessions.end();++it)if((*it)->runtime==r)
    {
        auto& s=**it;
        if(r->get_device()==s.apiDevice && r->get_device()->get_native()==s.nativeDevice)return &s;
        if(!s.failed && nrpg::MayReuse(s.privateComplete,s.sourceReturned,Healthy(s)))store.sessions.erase(it);
        else {s.runtime=nullptr;Quarantine(s,"NRPG: changed device with uncertain work; retained device incarnation");}
        break;
    }
    auto api=r->get_device()->get_api();
    if((api!=A::device_api::d3d10&&api!=A::device_api::d3d11&&api!=A::device_api::d3d12&&api!=A::device_api::vulkan&&api!=A::device_api::opengl)||store.sessions.size()>=MaxSessions)return nullptr;
    auto session=std::make_unique<Session>();session->runtime=r;session->id=store.next++;session->deviceId=store.next++;
    session->apiDevice=r->get_device();session->nativeDevice=session->apiDevice->get_native();
    if(api==A::device_api::opengl){auto found=store.glContexts.find(session->apiDevice);if(found!=store.glContexts.end())session->glContext=found->second;}
    if(api==A::device_api::d3d12)session->device=reinterpret_cast<ID3D12Device*>(session->nativeDevice);
    if(!session->nativeDevice)return nullptr;session->helperRoute=true;
#ifndef _WIN64
    session->helperRoute=true;
#endif
    auto* result=session.get();store.sessions.push_back(std::move(session));return result;
}
bool Setup(Session& s)
{
    if(s.list && s.queue && s.allocator && s.fence && s.event)return true;
    // Partial creation issued no GPU work; release it before a bounded retry.
    s.list.Reset();s.queue.Reset();s.allocator.Reset();s.fence.Reset();
    if(s.event){CloseHandle(s.event);s.event=nullptr;}
    D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    if(FAILED(s.device->CreateCommandQueue(&desc,IID_PPV_ARGS(&s.queue))) ||
        FAILED(s.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&s.allocator))) ||
        FAILED(s.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator.Get(),nullptr,IID_PPV_ARGS(&s.list))) ||
        FAILED(s.list->Close()) || FAILED(s.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s.fence))))return false;
    s.event=CreateEventW(nullptr,FALSE,FALSE,nullptr);return s.event!=nullptr;
}
bool Wait(Session& s)
{
    auto complete=s.fence->GetCompletedValue();
    if(complete==UINT64_MAX || !Healthy(s))return false;
    if(complete<s.value)
    {
        if(FAILED(s.fence->SetEventOnCompletion(s.value,s.event)) ||
            WaitForSingleObject(s.event,5000)!=WAIT_OBJECT_0)return false;
        complete=s.fence->GetCompletedValue();
    }
    s.privateComplete=complete!=UINT64_MAX && complete>=s.value && Healthy(s);
    return s.privateComplete;
}
void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* image,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to)
{
    if(from==to)return;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={image,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list->ResourceBarrier(1,&barrier);
}
bool Shape(const D3D12_RESOURCE_DESC& d,unsigned w,unsigned h)
{return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.Width==w && d.Height==h &&
    d.DepthOrArraySize==1 && d.MipLevels==1 && d.SampleDesc.Count==1 && d.SampleDesc.Quality==0;}
bool Allocate(Session& s,const std::array<ID3D12Resource*,4>& input)
{
    std::array<D3D12_RESOURCE_DESC,4> descriptions{};
    for(std::size_t i=0;i<4;++i)
    {
        if(!Owns(s,input[i]))return false;
        descriptions[i]=input[i]->GetDesc();
    }
    unsigned w=static_cast<unsigned>(descriptions[0].Width),h=descriptions[0].Height;
    if(!DlssNr::Connections::GpuBudget::AdmitRaster(w,h))return false;
    for(const auto& d:descriptions)if(!Shape(d,w,h))return false;
    // No BGRA reinterpretation, typeless alias, PQ or linear-HDR guess.
    if(descriptions[0].Format!=DXGI_FORMAT_R8G8B8A8_UNORM && descriptions[0].Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;
    if(descriptions[1].Format!=DXGI_FORMAT_R32_FLOAT ||
       (descriptions[2].Format!=DXGI_FORMAT_R16G16_FLOAT && descriptions[2].Format!=DXGI_FORMAT_R32G32_FLOAT) ||
        descriptions[3].Format!=DXGI_FORMAT_R8_UNORM)return false;
    bool same=s.images[0]!=nullptr;
    for(std::size_t i=0;i<4;++i)same=same && s.shapes[i].Width==descriptions[i].Width && s.shapes[i].Height==descriptions[i].Height && s.shapes[i].Format==descriptions[i].Format;
    if(same)return true;
    if(!nrpg::MayReuse(s.privateComplete,s.sourceReturned,Healthy(s)))return false;
    std::uint64_t bytes=0;
    for(std::size_t i=0;i<4;++i){auto desc=descriptions[i];desc.Flags=i==0?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        auto needed=DlssNr::Connections::GpuBudget::TextureBytes(s.device.Get(),desc);if(!needed||needed>MaxBytes-bytes)return false;bytes+=needed;}
    auto budget=DlssNr::Connections::GpuBudget::Reserve(bytes);if(!budget)return false;
    std::array<Ptr<ID3D12Resource>,4> fresh;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    for(std::size_t i=0;i<4;++i)
    {
        auto desc=descriptions[i];desc.Flags=i==0?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if(FAILED(s.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
            i==0?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            nullptr,IID_PPV_ARGS(&fresh[i]))))return false;
    }
    bool changed=s.images[0]!=nullptr;
    s.images=std::move(fresh);s.imageBudget=std::move(budget);s.shapes=descriptions;
    // The already-rendered guide shader used the old generation. Drop this
    // output and let the NEXT host update publish the new episode before use.
    if(changed){s.episode.Drop();return false;}
    return true;
}
bool SourceIdle(Session& s,A::command_list* list)
{
    auto queue=s.runtime->get_command_queue();
    if(!queue || list!=queue->get_immediate_command_list() || !Healthy(s))return false;
    // Source-only conservative baseline: never used to wait for uncertain model work.
    queue->flush_immediate_command_list();
    if(!Healthy(s))return false;
    if(!s.sourceFence.handle && !s.apiDevice->create_fence(0,A::fence_flags::none,&s.sourceFence))return false;
    auto value=++s.sourceValue;if(!queue->signal(s.sourceFence,value) || !s.apiDevice->wait(s.sourceFence,value,5000000000ull))return false;
    auto completed=s.apiDevice->get_completed_fence_value(s.sourceFence);return completed!=UINT64_MAX&&completed>=value&&Healthy(s);
}
#ifdef _WIN64
bool ReturnFlow(Session& s,A::command_list* commands)
{
    if(!s.flowBound)return true;
    for(auto image:{s.flowOutput.motion.Get(),s.flowOutput.distrust.Get()})commands->barrier(A::resource{reinterpret_cast<std::uint64_t>(image)},A::resource_usage::shader_resource,A::resource_usage::shader_resource_non_pixel);
    if(!SourceIdle(s,commands)){Quarantine(s,"NRPG: builtin flow reader completion unknown; output retained");return false;}
    std::string reason;Neurotic::PreparedFlow::Completion reader;
    reader.fence=reinterpret_cast<ID3D12Fence*>(s.sourceFence.handle);reader.value=s.sourceValue;
    if(!s.flow.AddReader(reader,reason)){Quarantine(s,"NRPG: builtin flow reader registration refused; output retained");return false;}
    s.runtime->update_texture_bindings("NRPG_BUILTIN_MOTION",{},{});s.runtime->update_texture_bindings("NRPG_BUILTIN_DISTRUST",{},{});
    for(auto view:s.flowViews)if(view.handle)s.apiDevice->destroy_resource_view(view);s.flowViews={};s.flowOutput={};s.flowBound=false;
    if(!s.flow.ReturnOutput(reason)){Quarantine(s,"NRPG: builtin flow lease retirement refused; session retained");return false;}return true;
}
bool BuiltinFlow(Session& s,A::command_list* commands,A::resource_view rtv)
{
    namespace OF=Neurotic::PreparedFlow;if(s.apiDevice->get_api()!=A::device_api::d3d12||!Sdr(s)||!DepthReady(s)||!Setup(s))return false;
    auto source=s.apiDevice->get_resource_from_view(rtv);auto* native=reinterpret_cast<ID3D12Resource*>(source.handle);if(!Owns(s,native))return false;
    auto shape=native->GetDesc();unsigned w=unsigned(shape.Width),h=shape.Height;if(!Shape(shape,w,h)||!DlssNr::Connections::GpuBudget::AdmitRaster(w,h,true))return false;
    if(s.flowInitialized&&(s.flowWidth!=w||s.flowHeight!=h||s.flowBackend!=nrpg::Config(s.runtime,"FlowBackend"))){std::string reason;if(!s.flow.Close(reason)){Quarantine(s,"NRPG: flow resize could not retire actual producer work");return false;}s.flowInitialized=false;s.flowPrevious=0;s.flowGeneration=0;s.episode.Drop();return false;}
    std::string reason;if(!s.flowInitialized){OF::Options options;options.width=w;options.height=h;auto backend=nrpg::Config(s.runtime,"FlowBackend");
        options.requested=backend=="nvidia"?OF::Backend::Nvidia:backend=="auto"?OF::Backend::Auto:OF::Backend::Software;
        if(!s.flow.Initialize(s.device.Get(),options,reason)){Log(reason.c_str());std::string retire;if(!s.flow.Close(retire))Quarantine(s,"NRPG: failed flow initialization retained uncertain backend work");return false;}s.flowInitialized=true;s.flowWidth=w;s.flowHeight=h;s.flowBackend=backend;}
    if(!SourceIdle(s,commands)){Quarantine(s,"NRPG: flow source capture completion unknown");return false;}
    struct CapturedFrame{std::shared_ptr<DlssNr::Connections::GpuBudget::Reservation> budget;Ptr<ID3D12Resource> texture;};
    auto frame=std::make_shared<CapturedFrame>();auto description=shape;description.Flags=D3D12_RESOURCE_FLAG_NONE;description.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
    auto allocation=s.device->GetResourceAllocationInfo(0,1,&description).SizeInBytes;if(allocation==UINT64_MAX||allocation>MaxBytes)return false;frame->budget=DlssNr::Connections::GpuBudget::Reserve(allocation);if(!frame->budget)return false;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    if(FAILED(s.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(frame->texture.GetAddressOf()))))return false;
    if(FAILED(s.allocator->Reset())||FAILED(s.list->Reset(s.allocator.Get(),nullptr)))return false;
    s.sources[0]=native;Barrier(s.list.Get(),native,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Barrier(s.list.Get(),frame->texture.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);s.list->CopyResource(frame->texture.Get(),native);
    Barrier(s.list.Get(),frame->texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Barrier(s.list.Get(),native,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    if(FAILED(s.list->Close())){Quarantine(s,"NRPG: flow capture recording uncertain");return false;}
    ID3D12CommandList* lists[]={s.list.Get()};s.privateComplete=false;s.queue->ExecuteCommandLists(1,lists);
    if(FAILED(s.queue->Signal(s.fence.Get(),++s.value))||!Wait(s)){
        // Capture allocation may still be referenced. Preserve it independently.
        s.images[0]=frame->texture;s.legacyCapture=frame;Quarantine(s,"NRPG: flow source copy completion unknown; allocation retained");return false;}
    s.sources={};OF::Input input;input.color=frame->texture;input.ownership=frame;input.producer={s.fence,s.value};
    bool reset=s.flowGeneration!=s.episode.generation;input.reset=reset;input.pair={s.episode.capture,reset?0:s.flowPrevious,s.id,s.episode.generation};
    if(!s.flow.Submit(input,reason)){if(!reason.empty())Log(reason.c_str());return false;}s.flowPrevious=s.episode.capture;s.flowGeneration=s.episode.generation;
    auto deadline=GetTickCount64()+5000;while(!s.flow.Poll(s.flowOutput,reason)){if(GetTickCount64()>=deadline){Quarantine(s,"NRPG: flow completion pending at deadline; input/output session retained");return false;}Sleep(1);}
    if(!s.flowOutput.producer.Ready()||s.flowOutput.pair.capture!=s.episode.capture||s.flowOutput.pair.generation!=s.episode.generation){Quarantine(s,"NRPG: completed flow identity mismatch; never relabelled");return false;}
    std::array<Ptr<ID3D12Resource>,2> resources={s.flowOutput.motion,s.flowOutput.distrust};
    for(unsigned i=0;i<2;++i){auto image=A::resource{reinterpret_cast<std::uint64_t>(resources[i].Get())};auto format=i?A::format::r8_unorm:A::format::r16g16_float;
        if(!s.apiDevice->create_resource_view(image,A::resource_usage::shader_resource,A::resource_view_desc(format),&s.flowViews[i])){Quarantine(s,"NRPG: flow view allocation unavailable; completed session retained");return false;}
        commands->barrier(image,A::resource_usage::shader_resource_non_pixel,A::resource_usage::shader_resource);}
    s.runtime->update_texture_bindings("NRPG_BUILTIN_MOTION",s.flowViews[0],s.flowViews[0]);s.runtime->update_texture_bindings("NRPG_BUILTIN_DISTRUST",s.flowViews[1],s.flowViews[1]);
    s.flowBound=true;s.consumerReset=s.flowOutput.explicitReset||s.flowOutput.warmup||s.flowOutput.sceneCut;s.episode.Provider();Host(s,true);return true;
}
#endif
F::Descriptor Descriptor(Session& s,unsigned w,unsigned h)
{
    F::Descriptor descriptor;descriptor.producer=0x4e52504700000001ull;descriptor.session=s.id;descriptor.stream=s.id;descriptor.device=s.deviceId;
    descriptor.generation=s.episode.generation;descriptor.capture=s.episode.capture;descriptor.previousCapture=s.episode.History()&&!s.consumerReset?s.episode.previous:0;descriptor.maskCapture=descriptor.capture;
    descriptor.color=descriptor.depth=descriptor.motion=descriptor.distrust={w,h,0,0,w,h};descriptor.flags=F::ZeroJitterPolicy|F::HudIncluded|F::HasDistrust|(s.episode.History()&&!s.consumerReset?0u:F::Reset);
    auto api=s.apiDevice->get_api();descriptor.sourceApi=api==A::device_api::d3d10?F::C::GraphicsApi::D3D10:api==A::device_api::d3d11?F::C::GraphicsApi::D3D11:api==A::device_api::vulkan?F::C::GraphicsApi::Vulkan:api==A::device_api::opengl?F::C::GraphicsApi::OpenGL:F::C::GraphicsApi::D3D12;
    char reversed[16]{};descriptor.depthReversed=1;
    if(s.runtime->get_preprocessor_definition_for_effect(Effect,"RESHADE_DEPTH_INPUT_IS_REVERSED",reversed)||s.runtime->get_preprocessor_definition("RESHADE_DEPTH_INPUT_IS_REVERSED",reversed))descriptor.depthReversed=std::strtoul(reversed,nullptr,10)!=0;
    return descriptor;
}
void ProbeObservation(Session& s,DlssNr::PreparedContent::Stage stage,std::span<const std::byte> bytes)
{
    namespace PC=DlssNr::PreparedContent;auto w=s.probeDescriptor.color.width,h=s.probeDescriptor.color.height;
    PC::Observation observation;observation.identity=PC::IdentityOf(s.probeDescriptor);observation.stage=stage;observation.resource=s.probeBindings[static_cast<unsigned>(stage)];observation.maskCapture=s.probeDescriptor.maskCapture;
    observation.view={bytes,std::uint64_t(w)*4,w,h,4,1,true};auto result=s.probe.Observe(observation);
    char line[256]{};std::snprintf(line,sizeof(line),"NRPG content capture=%llu stage=%u accepted=%u code=%u digest=%llx; displayObserved=0",s.probeDescriptor.capture,unsigned(stage),unsigned(result.accepted),unsigned(result.code),result.digest);Log(line);
}
bool DeferredFlow(const Session& s){
    const auto api=s.apiDevice->get_api();
    return s.builtin&&nrpg::Config(s.runtime,"NgxPath").empty()&&
        (api==A::device_api::d3d10||api==A::device_api::d3d11||api==A::device_api::d3d12||api==A::device_api::opengl);
}
template<class Connection,class Frame>
bool RunConnection(Session& s,Connection& connection,const Frame& frame,std::string& reason){
    s.lastPreparing=false;s.currentMotionReady=false;
    const uint32_t selected=s.claimedSource?s.claimedSource:3;
#ifdef _WIN64
    const bool newlyClaimed=s.claimedSource==0;
    // Claim before Capture can allocate private pools or submit source commands.
    // A matching source with a different session token must also be excluded.
    if(!s.claimRoute||!s.claimRoute(selected,1,s.id)){
        reason="Another renderer source owns the session or GPU transport was not selected";Block(s,reason.c_str());return true;
    }
#endif
    s.claimedSource=selected;s.activeTransport=1;
    bool captured=false,mixedAttempt=false;
    if constexpr(std::is_same_v<Frame,DlssNr::Connections::D3D12ObservedFrame>){
        namespace C=DlssNr::Connections;C::SignalPolicyWireV2 wire;
        if(s.querySignals&&s.querySignals(&wire,sizeof(wire))){
            mixedAttempt=true;
            C::Mixed::Policy policy{static_cast<C::Source>(wire.depth),static_cast<C::Source>(wire.motion),wire.strict!=0,wire.revision,static_cast<C::Source>(wire.color)};
            // Raw depth is captured by this registered acquisition owner;
            // supplied motion is an external effect, or explicitly derived here.
            captured=C::CapturePreparedInputs(connection,frame,static_cast<C::Source>(selected),
                static_cast<C::Source>(selected),s.externalOwner?C::Source::External:C::Source::ReShade,
                policy,s.pendingTexture,s.inputSignals,reason);
            if(!captured&&!policy.strict&&!connection.RequiresRestart()){
                // A failed mix may retain the established coherent whole-frame
                // path. It cannot borrow individual rejected supplemental data.
                captured=connection.Capture(frame,s.pendingTexture,reason);
                if(captured){s.inputSignals.color=selected;s.inputSignals.colorOrigin=uint32_t(frame.description.colorOrigin)+1;
                    s.inputSignals.colorReason=uint32_t(C::Mixed::Reason::PreferenceFallback);s.inputSignals.depth=selected;s.inputSignals.motion=frame.deriveMotion?2u:selected;
                    s.inputSignals.depthOrigin=uint32_t(frame.description.depthOrigin)+1;
                    s.inputSignals.motionOrigin=uint32_t(frame.deriveMotion?Neurotic::Feed::Prepared::C::SourceClass::Derived:frame.description.motionOrigin)+1;
                    s.inputSignals.depthReason=s.inputSignals.motionReason=uint32_t(C::Mixed::Reason::PreferenceFallback);}
            }
            if(s.publishSignals)s.publishSignals(selected,s.id,&s.inputSignals,sizeof(s.inputSignals));
        }else captured=connection.Capture(frame,s.pendingTexture,reason);
    }else captured=connection.Capture(frame,s.pendingTexture,reason);
    if(!captured){
        if(connection.RequiresRestart()){Quarantine(s,reason.c_str());return true;}
#ifdef _WIN64
        if(newlyClaimed){
            // No Processor call happened under this new claim. Retire all clean
            // adapter pools before allowing another module to become the owner.
            s.connection11.reset();s.connection10.reset();s.connection12.reset();s.connectionGl.reset();
            if(!s.retireRoute||!s.retireRoute(selected,s.id,1)){Quarantine(s,"New GPU owner claim could not retire cleanly");return true;}
            s.claimedSource=0;s.activeTransport=0;
        }
#endif
        if(mixedAttempt){Block(s,reason.c_str());return true;}
        return false;
    }
    s.currentMotionReady=!s.pendingTexture.textures.deriveMotion;
    s.statusV2.sourceApi=static_cast<uint32_t>(frame.description.sourceApi);s.statusV2.creationReady=1;
    s.statusV2.captureWidth=frame.description.color.width;s.statusV2.captureHeight=frame.description.color.height;
    ++s.status.inputFrames;
    bool delivered=false,preparing=false,unsafe=false;
#ifdef _WIN64
    const auto result=s.textureProcessor.ProcessTextures(s.pendingTexture.textures,s.textureOutput,reason);
    delivered=result==DlssNr::NativeGuides::Outcome::Delivered;preparing=result==DlssNr::NativeGuides::Outcome::Preparing;unsafe=result==DlssNr::NativeGuides::Outcome::Unsafe;
#else
    if(s.remoteConsumer&&(s.remoteGeneration!=frame.description.generation||s.remoteWidth!=frame.description.color.width||s.remoteHeight!=frame.description.color.height)){
        if(s.remoteConsumer->RequiresRestart()){Quarantine(s,"GPU helper generation cannot retire");return true;}s.remoteConsumer.reset();
    }
    if(!s.remoteConsumer){
        HMODULE module=nullptr;wchar_t path[32768]{};
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Store),&module)){
            const auto size=GetModuleFileNameW(module,path,32768);FreeLibrary(module);
            if(size&&size<32768){nrpg::gpu::ManagedHostSpec spec;const auto directory=std::filesystem::path(path).parent_path()/L"NeuRotic.GpuHost";
                spec.executable=directory/L"NeuRotic.PreparedGpuHost.exe";spec.coreModule=directory/L"OptiScaler.dll";
                s.remoteConsumer=std::make_unique<nrpg::gpu::GpuRemoteConsumer>(std::move(spec));s.remoteGeneration=frame.description.generation;s.remoteWidth=frame.description.color.width;s.remoteHeight=frame.description.color.height;}
        }
    }
    if(s.remoteConsumer){delivered=s.remoteConsumer->Process(s.pendingTexture.textures,s.textureOutput,reason);unsafe=s.remoteConsumer->RequiresRestart();}
    else reason="Managed 64-bit GPU host location unavailable";
#endif
    if(unsafe){Quarantine(s,reason.c_str());return true;}
    if(!delivered){
        std::string retired;
        if(!connection.Cancel(s.pendingTexture,retired)){Quarantine(s,retired.c_str());return true;}
        s.lastPreparing=preparing;s.episode.history=false;s.episode.previous=s.episode.previousFrame=0;
        Publish(s,preparing?P::Stage::Waiting:P::Stage::Blocked,reason.c_str());return true;
    }
    s.currentMotionReady=true;++s.status.modelCompletions;
    s.statusV2.outputWidth=frame.description.color.width;s.statusV2.outputHeight=frame.description.color.height;
    DlssNr::Connections::CopybackReceipt receipt;
    if(!connection.Finish(s.pendingTexture,s.textureOutput,receipt,reason)||!receipt.completed||
       receipt.capture!=frame.description.capture||receipt.generation!=frame.description.generation){Quarantine(s,reason.c_str());return true;}
#ifndef _WIN64
    if(!s.remoteConsumer->ReturnAfterCopyback(true,receipt.capture,receipt.generation,reason)){Quarantine(s,reason.c_str());return true;}
#endif
    s.episode.Provider();s.episode.Delivered();++s.status.copybackCompletions;
    s.lastDeliveredCapture=receipt.capture;s.lastDeliveredGeneration=receipt.generation;
    if constexpr(std::is_same_v<Frame,DlssNr::Connections::D3D12ObservedFrame>){
        if(s.publishSignals&&s.inputSignals.capture==receipt.capture){
            s.inputSignals.consumed=1;
            if(s.inputSignals.motionReason==uint32_t(DlssNr::Connections::Mixed::Reason::NeedsActivation))
                s.inputSignals.motionReason=uint32_t(DlssNr::Connections::Mixed::Reason::Ready);
            s.publishSignals(selected,s.id,&s.inputSignals,sizeof(s.inputSignals));
        }
    }
    Publish(s,P::Stage::Delivered,"Owned GPU depth and motion reached NR; exact capture copied back; display not observed");return true;
}
bool FeedApiGpu(Session& s,A::command_list* commands,const std::array<A::resource,4>& source,std::string& reason,const F::Descriptor* external=nullptr,bool derive=false){
    #ifndef _WIN64
    DlssNr::Connections::GpuBudget::ClampLimit(512ull*1024*1024);
#endif
    const auto api=s.apiDevice->get_api();
    if(!nrpg::Config(s.runtime,"NgxPath").empty()||(api!=A::device_api::d3d10&&api!=A::device_api::d3d11&&api!=A::device_api::d3d12&&api!=A::device_api::opengl))return false;
    if(!source[0].handle||!source[1].handle)return false;
#ifdef _WIN64
    if(!BindCore(s)){reason="Loaded core prepared ABI missing";return false;}
    DlssNr::Connections::PolicyWire policy;
    const auto selected=s.claimedSource?s.claimedSource:3u;
    if(!s.queryPolicy||!s.queryPolicy(&policy,sizeof(policy))||policy.unsafe||policy.transport==2||
       (policy.source&&policy.source!=selected)||(policy.claimedSource&&policy.claimedSource!=selected))return false;
#endif
    std::uint64_t luid=0;if(!s.apiDevice->get_property(A::device_properties::adapter_luid,&luid)||!luid){
        if(api!=A::device_api::opengl){reason="Actual source adapter LUID unavailable";return false;}
        Ptr<IDXGIFactory6> factory;Ptr<IDXGIAdapter1> candidate;DXGI_ADAPTER_DESC1 desc{};
        if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&candidate)))||FAILED(candidate->GetDesc1(&desc))){reason="Bounded OpenGL candidate adapter unavailable";return false;}
        // This is a candidate renderer LUID. GlConnection must prove actual
        // private pixel transfer before commands reference a game object.
        std::memcpy(&luid,&desc.AdapterLuid,8);
    }
    ID3D12Device* renderer=nullptr;
    if(api==A::device_api::d3d12)renderer=s.device.Get();
#ifdef _WIN64
    else renderer=s.textureProcessor.SharedDevice(luid,reason);
#else
    else {
        if(!s.transportDevice){Ptr<IDXGIFactory4> factory;Ptr<IDXGIAdapter1> adapter;LUID actual{};std::memcpy(&actual,&luid,8);
            if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapterByLuid(actual,IID_PPV_ARGS(&adapter)))||
               FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&s.transportDevice)))){reason="Same-adapter local D3D12 GPU transport unavailable";return false;}}
        renderer=s.transportDevice.Get();
    }
#endif
    if(!renderer)return false;
    if(!SourceIdle(s,commands)){Quarantine(s,"GPU capture source completion unknown; CPU fallback suppressed");return true;}
    auto shape=s.apiDevice->get_resource_desc(source[0]);auto descriptor=external?*external:Descriptor(s,shape.texture.width,shape.texture.height);
    const bool deriveMotion=external?derive:s.builtin;
    if(api==A::device_api::opengl){
        if(!s.glContext||(!s.glContext->IsCurrent()&&!s.glContext->TransferToCurrentThread())){reason="OpenGL context creation/lifetime observation unavailable";return false;}
        if(!s.connectionGl)s.connectionGl=std::make_unique<DlssNr::Connections::GlConnection>(renderer);
        DlssNr::Connections::GlObservedFrame frame;frame.description=descriptor;frame.context=s.glContext;frame.lease=s.glContext;
        frame.color=frame.outputTarget=source[0].handle;frame.depth=source[1].handle;frame.depthIsR32Float=true;
        if(!external)frame.description.rasterOrigin=F::C::RasterOrigin::BottomLeft;
        frame.deriveMotion=deriveMotion;frame.motion=source[2].handle;frame.distrust=source[3].handle;
        return RunConnection(s,*s.connectionGl,frame,reason);
    }
    // The callback excludes source mutation; COM aliases retain its generation
    // through the adapter's own capture/copyback fences.
    auto aliases=std::make_shared<std::array<Ptr<IUnknown>,4>>();
    for(unsigned i=0;i<4;++i)if(source[i].handle)(*aliases)[i]=reinterpret_cast<IUnknown*>(source[i].handle);
    if(api==A::device_api::d3d12){
        if(!s.connection12)s.connection12=std::make_unique<DlssNr::Connections::D3D12Connection>(renderer);
        DlssNr::Connections::D3D12ObservedFrame frame;frame.description=descriptor;frame.lease=aliases;frame.deriveMotion=deriveMotion;
        if(FAILED((*aliases)[0].As(&frame.color))||FAILED((*aliases)[1].As(&frame.depth))){reason="D3D12 source texture interfaces unavailable";return false;}
        if(!deriveMotion&&(FAILED((*aliases)[2].As(&frame.motion))||FAILED((*aliases)[3].As(&frame.distrust)))){reason="D3D12 supplied guide textures unavailable";return false;}
        frame.colorState=D3D12_RESOURCE_STATE_RENDER_TARGET;frame.depthState=frame.motionState=frame.distrustState=ShaderRead;frame.statesKnown=true;
        frame.sourceQueue=reinterpret_cast<ID3D12CommandQueue*>(s.runtime->get_command_queue()->get_native());
        frame.producer=reinterpret_cast<ID3D12Fence*>(s.sourceFence.handle);frame.producerValue=s.sourceValue;
        return RunConnection(s,*s.connection12,frame,reason);
    }
    if(api==A::device_api::d3d11){
        if(!s.connection11)s.connection11=std::make_unique<DlssNr::Connections::D3D11Connection>(renderer);
        DlssNr::Connections::D3D11ObservedFrame frame;frame.description=descriptor;frame.lease=aliases;
        reinterpret_cast<ID3D11Device*>(s.nativeDevice)->GetImmediateContext(&frame.context);
        if(FAILED((*aliases)[0].As(&frame.color))||FAILED((*aliases)[1].As(&frame.depth))){reason="D3D11 source texture interfaces unavailable";return false;}
        frame.deriveMotion=deriveMotion;
        if(!frame.deriveMotion&&(FAILED((*aliases)[2].As(&frame.motion))||FAILED((*aliases)[3].As(&frame.distrust)))){reason="D3D11 guide textures unavailable";return false;}
        frame.outputTarget=frame.color;return RunConnection(s,*s.connection11,frame,reason);
    }
    if(!s.connection10)s.connection10=std::make_unique<DlssNr::Connections::D3D10Connection>(renderer);
    DlssNr::Connections::D3D10ObservedFrame frame;frame.description=descriptor;frame.lease=aliases;
    frame.device=reinterpret_cast<ID3D10Device*>(s.nativeDevice);
    if(FAILED((*aliases)[0].As(&frame.color))||FAILED((*aliases)[1].As(&frame.depth))){reason="D3D10 source texture interfaces unavailable";return false;}
    frame.deriveMotion=deriveMotion;
    if(!frame.deriveMotion&&(FAILED((*aliases)[2].As(&frame.motion))||FAILED((*aliases)[3].As(&frame.distrust)))){reason="D3D10 guide textures unavailable";return false;}
    frame.outputTarget=frame.color;return RunConnection(s,*s.connection10,frame,reason);
}
void FeedHelper(Session& s,A::command_list* commands,A::resource_view rtv)
{
    std::array<A::resource_view,4> views={rtv,TextureView(s.runtime,"NRPG_RawDepth"),TextureView(s.runtime,"NRPG_Motion"),TextureView(s.runtime,"NRPG_Distrust")};
    std::array<A::resource,4> source{};for(unsigned i=0;i<4;++i)if(views[i].handle)source[i]=s.apiDevice->get_resource_from_view(views[i]);
    std::string reason;
    if(FeedApiGpu(s,commands,source,reason)||s.failed)return;
    if(DeferredFlow(s)){Block(s,reason.empty()?"Owned GPU flow transport unavailable":reason.c_str());return;}
#ifdef _WIN64
    if(s.apiDevice->get_api()==A::device_api::vulkan&&nrpg::Config(s.runtime,"SharedGpu")=="1"&&nrpg::Config(s.runtime,"NgxPath").empty()){
        const bool diagnostic=TransportDiagnostic(s);
        if(!BindCore(s)||!s.queryPolicy){Block(s,"Vulkan GPU policy unavailable");return;}
        DlssNr::Connections::PolicyWire policy;
        if(!s.queryPolicy(&policy,sizeof(policy))||policy.unsafe||policy.transport==2||
           (policy.source!=0&&policy.source!=3)){Block(s,"Vulkan GPU source/transport is not selected");return;}
        if(!diagnostic){if(!s.claimRoute||!s.claimRoute(3,1,s.id)){Block(s,"Vulkan GPU owner claim refused");return;}s.claimedSource=3;s.activeTransport=1;}
        auto shape=s.apiDevice->get_resource_desc(source[0]);auto descriptor=Descriptor(s,shape.texture.width,shape.texture.height);
        if(commands!=s.runtime->get_command_queue()->get_immediate_command_list()||!Healthy(s)){s.episode.Drop();return;}
        if(!s.shared)s.shared=std::make_unique<nrpg::SharedGpuClient>();
        if(s.shared->Process(s.runtime->get_command_queue(),descriptor,source,reason,nrpg::Config(s.runtime,"TransportOnly")=="1")){
            if(s.shared->CompletedCapture()!=descriptor.capture||!s.shared->CompletionValue()){Quarantine(s,"NRPG: shared completed capture mismatch");return;}
            s.episode.Delivered();++s.status.inputFrames;++s.status.copybackCompletions;
            if(!TransportDiagnostic(s))++s.status.modelCompletions;
            Publish(s,P::Stage::Delivered,TransportDiagnostic(s)?"Shared transport copyback completed; no model evaluation":"Shared mainline output copied back; display not observed");return;
        }
        if(s.shared->RequiresRestart()){Quarantine(s,"NRPG: Vulkan shared work uncertain; session retained, CPU fallback suppressed");return;}
        if(s.shared->Preparing()){s.episode.Drop();++s.status.inputFrames;Publish(s,P::Stage::Waiting,reason.c_str());return;}
        if(TransportDiagnostic(s)){s.episode.Drop();return;} // TransportOnly never falls into model helper evaluation.
        Block(s,reason.empty()?"Claimed Vulkan GPU route unavailable":reason.c_str());return;
    }
#endif
    const bool mainline=nrpg::Config(s.runtime,"NgxPath").empty();
    if(mainline && !BindCore(s)){Block(s,"Loaded NeuRotic core lacks the prepared rendering interface");return;}
    if(mainline&&s.queryPolicy){DlssNr::Connections::PolicyWire policy;
        if(!s.queryPolicy(&policy,sizeof(policy))||policy.unsafe||policy.transport==1||(!policy.allowCpuFallback&&policy.transport==0)||
           !s.claimRoute||!s.claimRoute(s.claimedSource?s.claimedSource:3,2,s.id)){Block(s,"CPU fallback is not eligible for this selected owner");return;}
        s.claimedSource=s.claimedSource?s.claimedSource:3;s.activeTransport=2;}
    if(!mainline && !s.helper.Start(s.runtime,reason)){Block(s,reason.empty()?"Public NGX helper unavailable":reason.c_str());return;}
    if(!SourceIdle(s,commands)){Quarantine(s,"NRPG: source readback retirement unknown; retained helper slot");return;}
    if(!s.helper.Allocate(s.apiDevice,source,reason,views)){Block(s,reason.c_str());return;}
    if(s.apiDevice->get_api()==A::device_api::d3d10||s.apiDevice->get_api()==A::device_api::d3d11||s.apiDevice->get_api()==A::device_api::d3d12)for(unsigned i=0;i<4;++i)s.helperAliases[i]=reinterpret_cast<IUnknown*>(source[i].handle);
    s.sourceReturned=false;s.helper.Record(commands,source);s.sourceReturned=SourceIdle(s,commands);
    if(!s.sourceReturned){Quarantine(s,"NRPG: readback completion unknown; retained staging resources");return;}
    std::vector<std::byte> packed;if(!s.helper.Pack(packed)){Block(s,"Captured guide readback could not be mapped");return;}
    ++s.status.inputFrames;Publish(s,P::Stage::Captured,"ReShade color, captured depth and estimated motion read back");
    auto shape=s.apiDevice->get_resource_desc(source[0]);nrpg::H::Request request;request.guides=Descriptor(s,shape.texture.width,shape.texture.height);
    LARGE_INTEGER timestamp{};QueryPerformanceCounter(&timestamp);request.timestampQpc=timestamp.QuadPart;request.header.payloadBytes=packed.size();request.flags=nrpg::H::CallerReadbackCompleted;
    nrpg::H::Reply reply;std::vector<std::uint8_t> output;
    bool probe=nrpg::Config(s.runtime,"ProbeContent")=="1"&&s.probeCaptures<16;s.probePending=false;
    auto colorBytes=std::size_t(shape.texture.width)*shape.texture.height*4;
    if(probe){s.probeDescriptor=request.guides;auto identity=[&](std::uint64_t handle){return DlssNr::PreparedContent::ResourceIdentity{s.deviceId,s.episode.generation,handle};};
        s.probeBindings={identity(source[0].handle),identity(reinterpret_cast<std::uintptr_t>(packed.data())),identity(1),identity(source[0].handle),identity(s.runtime->get_current_back_buffer().handle),{}};
        // The processed buffer is allocated by the helper client; begin the
        // bounded probe after its real pointer is available below.
    }
    Publish(s,P::Stage::Processing,mainline?"Normalizing captured guides and recording NeuRotic nodes":"Processing through explicit public NGX helper");
    bool processed=false;
    if(mainline){std::uint64_t luid=0;
        if(!s.apiDevice->get_property(A::device_properties::adapter_luid,&luid)||!luid)reason="Source adapter identity unavailable for mainline processing";
        else processed=s.mainline.Process(luid,request.guides,packed,output,reason);
    }else processed=s.helper.Process(request,packed,reply,output,reason);
    if(!processed){
        if(mainline&&s.mainline.Preparing()){s.episode.Drop();s.helperAliases={};Publish(s,P::Stage::Waiting,reason.c_str());}
        else if(mainline&&s.mainline.RequiresRestart())Quarantine(s,reason.c_str());else Block(s,reason.c_str());return;
    }
    ++s.status.modelCompletions;Publish(s,P::Stage::Processed,"Completed model output returned; copyback pending");
    if(probe){s.probeBindings[2].handle=reinterpret_cast<std::uintptr_t>(output.data());
        auto begin=s.probe.Begin(s.probeDescriptor,s.probeBindings,DlssNr::PreparedContent::Mode::Processed);
        if(begin.accepted){++s.probeCaptures;ProbeObservation(s,DlssNr::PreparedContent::Stage::Capture,std::span(packed).first(colorBytes));
            ProbeObservation(s,DlssNr::PreparedContent::Stage::Prepared,std::span(packed).first(colorBytes));
            ProbeObservation(s,DlssNr::PreparedContent::Stage::Processed,std::as_bytes(std::span(output)));}else probe=false;
    }
    // Exact completed reply belongs to this current capture. It grants no rights
    // to a different source texture and does not stand in for upload completion.
    s.pendingUpload=std::move(output);s.sourceReturned=false;s.helper.Upload(commands,s.runtime->get_command_queue(),source[0],s.pendingUpload);s.sourceReturned=SourceIdle(s,commands);
    if(!s.sourceReturned){Quarantine(s,"NRPG: helper upload completion unknown; retained source slot");return;}
    if(probe){s.sourceReturned=false;s.helper.RecordColor(commands,source[0],A::resource_usage::render_target);s.sourceReturned=SourceIdle(s,commands);
        if(!s.sourceReturned){Quarantine(s,"NRPG: content copyback readback completion uncertain");return;}
        std::vector<std::byte> observed;if(s.helper.Pack(observed)){ProbeObservation(s,DlssNr::PreparedContent::Stage::CopyBack,std::span(observed).first(colorBytes));s.probePending=true;}}
    s.pendingUpload.clear();s.helperAliases={};s.episode.Delivered();++s.status.copybackCompletions;
    Publish(s,P::Stage::Delivered,"Completed output copied back to ReShade; display not observed");
}
void Feed(Session& s,A::command_list* commands,A::resource_view rtv)
{
    if(!NrEnabled()&&!TransportDiagnostic(s)){Block(s,"Enable NR on a Present route using the prepared launcher");return;}
    if(s.failed || !Healthy(s)){Block(s,"Capture device unavailable or session quarantined");return;}
    if(!s.episode.Armed()&&!DeferredFlow(s)){Block(s,"Motion provider has not completed before the prepared guide pass");return;}
    if(!s.depthReady){Block(s,"No usable captured depth binding for this frame");return;}
    if(!HostInterface(s.runtime)){Block(s,"Prepared shader host uniforms are missing; reload effects with performance mode off");return;}
    if(!Sdr(s))
    {
        Block(s,"Current source is not a matched SDR swapchain");
        if(!s.colorRefusalReported){s.colorRefusalReported=true;Log("NRPG: non-SDR/unknown color space or unmatched swapchain bypassed");}
        return;
    }
    s.colorRefusalReported=false;
    if(!s.helperRoute){auto color=s.apiDevice->get_resource_from_view(rtv);auto shape=s.apiDevice->get_resource_desc(color);
        if(shape.texture.format!=A::format::r8g8b8a8_unorm && shape.texture.format!=A::format::r16g16b16a16_float)s.helperRoute=true;}
    if(s.helperRoute){FeedHelper(s,commands,rtv);return;}
    if(!BindCore(s) || !Setup(s)){Block(s,"Mainline core or private recording queue unavailable");return;}
    std::array<A::resource,4> source={s.runtime->get_device()->get_resource_from_view(rtv),
        Texture(s.runtime,"NRPG_RawDepth"),Texture(s.runtime,"NRPG_Motion"),Texture(s.runtime,"NRPG_Distrust")};
    std::array<ID3D12Resource*,4> input{};
    for(std::size_t i=0;i<4;++i){input[i]=reinterpret_cast<ID3D12Resource*>(source[i].handle);if(!input[i]){s.episode.Drop();return;}}
    if(!Allocate(s,input) || !nrpg::MayReuse(s.privateComplete,s.sourceReturned,Healthy(s))){s.episode.Drop();return;}
    // Hold source aliases until BOTH the private copies and runtime copy-back are retired.
    for(std::size_t i=0;i<4;++i)s.sources[i]=input[i];
    if(!SourceIdle(s,commands)){Quarantine(s,"NRPG: source completion unknown; session retained");return;}
    if(s.claimRoute&&!s.claimRoute(3,1,s.id)){Block(s,"Another renderer source owns the session or GPU transport was not selected");return;}
    s.claimedSource=3;s.activeTransport=1;
    if(!s.prepare(s.list.Get()) || FAILED(s.allocator->Reset()) || FAILED(s.list->Reset(s.allocator.Get(),nullptr)))
    {s.episode.Drop();return;}
    for(std::size_t i=0;i<4;++i)
    {
        auto original=i==0?D3D12_RESOURCE_STATE_RENDER_TARGET:ShaderRead;
        auto owned=i==0?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Barrier(s.list.Get(),input[i],original,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier(s.list.Get(),s.images[i].Get(),owned,D3D12_RESOURCE_STATE_COPY_DEST);
        s.list->CopyResource(s.images[i].Get(),input[i]);
        Barrier(s.list.Get(),s.images[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,owned);
        Barrier(s.list.Get(),input[i],D3D12_RESOURCE_STATE_COPY_SOURCE,original);
    }
    P::Dispatch dispatch;dispatch.list=s.list.Get();dispatch.queue=s.queue.Get();
    dispatch.color=s.images[0].Get();dispatch.depth=s.images[1].Get();dispatch.motion=s.images[2].Get();dispatch.distrust=s.images[3].Get();
    unsigned w=static_cast<unsigned>(s.shapes[0].Width),h=s.shapes[0].Height;dispatch.source=Descriptor(s,w,h);
    dispatch.workWidth=w;dispatch.workHeight=h;dispatch.requireModelDistrust=0; // mask is diagnostic, private NR has no supported binding
    ++s.status.inputFrames;Publish(s,P::Stage::Processing,"Normalizing captured guides and recording NeuRotic nodes");
    auto result=static_cast<P::Result>(s.record(&dispatch));
    if(FAILED(s.list->Close())){Quarantine(s,"NRPG: private list close failed; recording retained");return;}
    ID3D12CommandList* lists[]={s.list.Get()};s.privateComplete=false;
    s.queue->ExecuteCommandLists(1,lists);
    // ExecuteCommandLists has no HRESULT. Seal follows accepted invocation;
    // successful Signal+observed completion are separate required evidence.
    if((result!=P::Result::Disabled&&!s.seal(s.list.Get())) || FAILED(s.queue->Signal(s.fence.Get(),++s.value)) || !Wait(s))
    {Quarantine(s,"NRPG: private submit/completion uncertain; session retained, output dropped");return;}
    if(result==P::Result::Preparing){s.episode.Drop();s.sources={};Publish(s,P::Stage::Waiting,"Mainline model lifecycle preparation completed; waiting for the next capture");return;}
    if(result!=P::Result::Recorded){Quarantine(s,"NRPG: mainline recording refused; completed slot retained");return;}
    ++s.status.modelCompletions;Publish(s,P::Stage::Processed,"Mainline GPU work completed; copyback pending");
    A::resource owned{reinterpret_cast<std::uint64_t>(s.images[0].Get())};
    commands->barrier(owned,A::resource_usage::unordered_access,A::resource_usage::copy_source);
    commands->barrier(source[0],A::resource_usage::render_target,A::resource_usage::copy_dest);
    s.sourceReturned=false;commands->copy_resource(owned,source[0]);
    commands->barrier(source[0],A::resource_usage::copy_dest,A::resource_usage::render_target);
    commands->barrier(owned,A::resource_usage::copy_source,A::resource_usage::unordered_access);
    s.sourceReturned=SourceIdle(s,commands);
    if(!s.sourceReturned){Quarantine(s,"NRPG: copy-back completion unknown; aliases retained");return;}
    s.sources={};s.episode.Delivered();++s.status.copybackCompletions;Publish(s,P::Stage::Delivered,"Completed output copied back to ReShade; display not observed");
}
void Resolve(Session& s)
{
    s.guide=s.runtime->find_technique(Effect,"NeuRotic_PreparedGuides");
    char text[16]{};int mode=0;
    if(s.runtime->get_preprocessor_definition_for_effect(Effect,"NRPG_MV_PROVIDER",text) ||
        s.runtime->get_preprocessor_definition("NRPG_MV_PROVIDER",text))mode=std::atoi(text);
    A::effect_technique producer{};const Provider* selected=nullptr;
    for(const auto& entry:Providers)
    {
        if(entry.mode!=mode)continue;
        auto technique=s.runtime->find_technique(entry.file,entry.technique);
        if(technique.handle && s.runtime->get_technique_state(technique)){producer=technique;selected=&entry;break;}
    }
    if(s.provider!=selected || s.producer.handle!=producer.handle || s.builtin!=(mode==5))s.episode.Drop();s.builtin=mode==5;
    s.provider=selected;s.producer=producer;
}
void Begin(A::effect_runtime* runtime,A::command_list* commands,A::resource_view rtv,A::resource_view)
{
    std::lock_guard lock(Store().mutex);auto* s=Get(runtime);if(!s || s->failed)return;
    if(s->externalOwner)return;
#ifdef _WIN64
    if(BindCore(*s)&&s->queryPolicy){DlssNr::Connections::PolicyWire policy;if(s->queryPolicy(&policy,sizeof(policy))&&policy.source==4)return;}
#endif
    Resolve(*s);s->guideSeen=false;s->consumerReset=false;s->sourceSaved=false;s->inFrame=s->episode.Begin(Frame(runtime));Host(*s,false);ColorHost(*s,s->inFrame&&!s->helperRoute&&NrEnabled()&&Sdr(*s));
    Publish(*s,P::Stage::Waiting,!s->guide.handle?"Prepared guide effect is not loaded":!runtime->get_technique_state(s->guide)?"Enable the prepared guide effect after the motion provider":!NrEnabled()?"Enable NR on a Present route using the prepared launcher":"Waiting for this frame's depth and motion");
    if(s->inFrame&&DeferredFlow(*s)&&NrEnabled()){
        Host(*s,true);Set(runtime,"NRPG_MotionReady",false); // Motion is produced after the depth-only pass.
    }
#ifdef _WIN64
    else if(s->inFrame&&s->builtin&&NrEnabled()&&!BuiltinFlow(*s,commands,rtv))s->episode.Drop();
    ColorHost(*s,s->inFrame&&!s->helperRoute&&!s->failed&&NrEnabled()&&Sdr(*s));
#else
    (void)commands;(void)rtv;
#endif
}
void Technique(A::effect_runtime* runtime,A::effect_technique technique,A::command_list* list,A::resource_view rtv,A::resource_view)
{
    std::lock_guard lock(Store().mutex);auto* s=Get(runtime);if(!s || s->failed || !s->inFrame)return;
    if(s->externalOwner)return;
    if(technique==runtime->find_technique("NeuRotic_SourceGuidedColor.fx","NeuRotic_SourceGuided_Save")&&!s->guideSeen){s->sourceSaved=true;return;}
    if(technique==s->producer && !s->guideSeen && s->provider)
    {
        auto raw=Texture(runtime,s->provider->texture);
        auto desc=raw.handle?runtime->get_device()->get_resource_desc(raw):A::resource_desc{};
        unsigned w=0,h=0;runtime->get_screenshot_width_and_height(&w,&h);
        bool low=s->provider->mode>=3;
        if(desc.type==A::resource_type::texture_2d && desc.texture.width==(low?w/8:w) && desc.texture.height==(low?h/8:h) &&
            desc.texture.samples==1 && desc.texture.format==A::format::r16g16_float)
        {s->episode.Provider();Host(*s,true);}
        return;
    }
    if(technique==s->guide)
    {
        s->guideSeen=true;
        if(s->episode.Armed()||DeferredFlow(*s))Feed(*s,list,rtv);else Block(*s,"Motion provider must render before the prepared guide effect");
#ifdef _WIN64
        if(s->flowBound)ReturnFlow(*s,list);
#endif
        if(!s->helperRoute)ColorHost(*s,s->sourceSaved&&Sdr(*s),s->episode.previous==s->episode.capture);
        // Helper color composition executes on the completed source/output pair.
    }
}
void Finish(A::effect_runtime* runtime,A::command_list* commands,A::resource_view,A::resource_view)
{
    std::lock_guard lock(Store().mutex);auto* s=Get(runtime);if(!s || s->failed)return;
    if(s->externalOwner)return;
#ifdef _WIN64
    if(s->flowBound&&!ReturnFlow(*s,commands))return;
#else
    (void)commands;
#endif
    if(s->inFrame && !s->guideSeen)Block(*s,"Prepared guide effect did not execute this frame");s->inFrame=false;Host(*s,false);ColorHost(*s,false);
}
void Reload(A::effect_runtime* runtime)
{
    std::lock_guard lock(Store().mutex);auto* s=Get(runtime);if(!s || s->failed)return;
    s->episode.Drop();s->selectedDepth=0;s->inFrame=false;Resolve(*s);Host(*s,false);
}
void Destroy(A::effect_runtime* runtime)
{
    std::lock_guard lock(Store().mutex);auto& sessions=Store().sessions;
    auto& runtimes=Store().runtimes;runtimes.erase(std::remove(runtimes.begin(),runtimes.end(),runtime),runtimes.end());
    for(auto it=sessions.begin();it!=sessions.end();++it)if((*it)->runtime==runtime)
    {
        auto& s=**it;Publish(s,P::Stage::Idle,"ReShade runtime closed");
        {auto& shared=StatusStore();std::lock_guard statusLock(shared.mutex);if(shared.latest.session==s.id)shared.latest.active=shared.latest.depthReady=shared.latest.motionReady=0;}
        s.runtime=nullptr;
        if(!s.failed && nrpg::MayReuse(s.privateComplete,s.sourceReturned,Healthy(s))) {
#ifdef _WIN64
            std::string reason;if(s.flowInitialized&&!s.flow.Close(reason)){Quarantine(s,"NRPG: producer cleanup completion unavailable; retained slot");break;}
#endif
            if(s.claimedSource&&s.retireRoute)s.retireRoute(s.claimedSource,s.id,1);
            sessions.erase(it);
        }
        else Quarantine(s,"NRPG: runtime destroyed with uncertain work; retained slot consumes capacity");
        break;
    }
}
void Present(A::command_queue* queue,A::swapchain* chain,const A::rect*,const A::rect*,std::uint32_t,const A::rect*)
{
    std::lock_guard lock(Store().mutex);for(auto& entry:Store().sessions){auto& s=*entry;if(!s.runtime||s.failed||!s.probePending||s.runtime->get_command_queue()!=queue)continue;
        auto source=chain->get_current_back_buffer();if(source.handle!=s.probeBindings[4].handle||!s.helper.ColorShape(source))continue;s.probePending=false;
        auto* commands=queue->get_immediate_command_list();s.sourceReturned=false;s.helper.RecordColor(commands,source,A::resource_usage::present);s.sourceReturned=SourceIdle(s,commands);
        if(!s.sourceReturned){Quarantine(s,"NRPG: present-attempt content completion uncertain");continue;}
        std::vector<std::byte> observed;if(s.helper.Pack(observed)){auto bytes=std::size_t(s.probeDescriptor.color.width)*s.probeDescriptor.color.height*4;
            ProbeObservation(s,DlssNr::PreparedContent::Stage::PresentAttempt,std::span(observed).first(bytes));}
    }
}
void DestroyResource(A::device* device,A::resource resource)
{
    std::lock_guard lock(Store().mutex);for(auto& entry:Store().sessions){auto& s=*entry;if(s.apiDevice==device&&s.selectedDepth==resource.handle&&!s.failed){s.selectedDepth=0;s.depthReady=false;s.episode.Drop();}}
}
void InitDevice(A::device* device){
    if(device->get_api()!=A::device_api::opengl)return;
    auto& store=Store();std::lock_guard lock(store.mutex);
    const auto current=wglGetCurrentContext();if(!current||reinterpret_cast<uint64_t>(current)!=device->get_native()||store.glContexts.size()>=MaxSessions)return;
    const auto incarnation=store.next++;
    // ReShade init/destroy_device delimit this observed context incarnation.
    // No imported object or GL name is shared between different context owners.
    store.glContexts[device]=std::make_shared<DlssNr::Connections::GlContextLease>(current,DlssNr::Connections::GlContextIdentity{incarnation,incarnation,GetCurrentThreadId()});
}
void InitRuntime(A::effect_runtime* runtime){
    auto& store=Store();std::lock_guard lock(store.mutex);
    if(runtime&&store.runtimes.size()<MaxSessions*4&&std::find(store.runtimes.begin(),store.runtimes.end(),runtime)==store.runtimes.end())store.runtimes.push_back(runtime);
}
void DestroyDevice(A::device* device){
    auto& store=Store();std::lock_guard lock(store.mutex);auto found=store.glContexts.find(device);
    if(found!=store.glContexts.end()){found->second->Invalidate();store.glContexts.erase(found);}
    for(auto& entry:store.sessions)if(entry->apiDevice==device&&entry->connectionGl)Quarantine(*entry,"OpenGL device/context destroyed; old imports retained and source stopped");
}
void InitChain(A::swapchain* chain,bool)
{
    std::lock_guard lock(Store().mutex);auto& chains=Store().chains;
    for(auto* known:chains)if(known==chain)return;
    if(chains.size()<16)chains.push_back(chain);
}
void DestroyChain(A::swapchain* chain,bool)
{
    std::lock_guard lock(Store().mutex);auto& chains=Store().chains;
    for(auto it=chains.begin();it!=chains.end();++it)if(*it==chain){chains.erase(it);break;}
}
}
extern "C" __declspec(dllexport) unsigned __cdecl NeuRotic_QueryPreparedGuidesV1(DlssNr::PreparedGuides::Status* status)
{
    if(!status||status->size!=sizeof(*status)||status->version!=1)return 0;
    auto& shared=StatusStore();std::lock_guard lock(shared.mutex);*status=shared.latest;return 1;
}
extern "C" __declspec(dllexport) unsigned __cdecl NeuRotic_EvaluateExternalFeederV1(const nrpg::external::Frame* frame,nrpg::external::Receipt* receipt)
{
    namespace E=nrpg::external;
    if(!frame||!receipt||receipt->size!=sizeof(*receipt)||receipt->version!=E::Version)return 0;
    *receipt={};auto refuse=[&](std::string_view reason){const auto n=std::min(reason.size(),sizeof(receipt->reason)-1);std::memcpy(receipt->reason,reason.data(),n);receipt->reason[n]=0;return 1u;};
    if(!E::HeaderValid(*frame))return refuse("Unsupported external feeder header");
    #ifndef _WIN64
    if((frame->runtime>>32)||(frame->commands>>32))return refuse("External pointers exceed local process width");
    if(frame->descriptor.sourceApi==F::C::GraphicsApi::D3D10||frame->descriptor.sourceApi==F::C::GraphicsApi::D3D11||frame->descriptor.sourceApi==F::C::GraphicsApi::D3D12){for(auto resource:frame->resources)if(resource>>32)return refuse("External resource pointer exceeds local process width");}
#endif
    auto semanticCheck=frame->descriptor;
    if(semanticCheck.sourceApi==F::C::GraphicsApi::OpenGL&&semanticCheck.rasterOrigin==F::C::RasterOrigin::BottomLeft)
        semanticCheck.rasterOrigin=F::C::RasterOrigin::TopLeft; // GlConnection performs the real row/vector-Y conversion.
    const auto normalized=Neurotic::Context::NormalizePreparedGuides(semanticCheck);if(!normalized)return refuse(normalized.reason);
    auto& store=Store();std::lock_guard lock(store.mutex);
    auto* runtime=reinterpret_cast<A::effect_runtime*>(static_cast<std::uintptr_t>(frame->runtime));
    if(std::find(store.runtimes.begin(),store.runtimes.end(),runtime)==store.runtimes.end())return refuse("External runtime has no live ReShade registration");
    auto* commands=reinterpret_cast<A::command_list*>(static_cast<std::uintptr_t>(frame->commands));
    auto* queue=runtime->get_command_queue();if(!queue||commands!=queue->get_immediate_command_list())return refuse("External callback does not own the immediate command stream");
    auto* s=Get(runtime);if(!s)return refuse("External session capacity/device unavailable");
    if(s->failed){receipt->result=E::Result::Unsafe;return refuse("External session requires restart");}
    if(!NrEnabled()||!Sdr(*s))return refuse("External rendering requires enabled NR and matched SDR swapchain");
    const auto& d=frame->descriptor;
    const auto actualApi=s->apiDevice->get_api();const auto expected=actualApi==A::device_api::d3d11?F::C::GraphicsApi::D3D11:actualApi==A::device_api::d3d10?F::C::GraphicsApi::D3D10:actualApi==A::device_api::opengl?F::C::GraphicsApi::OpenGL:actualApi==A::device_api::vulkan?F::C::GraphicsApi::Vulkan:F::C::GraphicsApi::D3D12;
    if(d.sourceApi!=expected||s->apiDevice->get_resource_from_view(A::resource_view{frame->outputView}).handle!=frame->resources[0])return refuse("External API or output view association mismatch");
    if(s->claimedSource&&s->claimedSource!=4)return refuse("Prepared effect already owns this session");
    if(s->externalOwner){const auto& old=s->externalDescriptor;
        if(d.producer!=old.producer||d.session!=old.session||d.stream!=old.stream||d.device!=old.device||d.generation<old.generation||(d.generation==old.generation&&d.capture<=old.capture))return refuse("External identity changed or capture replayed");}
#ifdef _WIN64
    if(!BindCore(*s)||!s->claimRoute||!s->claimRoute(4,1,s->id))return refuse("External feeder source/GPU transport not selected or another owner active");
#endif
    s->claimedSource=4;s->externalOwner=true;s->externalDescriptor=d;s->depthReady=true;s->lastPreparing=false;s->currentMotionReady=false;
    std::array<A::resource,4> source{};for(unsigned i=0;i<4;++i)source[i]={frame->resources[i]};
    std::string reason;const auto before=s->status.copybackCompletions;
    bool handled=FeedApiGpu(*s,commands,source,reason,&d,frame->deriveMotion!=0);
#ifdef _WIN64
    if(!handled&&!s->failed&&actualApi==A::device_api::vulkan){
        if(!s->shared)s->shared=std::make_unique<nrpg::SharedGpuClient>();
        if(!frame->deriveMotion&&s->shared->Process(queue,d,source,reason,false)){
            if(s->shared->CompletedCapture()!=d.capture||!s->shared->CompletionValue())Quarantine(*s,"External Vulkan completed capture mismatch");
            else{s->currentMotionReady=true;++s->status.inputFrames;++s->status.modelCompletions;++s->status.copybackCompletions;s->lastDeliveredCapture=d.capture;s->lastDeliveredGeneration=d.generation;Publish(*s,P::Stage::Delivered,"External Vulkan exact output copyback completed");}
        }else if(s->shared->RequiresRestart())Quarantine(*s,reason.c_str());else s->lastPreparing=s->shared->Preparing();handled=true;
    }
#endif
    receipt->capture=d.capture;receipt->generation=d.generation;
    if(s->failed)receipt->result=E::Result::Unsafe;
    else if(s->status.copybackCompletions==before+1&&s->lastDeliveredCapture==d.capture&&s->lastDeliveredGeneration==d.generation)receipt->result=E::Result::Delivered;
    else if(s->lastPreparing)receipt->result=E::Result::Preparing;
    if(!handled&&reason.empty())reason="External API GPU path unavailable";
    strncpy_s(receipt->reason,reason.c_str(),_TRUNCATE);return 1;
}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID)
{
    if(reason==DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);if(!reshade::register_addon(module))return FALSE;
        reshade::register_event<reshade::addon_event::init_device>(InitDevice);
        reshade::register_event<reshade::addon_event::init_effect_runtime>(InitRuntime);
        reshade::register_event<reshade::addon_event::destroy_device>(DestroyDevice);
        reshade::register_event<reshade::addon_event::reshade_begin_effects>(Begin);
        reshade::register_event<reshade::addon_event::reshade_render_technique>(Technique);
        reshade::register_event<reshade::addon_event::reshade_finish_effects>(Finish);
        reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(Reload);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(Destroy);
        reshade::register_event<reshade::addon_event::present>(Present);
        reshade::register_event<reshade::addon_event::destroy_resource>(DestroyResource);
        reshade::register_event<reshade::addon_event::init_swapchain>(InitChain);
        reshade::register_event<reshade::addon_event::destroy_swapchain>(DestroyChain);
    }
    else if(reason==DLL_PROCESS_DETACH)
    {
        reshade::unregister_event<reshade::addon_event::init_device>(InitDevice);
        reshade::unregister_event<reshade::addon_event::init_effect_runtime>(InitRuntime);
        reshade::unregister_event<reshade::addon_event::destroy_device>(DestroyDevice);
        reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(Begin);
        reshade::unregister_event<reshade::addon_event::reshade_render_technique>(Technique);
        reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(Finish);
        reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(Reload);
        reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(Destroy);
        reshade::unregister_event<reshade::addon_event::present>(Present);
        reshade::unregister_event<reshade::addon_event::destroy_resource>(DestroyResource);
        reshade::unregister_event<reshade::addon_event::init_swapchain>(InitChain);
        reshade::unregister_event<reshade::addon_event::destroy_swapchain>(DestroyChain);
        reshade::unregister_addon(module); // no provider wait or uncertain release under loader lock
    }
    return TRUE;
}
