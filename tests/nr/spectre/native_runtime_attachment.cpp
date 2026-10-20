#define NR_GPU_SAFETY_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#define NR_SPECTRE_SOURCE_TESTING
#define NR_SOURCE_TRANSACTION_TESTING
#include <nr/lifecycle/NativeProcessBootstrap.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <iostream>
#include <thread>
namespace C=Neurotic::Contracts;namespace L=Neurotic::Lifecycle;namespace O=Neurotic::Orchestration;
namespace P=Neurotic::Protocol;namespace S=Neurotic::Context;
using Microsoft::WRL::ComPtr;
namespace DlssNr
{
class NativeRendererRetirementTestAccess
{
  public:
    static auto Receipt(std::shared_ptr<NativeProviderRetirementOwner> owner)
    {auto result=std::shared_ptr<NativeRendererRetirement>(new NativeRendererRetirement);
     result->Add(owner);result->observation_.attempted=1;result->observation_.succeeded=1;return result;}
};
// This source fixture never constructs a renderer invocation borrow. Keep a
// fail-fast link sentinel rather than simulating a successful provider owner.
const GpuSafety::LocalRecordingAction& NativeRendererInvocationBorrow::RecordingAction()const
{throw std::runtime_error("unexpected renderer invocation in source-only fixture");}
}
struct FixtureDependencyRefusal:std::runtime_error{using std::runtime_error::runtime_error;};
static void RequireFixture(bool value,const char* reason)
{if(!value)throw FixtureDependencyRefusal(reason);}
#ifdef NR_NATIVE_RUNTIME_ATTACHMENT_V1
namespace Neurotic::Lifecycle
{
struct SourceTransactionTestAccess
{
    template<class Factory>static bool Advance(NativeSourceTransactionObservation& source,
        const C::ResourceView& view,std::uint64_t recording,std::uint64_t ordinal,Factory&& factory)
    {
#ifdef NR_NATIVE_FINAL_OUTPUT_REFRESH_V1
        return source.AdvanceSelectedOutput(view,recording,ordinal,std::forward<Factory>(factory));
#else
        return false;
#endif
    }
};
class NativeResourceRegistryTestAccess
{
  public:
    static bool FixtureContent(NativeResourceRegistry& owner,ID3D12Resource* native)
    {return owner.MarkWritten(native);} // synthetic content, never GPU evidence
    static void BlockPhysicalDetach(NativeResourceRegistry& owner,bool blocked)
    {owner.privateController_=blocked?reinterpret_cast<const NativeOwnedContinuation*>(&owner):nullptr;}
    static auto Borrow(NativeResourceRegistry& owner,ID3D12Resource* native,const C::ResourceView& expected)
    {return owner.BorrowCurrentRead(native,expected);}
};
class NativeProcessTestAccess
{
  public:
    static auto Open(){return std::unique_ptr<NativeProcessBootstrap>(new NativeProcessBootstrap);}
    static auto Capture(NativeProcessBootstrap& host,DlssNr::NativeFeatureRegistry<unsigned>& registry)
    {auto pin=registry.Pin(7,registry.Read(7));RequireFixture(bool(pin),"feature pin unavailable");
     auto callback=host.Capture(registry,std::move(*pin));RequireFixture(bool(callback),"callback capture refused");return callback;}
    template<class Parameters>static bool Bind(NativeProcessBootstrap& host,NativeProcessBootstrap::Callback& callback,
        ID3D12GraphicsCommandList* list,Parameters* parameters)
    {const bool bound=host.BindCall(callback,list,parameters,0);RequireFixture(bound,"original call binding refused");return bound;}
    template<class Snapshot,class Factory>static auto Materialize(NativeProcessBootstrap& host,
        NativeProcessBootstrap::Callback& callback,const Snapshot& settings,Factory&& factory)
    {return host.Materialize(callback,settings,std::forward<Factory>(factory));}
    static void Close(NativeProcessBootstrap& host){host.CloseSource();}
    static auto Teardown(NativeProcessBootstrap& host){return host.PrepareTeardown();}
    static auto Complete(NativeProcessBootstrap& host,std::shared_ptr<const DlssNr::NativeRendererRetirement> receipt)
    {return host.CompleteTeardown(std::move(receipt));}
    static auto Recreate(NativeProcessBootstrap& host)
    {std::shared_ptr<NativeProcessBootstrap> fresh;
     return host.RecreateScope([&](auto value){fresh=std::move(value);return true;})?fresh:nullptr;}
    static auto RecreateRacing(NativeProcessBootstrap& host,DlssNr::NativeFeatureRegistry<unsigned>& registry,bool& refused)
    {std::shared_ptr<NativeProcessBootstrap> fresh;bool oldRefused=false;
     std::thread oldQuery([&]{DlssNr::NativeHostReturnObservationV1 observed;oldRefused=!host.QueryNativeReturn(0,7,0,0,observed);});
     const bool completed=host.RecreateScope([&](auto value){fresh=value;
        std::thread racing([&]{auto pin=registry.Pin(7,registry.Read(7));
            refused=!pin&&fresh->EnsureOwners();});
        racing.join();return true;});oldQuery.join();refused=refused&&oldRefused;return completed?fresh:nullptr;}
    static bool CloseKernel(NativeProcessBootstrap& host,std::shared_ptr<const DlssNr::NativeRendererRetirement> receipt)
    {host.state_->rendererRetirement=std::move(receipt);return host.state_->lifetime->AdvanceClosure().closed;}
    static bool RefuseFreshPublication(NativeProcessBootstrap& host)
    {std::shared_ptr<NativeProcessBootstrap> refused;
     const bool result=host.RecreateScope([&](auto fresh){refused=std::move(fresh);return false;});
     return !result&&refused&&!refused->EnsureOwners();}
    static auto Retirement(NativeProcessBootstrap& host){return host.ObserveOwnerRetirement(*host.state_);}
    static void Return(NativeProcessBootstrap::Callback& callback,bool success)
    {
#ifdef NR_NATIVE_SOURCE_RETURN_V1
        callback.Drop(success);
#else
        callback.Drop();
#endif
    }
    static auto Owners(NativeProcessBootstrap& host){return host.state_->Owner();}
    static auto ReadFactory(NativeProcessBootstrap& host,ID3D12Resource* native,const C::ResourceView& view)
    {return host.SelectedReadFactory(native,view);}
    static unsigned Query(NativeProcessBootstrap& host,std::uint64_t after,std::uint64_t handle,
        ID3D12GraphicsCommandList* list,ID3D12Resource* output,DlssNr::NativeHostReturnObservationV1& value)
    {return host.QueryNativeReturn(after,handle,reinterpret_cast<std::uintptr_t>(list),
        reinterpret_cast<std::uintptr_t>(output),value);}
    static auto Lifetime(NativeProcessBootstrap& host){return host.state_->lifetime;}
    static auto CurrentPreparation(const NativeProcessBootstrap::Callback& callback){return callback.currentPreparation_;}
    // Fixture-only injection at the private source seam. This supplies no SDK
    // lease, GPU permission, runtime qualification or actual consumer action.
    static void SelectControlledFixture(NativeProcessBootstrap& host,const NativeProcessBootstrap::Callback& callback,
        unsigned scenario,ID3D12GraphicsCommandList* list,ID3D12Resource* output)
    {
        auto& state=*host.state_;auto& owners=*state.Owner();
        auto selected=std::make_shared<NativeProcessBootstrap::ControlledFg>();
        const auto provider=owners.Provider()->Incarnation(owners.SourceJournal().Event());
        const auto handoff=owners.Handoff()->ContractChanged(owners.FinalizerJournal().Event());
        RequireFixture(S::Established(provider.value)&&S::Established(handoff.value),"fixture FG identity unavailable");
        selected->consumer.provider=provider.value.KnownPart()->value;selected->consumer.handoff=handoff.value.KnownPart()->value;
        selected->consumer.contract=P::Symbol("FSR3.ControlledOwnedOutput.v1");selected->consumer.contractVersion=1;
        selected->consumer.color=C::ColorDomain::SceneLinear;selected->consumer.extent={16,8};
        if(scenario!=3)selected->enrollment=owners.FrameGenerationJournal().Event().evidence.record;
        selected->writerCurrent=[list,output](ID3D12GraphicsCommandList* l,ID3D12Resource* r){return l==list&&r==output;};
        state.controlledFeature=7;state.controlledFeatureId=scenario==1?8:callback.featureHandle_;
        state.controlledFeatureGeneration=callback.generation_+(scenario==2?1:0);state.controlledFg=std::move(selected);
    }
    static void ChangeControlledEnrollment(NativeProcessBootstrap& host)
    {host.state_->controlledFg->enrollment=host.state_->Owner()->FrameGenerationJournal().Event().evidence.record;}
    static auto RecordResult(NativeProcessBootstrap& host,NativeProcessBootstrap::Callback& callback,
        P::NativeProtocolResult result)
    {return host.RecordNativeResult(callback,result);}
    static auto CaptureUnavailable(NativeProcessBootstrap& host,NativeProcessBootstrap::Callback& callback,
        const Neurotic::D3D12::NativeRecordingObservation& recording)
    {return host.RecordCommandStateUnavailable(callback,recording);}
    static bool RefreshHostOutput(NativeProcessBootstrap& host,NativeProcessBootstrap::Callback& callback,
        const DlssNr::GpuSafety::LocalRecordingAction& action)
    {return host.RefreshHostOutput(callback,action);}
    template<class Parameters>static bool ObserveSrReturn(NativeProcessBootstrap& host,
        NativeProcessBootstrap::Callback& callback,Parameters* parameters)
    {return host.ObserveSrReturn(callback,parameters,0,0);}
    template<class Parameters>static auto BeginSr(NativeProcessBootstrap& host,
        NativeProcessBootstrap::Callback& callback,Parameters* parameters,const Neurotic::D3D12::NativeRecordingObservation& recording)
    {return host.BeginOpaqueSr(callback,reinterpret_cast<void*>(7),parameters,0,recording);}
    template<class Parameters,class Operation>static void EndSr(NativeProcessBootstrap& host,
        NativeProcessBootstrap::Callback& callback,Operation& operation,Parameters* parameters,int result,
        const Neurotic::D3D12::NativeRecordingObservation& recording)
    {host.ObserveSrReturn(callback,parameters,result,0);
     host.SealOpaqueSr(callback,operation,reinterpret_cast<void*>(7),parameters,result,0,recording);}
    static bool Reserved(NativeProcessBootstrap& host){return host.state_->initialization!=nullptr;}
    static auto RetainedSample(NativeProcessBootstrap& host)
    {
        RequireFixture(host.state_->initialization&&host.state_->initialization->source,
            "initialization/source absent after refusal");
        const auto& source=*host.state_->initialization->source;
        RequireFixture(source.Source()!=nullptr,"source seed absent");
        const auto* frame=S::ResolveMetadata(source.Source()->seed.frame,source);
        RequireFixture(frame!=nullptr,"frame absent");
        const auto* sample=S::ResolveNativeSample(*frame,source);
        RequireFixture(sample!=nullptr,"sample absent");return *sample;
    }
};
}
template<class T>struct Option
{T v;T value_or_default()const{return v;}bool has_value()const{return true;}T value()const{return v;}};
struct Settings
{
    Option<bool> DlssNrNativeProtocol{true},DlssNrRunBeforeSr{false},DlssNrAutoMask{true};
    Option<unsigned> DlssNrRoute{0},DlssNrPreset{2},DlssNrStyle{0};
    Option<float> DlssNrIntensity{.75f},DlssNrLocalStructure{.2f},DlssNrLocalTone{.3f},DlssNrSkinStructure{.4f};
    struct Runtime{bool enabled=true;std::uint64_t resumeGeneration=1;}runtime;
    const Runtime& GetDlssNrRuntimeSnapshot()const{return runtime;}
};
struct Parameters
{
    std::array<void*,6> resources{};
    unsigned renderWidth=16,colorX=0;
    int Get(const char* key,unsigned* value)
    {const std::string_view k=key;*value=k=="DLSS.Render.Subrect.Dimensions.Width"?renderWidth:
        k=="DLSS.Input.Color.Subrect.Base.X"?colorX:(k=="Width"||k=="OutWidth")?16:
        (k=="Height"||k=="OutHeight"||k=="DLSS.Render.Subrect.Dimensions.Height")?8:0;return 0;}
    int Get(const char* key,float* value)
    {const std::string_view k=key;*value=k=="Jitter.Offset.X"?.5f:k=="Jitter.Offset.Y"?-.25f:k=="MV.Scale.X"?-2.f:1.f;return 0;}
    int Get(const char* key,void** value)
    {const auto& names=DlssNr::NativeNgxCallCapture::ResourceNames();for(std::size_t i=0;i<names.size();++i)
        if(std::string_view(key)==names[i]){*value=resources[i];return 0;}return 1;}
};
// Synthetic model owner only; real WARP metadata and all production
// source/context/profile/coordinator/owner-adoption code execute unchanged.
struct Model
{
    unsigned prepares=0;bool ready=false,current=true,throwPrepare=false;
    DlssNr::NativeRendererPreparation::Resources resources;
};
struct Renderer
{
    Model& model;bool prepared=false;
    explicit Renderer(Model& value):model(value){}
    bool PreflightCurrent()const{return model.current;}
    bool NeedsModelInitialization()const{return !model.ready;}
    std::optional<P::NativeEncodeControls> Encode()const{return P::NativeEncodeControls{1,203,0,0,3,16,8};}
    bool PrepareModel(L::NativeResourceRegistry& resources,DlssNr::NativePreparationStatus& status)
    {
        ++model.prepares;if(model.throwPrepare)throw std::runtime_error("synthetic owner after possible creation");
        if(!model.ready){status=DlssNr::NativePreparationStatus::CreationRecorded;return false;}
        for(auto* image:{model.resources.modelInput,model.resources.modelOutput,model.resources.originalCopy})
            if(!resources.Observe(image))return false;
        prepared=true;status=DlssNr::NativePreparationStatus::Ready;return true;
    }
    bool Current()const{return prepared&&model.ready&&model.current;}
    const auto* Inspect()const{return Current()?&model.resources:nullptr;}
};
template<class Store>struct MetadataOnly
{
    const Store& store;
    template<class T>auto Resolve(const C::MetadataRef<T>& ref)const{return store.Resolve(ref);}
    const C::AcquisitionCandidate* ResolveCandidate(const C::ContractRef<C::ContractId::C01>& ref)const
    {return store.ResolveCandidate(ref);}
};
// Exposes only the production metadata seal for a route-matching test. No
// instance, action guard, live C03 lease or provider authority is constructed.
struct MetadataSealProbe:P::NativeInvocationOwnerPort
{
    template<class Store>static bool Seal(P::NativeInvocationSnapshot& value,const std::shared_ptr<Store>& store)
    {return SealSnapshot(value,store);}
};
#endif
int main() try
{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';RequireFixture(false,name);}};
#ifdef NR_NATIVE_RUNTIME_ATTACHMENT_V1
    const auto passthrough=[](bool protocol,bool enabled,unsigned route,bool ss,bool explicitDlss){
#ifdef NR_NATIVE_SR_PASSTHROUGH_SELECTION_V1
        return DlssNr::UseNativeSrPassthrough(protocol,enabled,route,ss,explicitDlss);
#else
        return false;
#endif
    };
    check(!passthrough(false,true,0,true,true),"Native SR create remains opt-in");
    check(!passthrough(true,false,0,true,true),"disabled Native runtime retains existing creation route");
    check(!passthrough(true,true,1,true,true),"other NR routes retain existing creation route");
    check(!passthrough(true,true,0,false,true),"non-SR features do not acquire selected SR routing");
    check(!passthrough(true,true,0,true,false),"automatic or other backend selection does not acquire native SR routing");
    check(passthrough(true,true,0,true,true),"explicit enabled Native DLSS SR selects real provider creation");
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    const auto api=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("WARP prerequisite");};
    api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    api(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    api(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    const auto firstSeen=DlssNr::GpuSafety::Record(list.Get());
    check(firstSeen!=nullptr,"host command-list recording is observed before Reset");
    api(list->Close());api(list->Reset(allocator.Get(),nullptr));
    std::array<ComPtr<ID3D12Resource>,7> textures;Parameters parameters;
    for(std::size_t i=0;i<textures.size();++i)
    {
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=16;desc.Height=8;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;
        desc.Format=i==2?DXGI_FORMAT_R32_FLOAT:i==3?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;
        if(i>=4)desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&textures[i])));
        if(i<4)parameters.resources[i]=textures[i].Get();
    }
    using A=L::NativeProcessTestAccess;
    {
        for(unsigned scenario=0;scenario<3;++scenario)
        {
            DlssNr::NativeTeardownAttempt attempt;DlssNr::NativeTeardownObservationV1 observed;
            unsigned calls=0;
            auto shutdown=[&]() -> bool {++calls;if(scenario==0)throw std::runtime_error("shutdown uncertain");return scenario==2;};
            attempt.Observe(observed,shutdown);
            check(calls==0&&!observed.nrShutdownAttempted&&!observed.nrShutdownSucceeded,
                  "undrained source preflight never consumes the actual shutdown attempt");
            observed.sourceClosed=1;attempt.Observe(observed,shutdown);
            check(calls==1&&observed.nrShutdownAttempted==1&&observed.nrShutdownSucceeded==(scenario==2?1u:0u),
                  "actual shutdown reports thrown false and successful outcomes without retry");
            attempt.Observe(observed,shutdown);
            check(calls==1&&observed.nrShutdownAttempted==1&&!observed.nrShutdownSucceeded,
                  "repeat after throw false or success cannot reenter or manufacture success");
        }
        DlssNr::NativeTeardownAttempt attempt;DlssNr::NativeTeardownObservationV1 observed;observed.sourceClosed=1;
        unsigned nestedCalls=0;
        attempt.Observe(observed,[&]{DlssNr::NativeTeardownObservationV1 nested;nested.sourceClosed=1;
            attempt.Observe(nested,[&]{++nestedCalls;return true;});
            check(nested.nrShutdownAttempted==1&&!nested.nrShutdownSucceeded,"reentrant shutdown observes retained uncertainty");
            return false;});
        check(nestedCalls==0&&!observed.nrShutdownSucceeded,"reentrant attempt cannot enter foreign shutdown");
    }
    {
        DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
        auto host=A::Open();auto callback=A::Capture(*host,registry);
        auto active=A::Teardown(*host);
        check(active.sourceClosed==1&&active.callbacks==1&&!DlssNr::SourceCapturesDrained(active),
              "teardown closes ingress but refuses a live callback");
        callback.reset();auto drained=A::Teardown(*host);
        check(DlssNr::SourceCapturesDrained(drained),"terminal source with no captures reports capture drainage");
        check(!drained.scopeClosed&&!drained.finalizerStopped&&!drained.nrShutdownAttempted&&!drained.nrShutdownSucceeded,
              "capture drainage never invents aggregate closure or renderer shutdown");
        auto malformed=drained;malformed.version=2;
        check(!DlssNr::SourceCapturesDrained(malformed),"teardown ABI mismatch refuses drainage");
        malformed=drained;malformed.opaqueCaptures=1;
        check(!DlssNr::SourceCapturesDrained(malformed),"outstanding opaque capture refuses drainage");
        malformed=drained;malformed.controlledProducers=1;
        check(!DlssNr::SourceCapturesDrained(malformed),"outstanding producer refuses drainage");
    }
    for(unsigned closingRace=0;closingRace<2;++closingRace)
    {
        // Synthetic renderer receipt exercises owner matching only. No actual
        // model/provider execution or renderer shutdown is asserted here.
        DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
        auto host=A::Open();auto callback=A::Capture(*host,registry);A::Bind(*host,*callback,list.Get(),&parameters);
        Settings settings;Model model;model.ready=true;
        model.resources={&model,textures[4].Get(),textures[5].Get(),textures[6].Get(),16,8};
        model.resources.providerUses=std::make_shared<DlssNr::NativeProviderRetirementOwner>(
            DlssNr::ProviderLifetimeContract::ControlledCallerList(),C::ProviderIncarnation{});
        const auto active=A::Materialize(*host,*callback,settings,[&]{return std::make_unique<Renderer>(model);});
        check(active.current.has_value(),"aggregate test uses actual materialized coordinator");
        const auto oldSession=A::Owners(*host)->Session();
        auto& resources=A::Owners(*host)->Resources();
        check(L::NativeResourceRegistryTestAccess::FixtureContent(resources,textures[5].Get()),"synthetic content permits real read-reservation test");
        const auto* view=resources.Observe(textures[5].Get());
        auto read=L::NativeResourceRegistryTestAccess::Borrow(resources,textures[5].Get(),*view);
        check(read.has_value(),"aggregate test holds actual resource read reservation");
        callback.reset();A::Teardown(*host);
        check(!A::Recreate(*host),"closed source cannot recreate before aggregate retirement");
        auto other=std::make_shared<DlssNr::NativeProviderRetirementOwner>(
            DlssNr::ProviderLifetimeContract::ControlledCallerList(),C::ProviderIncarnation{});
        auto foreign=DlssNr::NativeRendererRetirementTestAccess::Receipt(other);
        check(!A::Complete(*host,foreign).scopeClosed,"foreign renderer owner cannot retire scope");
        auto receipt=DlssNr::NativeRendererRetirementTestAccess::Receipt(model.resources.providerUses);
        check(!A::Complete(*host,receipt).scopeClosed,"unreleased actual SR feature blocks aggregate retirement");
        auto release=registry.BeginRelease(7,registry.Read(7));
        check(release.has_value(),"aggregate feature has no retained callback pin");release->Complete(true);release.reset();
        check(!A::Complete(*host,receipt).scopeClosed,"live Resource reader blocks aggregate retirement");
        check(A::Retirement(*host).owners[static_cast<std::size_t>(O::InitialOwner::Resource)]==O::Retirement::Outstanding,
            "Resource domain preserves its own outstanding reader");
        read.reset();check(A::CloseKernel(*host,receipt),"existing kernel closes before physical detach fault injection");
        L::NativeResourceRegistryTestAccess::BlockPhysicalDetach(resources,true);
        check(!A::Complete(*host,receipt).scopeClosed,"physical detach refusal never reports controlled aggregate success");
        L::NativeResourceRegistryTestAccess::BlockPhysicalDetach(resources,false);
        const auto closed=A::Complete(*host,receipt);
        check(closed.scopeClosed&&closed.finalizerStopped,"matched owners close existing coordinator and finalizer");
        check(A::Complete(*host,receipt).scopeClosed,"repeat retirement observes closed scope without repeating shutdown");
        if(closingRace)
        {
            check(A::RefuseFreshPublication(*host),"global-close publication refusal keeps fresh root closed");
            registry.Set(7,12,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
            check(!registry.Pin(7,registry.Read(7)),"failed fresh publication re-closes registry admission");
            check(!A::Recreate(*host),"failed publication cannot retry old generation");
            continue;
        }
        bool racingRefused=false;auto recreated=A::RecreateRacing(*host,registry,racingRefused);
        check(bool(recreated),"fully retired scope authorizes registry next epoch");
        check(racingRefused,"old query stays closed while fully ready fresh root is atomically published");
        check(!A::Recreate(*host),"old scope recreation guard cannot publish twice");
        check(!A::Recreate(*recreated),"fresh scope cannot reuse old retirement authorization");
        registry.Set(7,12,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
        A::Teardown(*host);
        {auto pin=registry.Pin(7,registry.Read(7));check(bool(pin),"stale old teardown cannot stop recreated registry epoch");}
        auto fresh=A::Capture(*recreated,registry);
        check(fresh&&A::Owners(*recreated)->Session()!=oldSession,"new scope issues fresh session identity");
    }
    for(unsigned scenario=0;scenario<5;++scenario)
    {
        DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
        auto host=A::Open();auto callback=A::Capture(*host,registry);A::Bind(*host,*callback,list.Get(),&parameters);
        Neurotic::D3D12::NativeRecordingObserver observer;
        if(scenario==1||scenario==2)
        {
            if(scenario==2)observer.SetHookCoverage(true);
            observer.ResetNative(list.Get(),nullptr);
        }
        P::NativeProtocolResult result;
        if(scenario<3)result=A::CaptureUnavailable(*host,*callback,observer.Observe(list.Get()));
        else
        {
            if(scenario==4)
            {
                Settings settings;
                A::Materialize(*host,*callback,settings,[]{return std::unique_ptr<Renderer>{};});
            }
            result.reason=P::Symbol(scenario==3?"Native.CommandStateCaptureFailed":"Native.InitializationUnavailable");
            result=A::RecordResult(*host,*callback,result);
        }
        const std::array expected={"Native.RecordingUnavailable","Native.HookCoverageIncomplete",
            "Native.CommandStateUnavailable","Native.CommandStateCaptureFailed","Native.Initialization.Preflight"};
        A::Return(*callback,true);
        DlssNr::NativeHostReturnObservationV1 observed;
        check(A::Query(*host,0,7,list.Get(),textures[1].Get(),observed)==1&&
              std::string_view(observed.reason)==expected[scenario]&&result.reason.View()==expected[scenario],
              "early refusal and preparation detail survive the actual callback return observation");
        check(observed.status==1&&observed.runtimePreparation==0&&observed.evaluation==0&&
              observed.recordingIncarnation==0&&(observed.restorationFlags&3u)==0,
              "refusal diagnostics grant no evaluation, recording, or restoration authority");
    }
    for(unsigned scenario=0;scenario<9;++scenario)
    {
        parameters.renderWidth=scenario==5?12:16;parameters.colorX=scenario==5?2:0;
        api(list->Close());api(list->Reset(allocator.Get(),nullptr));
        Neurotic::D3D12::NativeRecordingObserver observer;observer.SetHookCoverage(true);
        check(observer.ResetNative(list.Get(),nullptr),"selected SR records actual WARP list identity after Reset");
        DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
        auto host=A::Open();auto callback=A::Capture(*host,registry);A::Bind(*host,*callback,list.Get(),&parameters);
        const auto* initialSource=callback->ObservedSource();
        const auto* initialFrame=S::ResolveMetadata(initialSource->Source()->seed.frame,*initialSource);
        check(initialFrame&&S::Established(initialFrame->episodeId)&&callback->TransactionObservation()&&
              initialFrame->episodeId.KnownPart()->value==callback->TransactionObservation()->Subject().episode&&
              !S::Established(initialFrame->baseRealFrameId),
              "actual publication joins issued episode without inventing original base identity");
        const auto subjectRef=initialSource->TransactionSubject();
        const auto* issued=subjectRef?S::ResolveMetadata(*subjectRef,*initialSource):nullptr;
        check(issued&&subjectRef->owner==C::OwnerDomain::IdentityRegistry&&
              *issued==callback->TransactionObservation()->Subject()&&issued->sample==callback->Sample(),
              "actual Identity metadata resolves exact enrollment subject and Resource device");
        auto wrongOwner=*subjectRef;wrongOwner.owner=C::OwnerDomain::Provider;
        check(!S::ResolveMetadata(wrongOwner,*initialSource),"subject cannot resolve under a foreign owner");
        check(callback->InvocationEvidenceGaps().gameInputContent&&
              !callback->InvocationEvidenceGaps().invocationInputProvenance,
            "actual source publication supplies callback provenance while physical game contents stay unknown");
        auto operation=A::BeginSr(*host,*callback,&parameters,observer.Observe(list.Get()));
        check(operation&&operation->receipt&&operation->receipt->MarkEntered(),"selected operation enters the source and Resource joined receipt");
        if(scenario!=4){list->CopyResource(textures[1].Get(),textures[0].Get());observer.Work(list.Get());}
        if(scenario==2)parameters.resources[0]=textures[4].Get();
        if(scenario==6)--parameters.renderWidth;
        if(scenario==3){api(list->Close());api(list->Reset(allocator.Get(),nullptr));observer.ResetNative(list.Get(),nullptr);}
        A::EndSr(*host,*callback,*operation,&parameters,scenario==1?1:0,observer.Observe(list.Get()));
        const bool wrote=scenario==0||scenario==5||scenario>=7;
        check(bool(callback->RequiredSrInput())==wrote,"only connected selected SR success publishes caller output for Native After");
        check(operation->receipt->PossibleEffects(),"entry uncertainty survives all SR return paths");
        check(!S::CompleteContent(*callback->RegisteredResource("Color")),"SR return never stamps a game input revision");
        if(wrote)
        {
            check(!callback->InvocationEvidenceGaps().hostRecordingUse&&callback->ObservedSource()->Complete(),
                "Resource commit and fresh source publication retain the observed local action");
            const auto* refreshed=callback->ObservedSource();const auto refreshedRef=refreshed->TransactionSubject();
            const auto* retained=refreshedRef?S::ResolveMetadata(*refreshedRef,*refreshed):nullptr;
            check(retained&&*retained==callback->TransactionObservation()->Subject(),
                  "selected SR publication retains same issued transaction through new metadata arena");
        }
        else check(!S::CompleteContent(*callback->RegisteredResource("Output")),"failed or incomplete SR keeps output content unknown");
        if(scenario==0)
        {
            Settings settings;settings.DlssNrRunBeforeSr.v=false;
            Model model;model.ready=true;
            model.resources={&model,textures[4].Get(),textures[5].Get(),textures[6].Get(),16,8};
            const auto active=A::Materialize(*host,*callback,settings,[&]{return std::make_unique<Renderer>(model);});
            check(active.current.has_value(),"post-SR publication activates Native After preparation");
            const auto exact=A::CurrentPreparation(*callback);auto* owners=A::Owners(*host);
            P::RecipeRequest request;
            static_cast<P::NativeProfilePreparationRequest&>(request)=exact->request;
            request.recipeHeader=owners->ProtocolJournal().Header(C::ContractId::C06,callback->Scope().scope);
            request.externalPlan=P::Reference<C::ContractId::C05>(active.current->Plan());
            request.nativePlan=exact->Publish(active.current->Plan(),C::OwnerDomain::StreamCoordinator);
            const auto built=P::BuildRecipe(request,*exact);
            if(!built.product)std::cerr<<"Native After recipe refusal: "<<built.reason.View()<<'\n';
            check(built.product.has_value(),"actual committed SR output builds executable Native After recipe");
            const auto mapped=P::BuildNativeBindingMap(built.product->Value(),*exact);
            check(mapped.has_value(),"actual committed SR output maps Native After bindings");
            auto metadataOnly=MetadataOnly{*exact};
            check(P::Qualify(request,metadataOnly).View()=="Native.PostSrOutputPending"&&
                  !P::BuildNativeBindingMap(built.product->Value(),metadataOnly),
                  "known output metadata without selected operation provenance cannot qualify After");
            auto wrongSample=callback->Sample();++wrongSample.producerOrdinal;
            auto output=*callback->RegisteredResource("Output");
            check(exact->SelectedSrOutput(callback->Sample(),output)&&!exact->SelectedSrOutput(wrongSample,output),
                  "selected SR output is bound to the exact invocation sample");
            auto wrongOutput=output;wrongOutput.identity.contentRevision={};
            check(!exact->SelectedSrOutput(callback->Sample(),wrongOutput),"unknown output revision cannot inherit selected SR provenance");
            wrongOutput=output;auto generation=output.identity.resourceGeneration.KnownPart()->value;++generation.identity.value;
            wrongOutput.identity.resourceGeneration=C::OptionalFact<C::GenerationToken>::FromKnown(generation,C::EvidenceRef{request.recipeHeader.record});
            check(!exact->SelectedSrOutput(callback->Sample(),wrongOutput),"changed output allocation cannot inherit selected SR provenance");
            auto changed=request;++changed.representations.Get(0)->sourceCandidate->record.value;
            check(!P::BuildRecipe(changed,*exact).product,"different C01 binding cannot borrow the selected output claim");
            changed=request;changed.representations.Get(0)->currentContent={};
            check(!P::BuildRecipe(changed,*exact).product,"changed current content refuses a selected output recipe");
            check(!S::CompleteContent(*callback->RegisteredResource("Depth"))&&
                  !S::CompleteContent(*callback->RegisteredResource("MotionVectors")),
                  "After guide bindings preserve unknown original game content revisions");
        }
        const auto returnedObservation=callback->TransactionObservation();
#ifdef NR_NATIVE_SELECTED_OUTPUT_READ_V1
        check(!returnedObservation->AcquireSelectedOutputRead(textures[1].Get()),
              "pending source cannot supply a selected-output read");
#endif
        if(scenario==8)callback.reset();
        else A::Return(*callback,scenario!=1&&scenario!=7);
        check(returnedObservation->Status()==(scenario==8?L::SourceAssociationReason::Abandoned:
              (scenario==1||scenario==7)?L::SourceAssociationReason::Failed:
              (scenario==0||scenario==5)?L::SourceAssociationReason::MissingConsumerRights:
              L::SourceAssociationReason::OutputUnavailable),
              "actual callback cleanup records terminal return without abandoning a returned source");
        DlssNr::NativeHostReturnObservationV1 status;
        check(A::Query(*host,0,7,list.Get(),textures[1].Get(),status)==1&&status.status==1&&
              status.evaluation==0&&status.stageOutcome==0&&status.sequence!=0&&status.callbackRecord!=0,
              "source-only return observation never fabricates a Native invocation or HostReturn");
        const auto sequence=status.sequence;
        check((status.restorationFlags&4u)==(scenario==8?0u:4u),
              "abandoned source observation does not claim actual outer return");
        check(!A::Query(*host,sequence,7,list.Get(),textures[1].Get(),status)&&
              !A::Query(*host,0,8,list.Get(),textures[1].Get(),status)&&
              !A::Query(*host,0,7,list.Get(),textures[0].Get(),status),
              "live observation refuses stale baseline and other caller bindings");
        if(scenario==0||scenario==5)
        {
#ifdef NR_NATIVE_SELECTED_OUTPUT_READ_V1
            auto read=returnedObservation->AcquireSelectedOutputRead(textures[1].Get());
            check(read&&read->Current()&&read->Native()==textures[1].Get()&&
                  read->View()==returnedObservation->SelectedOutput()->view,
                  "returned source borrows the exact selected Resource publication after callback cleanup");
            check(!returnedObservation->AcquireSelectedOutputRead(textures[0].Get()),
                  "selected-output read refuses a different native resource");
            A::Close(*host);
            check(!read->Current()&&!returnedObservation->AcquireSelectedOutputRead(textures[1].Get()),
                  "source closure revokes held and future selected-output reads");
#else
            check(false,"returned source borrows the exact selected Resource publication after callback cleanup");
#endif
        }
#ifdef NR_NATIVE_SELECTED_OUTPUT_READ_V1
        else check(!returnedObservation->AcquireSelectedOutputRead(textures[1].Get()),
                   "failed or incomplete source has no selected-output read authority");
#endif
        parameters.resources[0]=textures[0].Get();
    }
    parameters.renderWidth=16;parameters.colorX=0;
    // Real WARP source/Resource publication through materialization and recipe
    // qualification. SDK/provider execution is intentionally absent here.
    for(unsigned scenario=0;scenario<4;++scenario)
    {
        DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
        auto host=A::Open();Settings settings;settings.DlssNrRunBeforeSr.v=false;
        Model model;model.ready=true;model.resources={&model,textures[4].Get(),textures[5].Get(),textures[6].Get(),16,8};
        api(list->Close());api(list->Reset(allocator.Get(),nullptr));
        Neurotic::D3D12::NativeRecordingObserver observer;observer.SetHookCoverage(true);
        check(observer.ResetNative(list.Get(),nullptr),"selected FG fixture observes actual recording");
        auto callback=A::Capture(*host,registry);A::Bind(*host,*callback,list.Get(),&parameters);
        auto operation=A::BeginSr(*host,*callback,&parameters,observer.Observe(list.Get()));
        check(operation&&operation->receipt->MarkEntered(),"selected FG fixture starts existing selected SR receipt");
        list->CopyResource(textures[1].Get(),textures[0].Get());observer.Work(list.Get());
        A::EndSr(*host,*callback,*operation,&parameters,0,observer.Observe(list.Get()));
        check(bool(callback->RequiredSrInput()),"selected FG fixture retains actual source output publication");
        A::SelectControlledFixture(*host,*callback,scenario,list.Get(),textures[1].Get());
        const auto active=A::Materialize(*host,*callback,settings,[&]{return std::make_unique<Renderer>(model);});
        if(scenario)
        {
            check(!active.current,scenario==1?"wrong feature refuses selected FG materialization":
                scenario==2?"wrong feature generation refuses selected FG materialization":"missing enrollment refuses selected FG materialization");
            A::Return(*callback,true);continue;
        }
        if(!active.current)std::cerr<<"FG materialization refusal: "<<callback->RuntimePreparationReason().View()<<'\n';
        check(bool(active.current),"selected FG creates supported immutable route before Native work");
        auto store=A::CurrentPreparation(*callback);auto* owners=A::Owners(*host);
        const auto* delivery=S::ResolveMetadata(*active.current->Anchor().nativeDelivery,*store);
        check(delivery&&delivery->kind==C::NativeDeliveryKind::FinalConsumerRequired&&delivery->finalConsumer.has_value(),
            "selected FG delivery is FinalConsumerRequired at original commit");
        const auto projection=P::PrepareNativeProfile(store->request,*store);
        check(bool(projection.projection),"selected FG structural preparation succeeds without executing provider");
        P::RecipeRequest request;static_cast<P::NativeProfilePreparationRequest&>(request)=store->request;
        request.recipeHeader=owners->ProtocolJournal().Header(C::ContractId::C06,callback->Scope().scope);
        request.externalPlan=P::Reference<C::ContractId::C05>(active.current->Plan());
        request.nativePlan=store->Publish(active.current->Plan(),C::OwnerDomain::StreamCoordinator);
        const auto built=P::BuildRecipe(request,*store);
        check(bool(built.product),"selected FG builds executable recipe from actual selected output publication");
        check(built.product->Value().profileCertificate.purpose.View()=="RENDER.NativeBindings",
            "selected FG actual recipe has RENDER.NativeBindings qualification");
        check(projection.projection->Value().profileCertificate.purpose.View()=="RENDER.NativeStructuralBindings",
            "structural preparation remains distinct from executable binding qualification");
        const auto* frame=S::ResolveMetadata(built.product->Value().recipe.frame,*store);
        check(frame&&!S::Established(frame->baseRealFrameId)&&!frame->evaluationId.IsKnown(),
            "selected FG route preserves unknown original game identity");
        A::ChangeControlledEnrollment(*host);
        auto later=A::Capture(*host,registry);A::Bind(*host,*later,list.Get(),&parameters);
        check(!A::Materialize(*host,*later,settings,[&]{return std::make_unique<Renderer>(model);}).current,
            "different FG enrollment cannot inherit earlier immutable route");
        A::Return(*later,true);A::Return(*callback,true);
    }
    {
        DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
        auto host=A::Open();Settings settings;settings.DlssNrRunBeforeSr.v=false;
        Model model;model.ready=true;
        model.resources={&model,textures[4].Get(),textures[5].Get(),textures[6].Get(),16,8};
        std::optional<O::MaterializationRecord> initial;
        for(unsigned frame=0;frame<2;++frame)
        {
            api(list->Close());api(list->Reset(allocator.Get(),nullptr));
            Neurotic::D3D12::NativeRecordingObserver observer;observer.SetHookCoverage(true);
            check(observer.ResetNative(list.Get(),nullptr),"repeated After observes fresh recording");
            auto callback=A::Capture(*host,registry);A::Bind(*host,*callback,list.Get(),&parameters);
            auto operation=A::BeginSr(*host,*callback,&parameters,observer.Observe(list.Get()));
            check(operation&&operation->receipt->MarkEntered(),"repeated After enters selected source operation");
            list->CopyResource(textures[1].Get(),textures[0].Get());observer.Work(list.Get());
            A::EndSr(*host,*callback,*operation,&parameters,0,observer.Observe(list.Get()));
            check(bool(callback->RequiredSrInput()),"repeated After commits genuine selected output publication");
            const auto active=A::Materialize(*host,*callback,settings,[&]{return std::make_unique<Renderer>(model);});
            check(bool(active.current),"repeated After retains active route");
            auto store=A::CurrentPreparation(*callback);auto* owners=A::Owners(*host);
            if(initial)store->committedRoute=std::static_pointer_cast<const std::remove_reference_t<decltype(*store)>>(initial->metadata);
            P::RecipeRequest request;static_cast<P::NativeProfilePreparationRequest&>(request)=store->request;
            request.recipeHeader=owners->ProtocolJournal().Header(C::ContractId::C06,callback->Scope().scope);
            request.externalPlan=P::Reference<C::ContractId::C05>(active.current->Plan());
            request.nativePlan=store->Publish(active.current->Plan(),C::OwnerDomain::StreamCoordinator);
            auto built=P::BuildRecipe(request,*store);
            check(bool(built.product),"repeated After builds fresh recipe from selected output");
            if(!initial)
            {
                auto projection=P::PrepareNativeProfile(store->request,*store);
                check(bool(projection.projection),"initial After structural projection is retained");
                initial.emplace();initial->enrollment=active.current->Scope();initial->plan=active.current->Plan();
                initial->anchor=active.current->Anchor();initial->initialSample=callback->Sample();
                initial->preparation=projection.projection->Value();initial->metadata=store;
                initial->delivery=*S::ResolveMetadata(*initial->plan.nativeDelivery,*store);
            }
            P::NativeInvocationSnapshot snapshot;snapshot.product=built.product->Value();
            snapshot.frame=*S::ResolveMetadata(snapshot.product.recipe.frame,*store);
            snapshot.anchor=active.current->Anchor();snapshot.generation=active.current->Plan().routeGeneration.KnownPart()->value;
            snapshot.consumer=callback->Sample().callback;snapshot.recording=snapshot.consumer;snapshot.reservation=snapshot.consumer;
            snapshot.generations=S::ResolveMetadata(snapshot.product.inputContext,*store)->generations;
            // Descriptive binding only. The test never calls BeginConsumption.
            L::LeaseBinding binding;auto& description=binding.description;
            description.header=owners->ProtocolJournal().Header(C::ContractId::C03,callback->Scope().scope);
            description.header.owner=C::OwnerDomain::Resource;description.context=snapshot.product.recipe.context;
            description.candidate=*snapshot.product.inputRepresentations.Get(0)->sourceCandidate;
            const auto output=*callback->RegisteredResource("Output");description.resource=output.identity;
            description.device=output.device;description.generations=snapshot.generations;
            description.use=C::UsageKind::OrderedGpuRead;description.commandScope.key=snapshot.recording;
            const C::EvidenceRef evidence{description.header.record};
            description.revoked=C::OptionalFact<bool>::FromKnown(false,evidence);
            description.revocationEpoch=C::OptionalFact<C::RecordKey>::FromKnown(snapshot.reservation,evidence);
            description.expirationBoundary=C::OptionalFact<C::RecordKey>::FromKnown(snapshot.consumer,evidence);
            description.submissionRecheckRequired=C::OptionalFact<bool>::FromKnown(true,evidence);
            binding.consumer=snapshot.consumer;binding.purpose=P::Symbol("ColorModelInput");
            binding.plan=initial->plan.header.record;binding.profile=snapshot.product.profileCertificate.header.record;
            binding.route=C::GenerationToken{snapshot.generation.Describe()};
            binding.sameRecording=L::SameRecordingUse{snapshot.recording,snapshot.reservation,1,2};
            check(L::ValidBinding(binding),"route fixture binding is structurally valid without live rights");
            snapshot.leases.Push(binding);
            check(MetadataSealProbe::Seal(snapshot,store),"real selected After closure passes production metadata seal");
            if(frame)check(snapshot.product.dependencies.structuralPlans!=initial->preparation.dependencies.structuralPlans,
                "later source callback issues genuinely fresh structural C12 keys");
            check(P::NativeInvocationMatches(snapshot,*initial),frame?
                "second actual selected After callback matches retained route":
                "initial actual selected After callback matches retained route");
            check(!snapshot.frame.evaluationId.IsKnown()&&snapshot.product.recipe.evaluation.Check()==C::Error::None,
                "HostReturn keeps upstream evaluation unknown and uses separately issued NR evaluation");
            auto changed=snapshot;++changed.product.recipe.committedPlan.revision;
            check(!P::NativeInvocationMatches(changed,*initial),"fresh After does not bypass exact plan revision");
            changed=snapshot;changed.product.dependencies.placement=C::Placement::NativeBefore;
            check(!P::NativeInvocationMatches(changed,*initial),"fresh After does not bypass placement dependencies");
            changed=snapshot;auto otherEvaluation=snapshot.product.recipe.evaluation;++otherEvaluation.value;
            changed.frame.evaluationId=C::OptionalFact<C::EvaluationId>::FromKnown(otherEvaluation,evidence);
            check(!P::NativeInvocationMatches(changed,*initial),"known conflicting upstream evaluation remains refused");
            A::Return(*callback,true);
        }
    }
    {
        DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));auto host=A::Open();
        Neurotic::D3D12::NativeRecordingObserver observer;observer.SetHookCoverage(true);
        api(list->Close());api(list->Reset(allocator.Get(),nullptr));observer.ResetNative(list.Get(),nullptr);
        auto first=A::Capture(*host,registry);A::Bind(*host,*first,list.Get(),&parameters);
        auto initial=A::BeginSr(*host,*first,&parameters,observer.Observe(list.Get()));
        check(initial&&initial->receipt->MarkEntered(),"final-output transport fixture enters initial selected writer");
        list->CopyResource(textures[1].Get(),textures[0].Get());observer.Work(list.Get());
        A::EndSr(*host,*first,*initial,&parameters,0,observer.Observe(list.Get()));
        const auto source=first->TransactionObservation();const auto prior=source->SelectedOutput();
        check(bool(prior),"initial selected writer supplies prior content publication");
        // Exercise only transport of two real Resource publications here. The
        // second controlled SR writer is not labeled as a Native model pass.
        auto second=A::Capture(*host,registry);A::Bind(*host,*second,list.Get(),&parameters);
        DlssNr::NativeHostReturnObservationV1 ambiguous;
        check(!A::Query(*host,0,7,list.Get(),textures[1].Get(),ambiguous),
              "overlapping callbacks cannot alias the diagnostic latest return");
        auto later=A::BeginSr(*host,*second,&parameters,observer.Observe(list.Get()));
        check(later&&later->receipt->MarkEntered(),"final-output transport fixture enters subsequent selected writer");
        list->CopyResource(textures[1].Get(),textures[0].Get());observer.Work(list.Get());
        A::EndSr(*host,*second,*later,&parameters,0,observer.Observe(list.Get()));
        const auto output=second->RequiredSrInput();const auto recorded=observer.Observe(list.Get());
        check(output&&output->view.identity!=prior->view.identity,"subsequent actual write changes Resource content revision");
        const auto advance=[&](std::uint64_t recording,std::uint64_t ordinal){
            return L::SourceTransactionTestAccess::Advance(*source,output->view,recording,ordinal,
                A::ReadFactory(*host,textures[1].Get(),output->view));};
        check(!L::SourceTransactionTestAccess::Advance(*source,*second->RegisteredResource("Color"),
              recorded.incarnation,recorded.workOrdinal,A::ReadFactory(*host,textures[0].Get(),
              *second->RegisteredResource("Color"))),"final-output refresh rejects another resource and unknown contents");
        check(!advance(recorded.incarnation+1,recorded.workOrdinal),"final-output refresh rejects another recording");
        check(!advance(recorded.incarnation,prior->workOrdinal),"final-output refresh rejects nonadvancing work");
        check(advance(recorded.incarnation,recorded.workOrdinal),"final-output refresh follows a later exact Resource publication");
        A::Return(*first,true);
        auto read=source->AcquireSelectedOutputRead(textures[1].Get());
        check(read&&read->Current()&&read->View()==output->view,
              "returned source reads refreshed content instead of obsolete SR revision");
        check(!advance(recorded.incarnation,recorded.workOrdinal+1),"terminal source rejects later publication changes");
    }
    {
        DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));auto host=A::Open();
        Neurotic::D3D12::NativeRecordingObserver observer;observer.SetHookCoverage(true);
        api(list->Close());api(list->Reset(allocator.Get(),nullptr));observer.ResetNative(list.Get(),nullptr);
        auto first=A::Capture(*host,registry);A::Bind(*host,*first,list.Get(),&parameters);
        auto operation=A::BeginSr(*host,*first,&parameters,observer.Observe(list.Get()));
        check(operation&&operation->receipt->MarkEntered(),"publication pin fixture enters selected SR");
        list->CopyResource(textures[1].Get(),textures[0].Get());observer.Work(list.Get());
        A::EndSr(*host,*first,*operation,&parameters,0,observer.Observe(list.Get()));
        const auto output=first->RequiredSrInput();
        check(bool(output),"selected SR publication tail preserves successful full output");
        const auto source=first->TransactionObservation();
        A::Return(*first,true);
        auto pinned=source->AcquireSelectedOutputRead(textures[1].Get());
        check(pinned&&pinned->Current(),"existing Resource owner pins the exact committed SR output");
        auto second=A::Capture(*host,registry);A::Bind(*host,*second,list.Get(),&parameters);
        auto conflicting=A::BeginSr(*host,*second,&parameters,observer.Observe(list.Get()));
        check(conflicting&&conflicting->receipt->MarkEntered(),"original SR still enters after publication tracking conflict");
        list->CopyResource(textures[1].Get(),textures[0].Get());observer.Work(list.Get());
        A::EndSr(*host,*second,*conflicting,&parameters,0,observer.Observe(list.Get()));
        check(!pinned->Current()&&pinned->View()==output->view,"conflicting selected SR revokes pin but preserves historical snapshot");
        check(!source->AcquireSelectedOutputRead(textures[1].Get()),
              "retained source cannot reacquire a stale selected revision after a conflicting write");
        check(!second->RequiredSrInput()&&!first->RequiredSrInput()&&
              !S::CompleteContent(*second->RegisteredResource("Output")),
            "untracked selected SR cannot leave an earlier known output visible");
        pinned.reset();
        auto third=A::Capture(*host,registry);A::Bind(*host,*third,list.Get(),&parameters);
        auto retry=A::BeginSr(*host,*third,&parameters,observer.Observe(list.Get()));
        check(retry&&retry->receipt->MarkEntered(),"later original SR remains callable after pin destruction");
        list->CopyResource(textures[1].Get(),textures[0].Get());observer.Work(list.Get());
        A::EndSr(*host,*third,*retry,&parameters,0,observer.Observe(list.Get()));
        check(!third->RequiredSrInput(),"pin destruction does not launder uncertain output into a new publication");
    }
    {
        DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));auto host=A::Open();
        Neurotic::D3D12::NativeRecordingObserver observer;observer.SetHookCoverage(true);
        for(unsigned frame=0;frame<22;++frame)
        {
            api(list->Close());api(list->Reset(allocator.Get(),nullptr));observer.ResetNative(list.Get(),nullptr);
            auto callback=A::Capture(*host,registry);A::Bind(*host,*callback,list.Get(),&parameters);
            auto operation=A::BeginSr(*host,*callback,&parameters,observer.Observe(list.Get()));
            check(operation&&operation->receipt&&operation->receipt->MarkEntered(),"bounded SR retention permits later callbacks after exact terminal recording");
            list->CopyResource(textures[1].Get(),textures[0].Get());observer.Work(list.Get());
            A::EndSr(*host,*callback,*operation,&parameters,frame==20?1:0,observer.Observe(list.Get()));
            check(bool(callback->RequiredSrInput())==(frame<20),"success then partial failure cannot be retried into a valid caller Output");
            if(frame>=20)check(!S::CompleteContent(*callback->RegisteredResource("Output")),
                "partial failure invalidates previously known output while preserving pending reservation");
        }
    }
    {
        DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));auto host=A::Open();
        Neurotic::D3D12::NativeRecordingObserver observer;observer.SetHookCoverage(true);
        api(list->Close());api(list->Reset(allocator.Get(),nullptr));observer.ResetNative(list.Get(),nullptr);
        // Keep the exact recording nonterminal so all sixteen retained captures
        // remain live. The next original SR call is still permitted to execute.
        for(unsigned frame=0;frame<17;++frame)
        {
            auto callback=A::Capture(*host,registry);A::Bind(*host,*callback,list.Get(),&parameters);
            auto operation=A::BeginSr(*host,*callback,&parameters,observer.Observe(list.Get()));
            if(frame<16)
            {
                check(operation&&operation->receipt&&operation->receipt->MarkEntered(),"nonterminal SR capture retains its bounded slot");
                list->CopyResource(textures[1].Get(),textures[0].Get());observer.Work(list.Get());
                A::EndSr(*host,*callback,*operation,&parameters,0,observer.Observe(list.Get()));
                check(bool(callback->RequiredSrInput()),"retained selected SR publishes known caller Output");
            }
            else
            {
                check(!operation,"full SR capture table refuses additional tracking");
                list->CopyResource(textures[1].Get(),textures[0].Get());observer.Work(list.Get());
                A::ObserveSrReturn(*host,*callback,&parameters);
                auto next=A::Capture(*host,registry);A::Bind(*host,*next,list.Get(),&parameters);
                check(!S::CompleteContent(*next->RegisteredResource("Output"))&&!callback->RequiredSrInput(),
                    "unretained original SR invalidates old output and cannot publish a successful receipt");
            }
        }
    }
    {
        auto empty=A::Open();bool refused=false,dependentReached=false;
        try{(void)A::RetainedSample(*empty);dependentReached=true;}
        catch(const FixtureDependencyRefusal&){refused=true;}
        check(refused&&!dependentReached,"absent initialization refuses before dependent sample dereference");
        bool guarded=false;
        try{RequireFixture(false,"controlled prerequisite refusal");dependentReached=true;}
        catch(const FixtureDependencyRefusal&){guarded=true;}
        check(guarded&&!dependentReached,"failed prerequisite aborts dependent fixture operations");
    }
    {
        DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
        auto host=A::Open();auto callback=A::Capture(*host,registry);
        check(callback&&A::Bind(*host,*callback,list.Get(),&parameters),"post-SR owner captures original callback before host work");
        if(callback)
        {
            const auto gaps=callback->InvocationEvidenceGaps();
            check(gaps.gameInputContent&&gaps.hostRecordingUse,
                "captured host input and list expose missing production invocation evidence");
            const auto* before=callback->ObservedSource();
            check(before&&!S::CompleteContent(*callback->RegisteredResource("Output")),
                "source capture cannot invent a future SR output write");
            auto ticket=DlssNr::GpuSafety::Record(list.Get());
            auto wrongAction=DlssNr::GpuSafety::BeginLocalAction(ticket,list.Get(),textures[0].Get());
            check(wrongAction&&!A::RefreshHostOutput(*host,*callback,*wrongAction),
                "foreign resource action cannot consume caller Output publication");
            wrongAction.reset();
            auto action=DlssNr::GpuSafety::BeginLocalAction(ticket,list.Get(),textures[1].Get());
            check(action&&action->Current(),"actual copy owner retains exact recording action");
            check(action&&!A::RefreshHostOutput(*host,*callback,*action),
                "no SR return cannot publish an output source revision");
            list->CopyResource(textures[1].Get(),textures[0].Get());
            check(A::ObserveSrReturn(*host,*callback,&parameters),
                "controlled SR operation records its return after the actual copy");
            check(!callback->RequiredSrInput(),
                "successful SR return alone cannot supply a versioned NR input");
            const bool refreshed=action&&A::RefreshHostOutput(*host,*callback,*action);
            check(refreshed&&S::CompleteContent(*callback->RegisteredResource("Output"))&&
                  callback->ObservedSource()!=before&&callback->ObservedSource()->Complete(),
                "actual recorded host SR output is versioned before fresh source publication");
            check(!callback->RequiredSrInput(),
                "fixture-only output publication is not an opaque SR operation receipt");
            const auto afterWrite=callback->InvocationEvidenceGaps();
            check(afterWrite.gameInputContent&&afterWrite.hostRecordingUse,
                "one owned output write cannot certify game inputs or host replay rights");
            check(action&&!A::RefreshHostOutput(*host,*callback,*action),"same callback cannot duplicate the output write publication");
        }
    }
    {
        Settings settings;DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));const auto snapshot=registry.Read(7);
        auto host=A::Open();auto callback=A::Capture(*host,registry);A::Bind(*host,*callback,list.Get(),&parameters);
        check(!A::Materialize(*host,*callback,settings,[]{return std::unique_ptr<Renderer>{};}).current,"preflight refusal produces no route");
        callback.reset();auto release=registry.BeginRelease(7,snapshot);
        check(release.has_value(),"historical initialization does not retain a live callback pin");
        if(release)release->Complete(true);
        A::Close(*host);auto shutdown=registry.BeginShutdown();
        check(shutdown.has_value(),"historical metadata does not prevent original provider shutdown");
        if(shutdown)shutdown->Complete();
    }
    {
        Settings settings;DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));auto host=A::Open();
        auto earlier=A::Capture(*host,registry);A::Bind(*host,*earlier,list.Get(),&parameters);
        A::Materialize(*host,*earlier,settings,[]{return std::unique_ptr<Renderer>{};});earlier.reset();
        auto actual=A::Capture(*host,registry);A::Bind(*host,*actual,list.Get(),&parameters);
        Model model;model.throwPrepare=true;
        const auto refused=A::Materialize(*host,*actual,settings,[&]{return std::make_unique<Renderer>(model);});
        check(!refused.current&&model.prepares==1,"possible initialization effect is retained on exception");
        check(A::RetainedSample(*host)==actual->Sample(),"failure storage matches the actual effectful callback after an earlier refusal");
        actual.reset();auto release=registry.BeginRelease(7,registry.Read(7));
        check(release.has_value(),"retained possible initialization effects do not retain a live callback pin");
        if(release)release->Complete(true);
        A::Close(*host);auto shutdown=registry.BeginShutdown();
        check(shutdown.has_value(),"retained possible initialization effects do not prevent original provider shutdown");
        if(shutdown)shutdown->Complete();
    }
    for(bool pre:{false,true})
    {
        Settings settings;settings.DlssNrRunBeforeSr.v=pre;
        DlssNr::NativeFeatureRegistry<unsigned> registry;registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
        auto host=A::Open();auto callback=A::Capture(*host,registry);
        check(callback&&A::Bind(*host,*callback,list.Get(),&parameters),"authentic source capture before initialization");
        if(!callback)continue;auto* owners=A::Owners(*host);auto* allocationOwner=&owners->Resources();
        Model model;model.resources={&model,textures[4].Get(),textures[5].Get(),textures[6].Get(),16,8};
        auto prepare=[&]{check(A::Reserved(*host),"retained failure storage exists before renderer entry");return std::make_unique<Renderer>(model);};
        auto first=A::Materialize(*host,*callback,settings,prepare);
        check(!first.current&&model.prepares==1,"creation recording is not coordinator readiness");
        const auto* observed=callback->ObservedSource();
        check(observed&&observed->RepresentationPreparation()&&observed->RepresentationPreparation()->bundle.has_value(),"real source reaches C02 C04 C12 before model preparation");
        check(!A::Materialize(*host,*callback,settings,prepare).current&&model.prepares==1,"same callback cannot repeat initialization effects");
        auto later=A::Capture(*host,registry);check(later&&A::Bind(*host,*later,list.Get(),&parameters),"subsequent callback uses retained owner set");
        model.ready=true;auto active=A::Materialize(*host,*later,settings,prepare);
        if(!active.current)std::cerr<<"Initial route refusal: "<<later->RuntimePreparationReason().View()<<'\n';
        check(active.status==O::InitStatus::Activated&&active.current.has_value(),"source context profile and model owners activate real coordinator");
        check(A::CurrentPreparation(*later)&&A::CurrentPreparation(*later)->source.get()==later->ObservedSource(),
            "activated callback retains only its own qualified current-frame preparation");
        auto lifetime=A::Lifetime(*host);
        check(lifetime&&A::Owners(*host)==owners&&&A::Owners(*host)->Resources()==allocationOwner,"adoption transfers the identical owner set and resource registry");
        if(!active.current)continue;
        check(active.current->Plan().committed.IsKnown()&&active.current->Plan().routeGeneration.IsKnown(),"canonical route commit contains genuine issued generation");
        check(active.current->Anchor().nativeDelivery.has_value(),"committed delivery meaning is retained");
        auto next=A::Capture(*host,registry);check(next&&A::Bind(*host,*next,list.Get(),&parameters),"post-adoption source publication still uses original owners");
        const auto repeated=A::Materialize(*host,*next,settings,prepare);
        check(repeated.status==O::InitStatus::ExistingReadOnly&&repeated.current&&repeated.current->Plan()==active.current->Plan(),"later preparation retrieves same committed plan without reissue");
        check(A::CurrentPreparation(*next)&&A::CurrentPreparation(*next)!=A::CurrentPreparation(*later)&&
              A::CurrentPreparation(*next)->source.get()==next->ObservedSource(),
            "next callback keeps fresh metadata while reusing the single committed route");
        auto exact=A::CurrentPreparation(*next);
        exact->committedRoute=A::CurrentPreparation(*later);
        P::RecipeRequest executable;
        static_cast<P::NativeProfilePreparationRequest&>(executable)=exact->request;
        executable.recipeHeader=owners->ProtocolJournal().Header(C::ContractId::C06,next->Scope().scope);
        executable.externalPlan=P::Reference<C::ContractId::C05>(repeated.current->Plan());
        executable.nativePlan=exact->Publish(repeated.current->Plan(),C::OwnerDomain::StreamCoordinator);
        const auto built=P::BuildRecipe(executable,*exact);
        if((pre&&!built.reason.Empty())||(!pre&&built.reason.View()!="Native.PostSrOutputPending"))
            std::cerr<<"Executable refusal: "<<built.reason.View()<<" pre="<<pre<<'\n';
        check(pre?built.reason.Empty():built.reason.View()=="Native.PostSrOutputPending",
            "Native Before executable recipe accepts exact invocation; Native After awaits post-SR publication");
        if(pre&&built.product)
        {
            const auto& p=built.product->Value();
            const auto mapped=P::BuildNativeBindingMap(p,*exact);
            if(!mapped)
            {
                const auto* context=S::ResolveMetadata(p.inputContext,*exact);
                const auto* frame=S::ResolveMetadata(p.recipe.frame,*exact);
                std::cerr<<"Map diag: valid="<<S::ValidValues(p)<<" frame="<<(frame!=nullptr)
                         <<" eval="<<(frame&&S::Established(frame->evaluationId))
                         <<" create="<<bool(P::BuildLegacyCommands(p.bindingPlan,P::LegacyStage::Create))
                         <<" evaluate="<<bool(P::BuildLegacyCommands(p.bindingPlan,P::LegacyStage::Evaluate))
                         <<" context="<<(context!=nullptr)<<" native="<<(frame&&frame->nativeSample.has_value())<<'\n';
                for(const auto& use:p.inputRepresentations)
                {
                    const auto* plan=S::ResolveMetadata(use.plan,*exact);
                    const auto* view=S::ResolveMetadata(use.currentSource,*exact);
                    std::cerr<<" use "<<C::EnumName(use.purpose)<<" plan="<<(plan!=nullptr)
                             <<" view="<<(view!=nullptr)<<" pub="<<(context&&frame&&P::CurrentNativePublication(use,*context,*frame,*exact))
                             <<" lineage="<<(plan&&P::LineageMatches(use,*plan,*exact))<<'\n';
                }
            }
            check(mapped.has_value(),"current callback maps native bindings from retained C01 publication");
            const auto initial=P::PrepareNativeProfile(A::CurrentPreparation(*later)->request,*A::CurrentPreparation(*later));
            if(initial.projection)
            {
                const auto& stable=initial.projection->Value().dependencies.structuralPlans;
                const auto& fresh=p.dependencies.structuralPlans;
                bool same=stable.Size()==fresh.Size();
                if(same)for(const auto& key:stable)
                    if(std::count(fresh.begin(),fresh.end(),key)!=1)same=false;
                check(!same,"subsequent callback retains fresh C12 keys rather than reusing the initial publication");
            }
        }
        check(lifetime->Coordinator().Inspect().successfulIssues==1,"single coordinator issues only once");
        auto changed=A::Capture(*host,registry);A::Bind(*host,*changed,list.Get(),&parameters);settings.DlssNrIntensity.v=.5f;
        check(!A::Materialize(*host,*changed,settings,prepare).current,"changed settings cannot silently inherit committed plan");
        A::Close(*host);check(!later->Current()&&!lifetime->Coordinator().TryReadCurrent().current,"source closure revokes actual coordinator");
        host.reset();check(!later->Current()&&later->ObservedSource()->Complete(),"retained callback keeps adopted owners and historical metadata alive");
        check(!lifetime->AdvanceClosure().closed,"unknown provider retirement cannot become closure");
        callback.reset();later.reset();next.reset();changed.reset();
        auto release=registry.BeginRelease(7,registry.Read(7));
        check(release.has_value(),"retained creation and admitted route do not retain live callback pins");
        if(release)release->Complete(true);
        auto shutdown=registry.BeginShutdown();
        check(shutdown.has_value(),"admitted route metadata does not prevent original provider shutdown");
        if(shutdown)shutdown->Complete();
    }
#else
    check(false,"production source to coordinator and renderer attachment exists");
#endif
    std::cout<<"Native runtime integration: "<<count<<" checks, "<<failed<<" failures; WARP metadata and synthetic model owner; GPU/model/provider/game NOT_RUN\n";
    return failed?1:0;
}
catch(const std::exception& error)
{std::cerr<<"FIXTURE_DEPENDENCY_REFUSED: "<<error.what()<<'\n';return 1;}
