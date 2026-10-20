#define NR_GPU_SAFETY_TEST
#include "../../../OptiScaler/dlssnr/NrGpuSafety.cpp"
#define NR_SPECTRE_SOURCE_TESTING
#include <iostream>
#if __has_include(<nr/lifecycle/NativeControlledSceneProducer.h>)
#include <nr/lifecycle/NativeControlledSceneProducer.h>
#include <nr/lifecycle/NativeProcessBootstrap.h>
#include <nr/protocol/NativeEncodeControls.h>
#include <nr/lifecycle/NativeOwnerSet.h>
#include <nr/d3d12/NativeRecordingHooks.h>
#include <dxgi1_4.h>
#define HAS_CONTROLLED_PRODUCER 1
#endif
namespace DlssNr
{
const GpuSafety::LocalRecordingAction& NativeRendererInvocationBorrow::RecordingAction()const
{throw std::runtime_error("unexpected renderer invocation in source fixture");}
class NativeNgxCallCaptureTestAccess
{public:template<class T>static auto Capture(ID3D12GraphicsCommandList* list,T* parameters)
    {return NativeNgxCallCapture::Capture(list,parameters,0);}};
}
struct Parameters
{
    std::array<void*,6> images{};
    unsigned flags=3,creationWidth=127,creationHeight=95,quality=5;
    int Get(const char* key,unsigned* value)
    {const std::string_view k=key;*value=k=="DLSS.Feature.Create.Flags"?flags:k=="PerfQualityValue"?quality:
        (k=="Width"||k=="OutWidth")?creationWidth:k=="DLSS.Render.Subrect.Dimensions.Width"?creationWidth:
        (k=="Height"||k=="OutHeight"||k=="DLSS.Render.Subrect.Dimensions.Height")?creationHeight:0;return 0;}
    int Get(const char* key,float* value){const std::string_view k=key;*value=k.starts_with("Jitter")?0.f:1.f;return 0;}
    int Get(const char* key,void** value){const auto& names=DlssNr::NativeNgxCallCapture::ResourceNames();
        for(unsigned i=0;i<names.size();++i)if(key==std::string_view(names[i])){*value=images[i];return 0;}return 1;}
};
namespace Neurotic::Lifecycle
{
class NativeProcessTestAccess
{
 public:
    static auto Open(){return std::unique_ptr<NativeProcessBootstrap>(new NativeProcessBootstrap);}
    static bool Creation(const DlssNr::NativeNgxCreationParameters& creation)
    {return NativeProcessBootstrap::ControlledCreation(creation,false);}
    static auto Record(NativeProcessBootstrap& host,ID3D12GraphicsCommandList* list,const NativeControlledSceneProducer::Images& images,
        std::uint64_t generation,const NativeControlledSceneProducer::Observe& observe,
        const NativeControlledSceneProducer::RegisterLayout& layout)
    {return host.RecordControlledScene(list,images,generation,0,false,observe,layout);}
    static auto Capture(NativeProcessBootstrap& host,DlssNr::NativeFeatureRegistry<unsigned>& registry)
    {auto pin=registry.Pin(7,registry.Read(7));return host.Capture(registry,std::move(*pin));}
    template<class T>static bool Bind(NativeProcessBootstrap& host,NativeProcessBootstrap::Callback& c,
        ID3D12GraphicsCommandList* list,T* p,const NativeControlledSceneProducer::Observe& observe)
    {return host.BindCall(c,list,p,0,observe);}
    template<class T>static auto Begin(NativeProcessBootstrap& host,NativeProcessBootstrap::Callback& c,T* p,
        const D3D12::NativeRecordingObservation& r){return host.BeginOpaqueSr(c,reinterpret_cast<void*>(7),p,0,r);}
    template<class T,class Op>static void End(NativeProcessBootstrap& host,NativeProcessBootstrap::Callback& c,
        T* p,Op& op,const D3D12::NativeRecordingObservation& r)
    {host.ObserveSrReturn(c,p,0,0);host.SealOpaqueSr(c,op,reinterpret_cast<void*>(7),p,0,0,r);}
    template<class Settings,class Factory>static auto Materialize(NativeProcessBootstrap& h,NativeProcessBootstrap::Callback& c,
        const Settings& s,Factory&& f){return h.Materialize(c,s,std::forward<Factory>(f));}
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
struct StoppingRenderer
{
    unsigned passthrough;bool& reached;
    bool PreflightCurrent()const{return true;}bool NeedsModelInitialization()const{return true;}
    auto Encode()const{return std::optional{Neurotic::Protocol::NativeEncodeControls{passthrough,203,0,0,3,127,95}};}
    bool PrepareModel(Neurotic::Lifecycle::NativeResourceRegistry&,DlssNr::NativePreparationStatus& status)
    {reached=true;status=DlssNr::NativePreparationStatus::CreationRecorded;return false;}
    bool Current()const{return false;}
    const DlssNr::NativeRendererPreparation::Resources* Inspect()const{return nullptr;}
};
int main()try
{
#ifndef HAS_CONTROLLED_PRODUCER
    std::cerr << "FAIL: no owned controlled scene producer\n";
    return 1;
#else
    namespace L=Neurotic::Lifecycle;namespace C=Neurotic::Contracts;namespace P=Neurotic::Protocol;
    using Microsoft::WRL::ComPtr;using Producer=L::NativeControlledSceneProducer;
    unsigned checks=0;const auto check=[&](bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);};
    const auto api=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("WARP prerequisite");};
    static_assert(!std::is_default_constructible_v<Neurotic::Lifecycle::NativeProducerColorBinding>);
#ifndef NR_NATIVE_ENCODE_COLOR_V1
    std::cerr << "FAIL: no declaration/encode consistency gate\n";
    return 2;
#endif
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    api(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    api(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    check(DlssNr::GpuSafety::PrepareRecordingHooks(list.Get()),"GPU recording hooks");
    check(bool(DlssNr::GpuSafety::Record(list.Get())),"GPU owner sees list before Reset");
    auto* observer=new Neurotic::D3D12::NativeRecordingObserver;
    auto* hooks=new Neurotic::D3D12::NativeRecordingHooks(*observer);
    check(hooks->Install(list.Get()),"native hooks");api(list->Close());api(list->Reset(allocator.Get(),nullptr));
    C::Symbol space;space.Assign("ControlledScene.Warp.Test");auto* owners=new L::NativeOwnerSet(space);
    auto& registry=owners->Resources();Producer::Images images{};std::array<ComPtr<ID3D12Resource>,4> held;
    constexpr std::array formats={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32_UINT};
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    for(unsigned i=0;i<4;++i){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=127;desc.Height=95;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;
        desc.Format=formats[i];desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&held[i])));
        images[i]=held[i].Get();}
    auto* retained=new std::vector<std::shared_ptr<Producer::Declaration>>;
    const Producer::Observe observe=[&](auto* value){return observer->Observe(value);};
    {
        const auto ticket=DlssNr::GpuSafety::Record(list.Get());
        check(!DlssNr::GpuSafety::BeginLocalAction(ticket,list.Get(),std::span<ID3D12Resource* const>{}),"empty resource span refuses");
        auto invalid=images;invalid[3]=nullptr;
        check(!DlssNr::GpuSafety::BeginLocalAction(ticket,list.Get(),std::span<ID3D12Resource* const>(invalid)),"null resource invalidates whole borrow");
        auto action=DlssNr::GpuSafety::BeginLocalAction(ticket,list.Get(),std::span<ID3D12Resource* const>(images));
        check(action&&action->Current(),"one exclusive guard covers owned pass");
        for(auto* image:images)check(action->CurrentFor(image),"each exact pass resource retained by guard");
        check(!DlssNr::GpuSafety::BeginLocalAction(ticket,list.Get(),images[0]),"nested single borrow stays refused");
    }
    C::NativeSampleIdentityV1 sample;
    const auto id=[](auto& value){value.nameSpace.Assign("ControlledScene.Warp.Test");value.issuer.Assign("Fixture");value.value=1;};
    id(sample.session);id(sample.stream);id(sample.view);id(sample.feature);id(sample.ingress);id(sample.callback);sample.producerOrdinal=1;
    check(sample.Check()==C::Error::None,"fixture native sample valid");
    Parameters parameters;parameters.images={images[0],images[0],images[1],images[2],nullptr,nullptr};
    {
        using A=L::NativeProcessTestAccess;
        check(A::Creation(DlssNr::NativeNgxCreationParameters::Capture(&parameters,0)),"exact original HDR DLAA creation accepted");
        parameters.flags=0;check(!A::Creation(DlssNr::NativeNgxCreationParameters::Capture(&parameters,0)),"LDR original creation cannot inherit producer SR output meaning");parameters.flags=3;
        parameters.creationWidth=128;check(!A::Creation(DlssNr::NativeNgxCreationParameters::Capture(&parameters,0)),"different original dimensions cannot inherit fixed adapter semantics");parameters.creationWidth=127;
        parameters.quality=0;check(!A::Creation(DlssNr::NativeNgxCreationParameters::Capture(&parameters,0)),"non-DLAA original selected adapter refused");parameters.quality=5;
    }
    const auto layout=[&](auto* root,auto value){return observer->RegisterLayout(root,std::move(value));};
    check(bool(Producer::Prepare(registry,list.Get(),images,9,2,false,layout)),"continued history frame must be admitted");
    check(!Producer::Prepare(registry,list.Get(),images,9,4,false,layout),"unplanned frame refuses");
    {
        std::array<ComPtr<ID3D12Resource>,4> resized;Producer::Images larger{};
        for(unsigned i=0;i<4;++i){auto desc=images[i]->GetDesc();desc.Width=159;desc.Height=111;
            api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&resized[i])));larger[i]=resized[i].Get();}
        auto declaration=Producer::Prepare(registry,list.Get(),larger,10,0,false,layout);
        check(bool(declaration),"actual resized producer resources admitted");retained->push_back(declaration);
        check(declaration->Record(registry,observe),"resized owner shader records real work");
        parameters.creationWidth=159;parameters.creationHeight=111;
        check(L::NativeProcessTestAccess::Creation(DlssNr::NativeNgxCreationParameters::Capture(&parameters,0)),"resized selected DLAA creation accepted");
        parameters.images={larger[0],larger[0],larger[1],larger[2],nullptr,nullptr};
        auto call=DlssNr::NativeNgxCallCaptureTestAccess::Capture(list.Get(),&parameters);
        std::array<const C::ResourceView*,6> views={registry.Observe(larger[0]),registry.Observe(larger[0]),registry.Observe(larger[1]),registry.Observe(larger[2]),nullptr,nullptr};
        Producer::Scope span(declaration,reinterpret_cast<void*>(7),&parameters);
        check(bool(Producer::Consume(*call,sample,10,&parameters,observe(list.Get()),views)),"resized declaration binds exact call dimensions");
        parameters.creationWidth=127;parameters.creationHeight=95;
        parameters.images={images[0],images[0],images[1],images[2],nullptr,nullptr};
        api(list->Close());api(list->Reset(allocator.Get(),nullptr));
        check(declaration->Terminal(),"resized declaration retained until actual cancellation");
    }
    std::shared_ptr<const L::NativeProducerColorBinding> bound;
    for(unsigned scenario=0;scenario<5;++scenario)
    {
        auto declaration=Producer::Prepare(registry,list.Get(),images,9,scenario==4?2:(scenario?1:0),false,
            [&](auto* root,auto layout){return observer->RegisterLayout(root,std::move(layout));});
        check(bool(declaration),"fixed producer setup");retained->push_back(declaration);
        check(!declaration->Terminal(),"unrecorded declaration cannot retire");
        const bool recorded=declaration->Record(registry,[&](auto* value){
            check(!declaration->Terminal(),"in-flight record cannot expose terminal ticket");return observe(value);});
        if(!recorded){const auto r=observe(list.Get());const auto d=hooks->Diagnostic(list.Get());std::cerr<<"record stage="<<declaration->RecordStage()<<" active="<<r.active<<" coverage="<<r.completeCoverage<<" missing="<<r.missingRequiredHistory<<" ordinal="<<r.workOrdinal<<" refusal="<<static_cast<unsigned>(d.firstRefusal)<<'\n';}
        check(recorded,"actual producer recorded");
        check(!declaration->Record(registry,observe),"recorded declaration cannot replay");
        check(!declaration->Terminal(),"unsubmitted producer is not retired");
        std::array<const C::ResourceView*,6> views={registry.Observe(images[0]),registry.Observe(images[0]),registry.Observe(images[1]),registry.Observe(images[2]),nullptr,nullptr};
        auto call=DlssNr::NativeNgxCallCaptureTestAccess::Capture(list.Get(),&parameters);
        check(bool(call),"actual resource and parameter capture");
        Producer::Scope scope(declaration,reinterpret_cast<void*>(7),&parameters);
        if(scenario==3)list->Dispatch(16,12,1); // real intervening write invalidates the producer's exact handoff ordinal
        auto binding=Producer::Consume(*call,sample,scenario==1?10:9,scenario==2?nullptr:&parameters,observe(list.Get()),views);
        check(bool(binding)==(scenario==0||scenario==4),"generation/parameters/intervening work bound");
        check(!Producer::Consume(*call,sample,9,&parameters,observe(list.Get()),views),"one callback consumes declaration once");
        if(binding)
        {
            bound=binding;const auto evidence=owners->SourceJournal().Event().evidence;
            const auto color=binding->ColorFor(sample,*call,*views[0],false,evidence);
            check(color&&color->domain.KnownPart()->value==C::ColorDomain::SceneLinear,"actual producer declares linear input");
            check(!binding->ColorFor(sample,*call,*views[1],true,evidence),"producer does not declare uncommitted SR output");
            auto changed=sample;++changed.producerOrdinal;
            check(!binding->ColorFor(changed,*call,*views[0],false,evidence),"different callback cannot reuse producer color");
            auto encode=P::NativeEncodeControls{0,203,0,0,3,127,95};
            check(P::NativeEncodeMatchesColor(encode,*color),"linear producer matches HDR encode");
            encode.passthrough=1;check(!P::NativeEncodeMatchesColor(encode,*color),"linear producer refuses display passthrough");
            C::ColorDescription unknown;check(!P::NativeEncodeMatchesColor(encode,unknown),"unknown color remains refused");
            auto display=*color;display.domain=C::OptionalFact<C::ColorDomain>::FromKnown(C::ColorDomain::EncodedDisplay,evidence);display.transfer={};
            check(P::NativeEncodeMatchesColor(encode,display),"LDR declaration matches passthrough");
            encode.passthrough=0;check(!P::NativeEncodeMatchesColor(encode,display),"legacy mismatched LDR fixture refuses");
        }
    }
    // Cancel this local recording by the actual unsubmitted Close/Reset path.
    // No submitted completion is inferred and no provider is invoked.
    api(list->Close());api(list->Reset(allocator.Get(),nullptr));
    check((*retained)[0]->Terminal(),"actual unsubmitted reset retires recorded producer closure");
    // Connected synthetic selected SR: actual fixed producer, original callback
    // capture, Resource SR receipt and fresh Context/C12 all execute unchanged.
    // Model/provider work stops at the synthetic PrepareModel boundary.
    for(unsigned passthrough=0;passthrough<2;++passthrough)
    {
        using A=L::NativeProcessTestAccess;
        ComPtr<ID3D12Resource> output;auto desc=images[0]->GetDesc();
        api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&output)));
        parameters.images[1]=output.Get();
        DlssNr::NativeFeatureRegistry<unsigned> features;features.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
        auto host=A::Open();
        auto declaration=A::Record(*host,list.Get(),images,features.Read(7).generation,observe,
            [&](auto* root,auto layout){return observer->RegisterLayout(root,std::move(layout));});
        check(bool(declaration),"bootstrap retains real producer before work");
        Producer::Scope span(declaration,reinterpret_cast<void*>(7),&parameters);
        auto callback=A::Capture(*host,features);
        check(callback&&A::Bind(*host,*callback,list.Get(),&parameters,observe),"actual callback consumes owned producer span");
        const auto inputColor=callback->ObservedSource()->DeclaredColor(false);
        const auto initialOutput=callback->ObservedSource()->DeclaredColor(true);
        check(inputColor&&Neurotic::Context::Established(inputColor->domain)&&
            (!initialOutput||!Neurotic::Context::Established(initialOutput->domain)),"role publication keeps pre-SR Output unknown");
        auto operation=A::Begin(*host,*callback,&parameters,observe(list.Get()));
        check(operation&&operation->receipt&&operation->receipt->MarkEntered(),"synthetic selected SR enters genuine receipt");
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={images[0],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1,&b);list->CopyResource(output.Get(),images[0]);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
        A::End(*host,*callback,&parameters,*operation,observe(list.Get()));
        check(bool(callback->RequiredSrInput()),"exact selected SR publishes real output revision");
        const auto outputColor=callback->ObservedSource()->DeclaredColor(true);
        check(outputColor&&Neurotic::Context::Established(outputColor->domain)&&
            outputColor->domain.KnownPart()->value==C::ColorDomain::SceneLinear,"only committed selected SR derives linear output");
        bool reached=false;Settings settings;
        A::Materialize(*host,*callback,settings,[&]{return std::make_unique<StoppingRenderer>(StoppingRenderer{passthrough,reached});});
        const auto* context=callback->ObservedSource()->ContextPreparation();
        check(context&&context->certificate&&context->certificate->eligibility==C::Eligibility::Eligible,"actual C02/C04 HDR qualification succeeds");
        const auto* representation=callback->ObservedSource()->RepresentationPreparation();
        check(representation&&bool(representation->bundle)==(passthrough==0)&&reached==(passthrough==0),"C12 permits matching HDR encode and blocks mismatched passthrough before model");
        if(passthrough)check(representation->reason.View()=="CTX.Native.EncodeColorMismatch","mismatched encode gives precise refusal");
        callback.reset();operation.reset();api(list->Close());api(list->Reset(allocator.Get(),nullptr));
        check(declaration->Terminal(),"bootstrap retained closure reaches observed unsubmitted terminal");
    }
    std::cout << "Controlled scene producer: "<<checks<<" checks, 0 failures; WARP only\n";
    return 0;
#endif
}
catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
