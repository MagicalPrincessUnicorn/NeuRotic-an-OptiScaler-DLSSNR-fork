#define NR_SPECTRE_SOURCE_TESTING
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <iostream>
#include <array>
#include <stdexcept>
#include <string_view>
#include <limits>
#include <climits>
#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#if __has_include(<dlssnr/StreamlineSourceScope.h>)
#include <dlssnr/StreamlineSourceScope.h>
#define NR_STREAMLINE_SCOPE_AVAILABLE
namespace DlssNr
{
class StreamlineSourceScopeTestAccess
{
  public:
    static auto Open(bool selected,std::uint32_t frame,std::optional<std::uint32_t> viewport,ID3D12GraphicsCommandList* list)
    {return std::unique_ptr<StreamlineSourceScope>(new StreamlineSourceScope(selected,frame,viewport,list));}
    static auto Barrier(){return std::unique_ptr<StreamlineSourceScope>(new StreamlineSourceScope);}
    static void Configure(StreamlineSourceScope& scope,bool selected,std::uint32_t frame,
        std::optional<std::uint32_t> viewport,ID3D12GraphicsCommandList* list){scope.Configure(selected,frame,viewport,list);}
    static auto Capture(ID3D12GraphicsCommandList* list){return StreamlineSourceScope::CaptureFor(list);}
    static void Complete(StreamlineSourceScope& scope,std::uint32_t result,bool success){scope.Complete(result,success);}
    template<class Capture>static auto Source(const Capture& value)
    {
        if constexpr(requires{value.SourceFrame();})return value.SourceFrame();
        else return std::shared_ptr<const StreamlineSourceScope::Observation>{};
    }
};
}
#endif
#if __has_include(<dlssnr/NativeNgxCallCapture.h>)
#include <dlssnr/NativeNgxCallCapture.h>
#include <nr/lifecycle/NativeProcessBootstrap.h>
#define NR_CAPTURE_AVAILABLE
namespace DlssNr
{
class NativeNgxCallCaptureTestAccess
{
  public:
    template<class Parameters>static auto Capture(ID3D12GraphicsCommandList* list,Parameters* parameters)
    {return NativeNgxCallCapture::Capture(list,parameters,0);}
    static void ForgetFormatSupport(NativeNgxCallCapture& capture)
    {for(auto& resource:capture.resources_)resource.formatSupport.reset();} // test-only failed-query injection
};
// Test-only wrapped interface. Actual WARP backing; no model/provider call.
class CaptureProxy final:public IUnknown
{
    std::atomic<ULONG> refs_{1};Microsoft::WRL::ComPtr<IUnknown> base_;bool* destroyed_;
    ~CaptureProxy(){*destroyed_=true;}
  public:
    CaptureProxy(IUnknown* base,bool& destroyed):base_(base),destroyed_(&destroyed){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override
    {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)){*out=this;AddRef();return S_OK;}
        if(id==NativeIdentity::streamlineBase)return base_->QueryInterface(__uuidof(IUnknown),out);
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto count=--refs_;if(!count)delete this;return count;}
};
}
namespace Neurotic::Lifecycle
{
class NativeProcessTestAccess
{
  public:
    static auto Open(){return std::unique_ptr<NativeProcessBootstrap>(new NativeProcessBootstrap);}
    static auto Capture(NativeProcessBootstrap& host,DlssNr::NativeFeatureRegistry<unsigned>& registry)
    {auto pin=registry.Pin(7,registry.Read(7));if(!pin)return std::optional<NativeProcessBootstrap::Callback>{};return host.Capture(registry,std::move(*pin));}
    template<class Parameters>static bool Bind(NativeProcessBootstrap& host,NativeProcessBootstrap::Callback& callback,
        ID3D12GraphicsCommandList* list,Parameters* parameters){return host.BindCall(callback,list,parameters,0);}
    template<class Parameters>static bool ObserveSrReturn(NativeProcessBootstrap& host,
        NativeProcessBootstrap::Callback& callback,Parameters* parameters,int result)
    {return host.ObserveSrReturn(callback,parameters,result,0);}
    static void Close(NativeProcessBootstrap& host){host.CloseSource();}
    static auto RemoveSourceJournal(NativeProcessBootstrap& host){return std::move(host.state_->owners->SourceJournal());}
    static auto PolicyPublication(NativeProcessBootstrap& host){return host.state_->owners->ContextJournal().Event().evidence.record;}
    template<class Bootstrap,class Policy>static auto Qualify(Bootstrap& host,NativeProcessBootstrap::Callback& callback,const Policy& policy)
    {
        if constexpr(requires{host.PrepareContext(callback,policy);})return host.PrepareContext(callback,policy);
        else return nullptr;
    }
    template<class Bootstrap,class Controls>static auto Represent(Bootstrap& host,NativeProcessBootstrap::Callback& callback,
        C::Placement placement,const Controls& controls)
    {
        if constexpr(requires{host.PrepareRepresentations(callback,placement,controls);})return host.PrepareRepresentations(callback,placement,controls);
        else return nullptr;
    }
};
}
#endif
using Microsoft::WRL::ComPtr;
static void Api(HRESULT hr){if(FAILED(hr))throw std::runtime_error("controlled D3D12 prerequisite failed");}
struct Parameters
{
    std::array<void*,6> resources{};unsigned width=16,outputX=0,outputY=0;float scale=-2.5f;bool missingWidth=false,missingJitter=false,missingExposure=false,missingOutputOrigin=false,throws=false;
    unsigned reads=0;bool prepared=false;
    std::function<void()> onRead;
    int Get(const char* key,unsigned* value){++reads;if(onRead){auto action=std::move(onRead);action();}if(throws)throw std::runtime_error("parameter owner failure");
        const std::string_view name(key);if(name.starts_with("DFC.Feeder.")||name.starts_with("NR.Prepared.")){if(!prepared)return 1;*value=1;return 0;}
        if(missingWidth&&name=="Width")return 1;
        if(name=="DLSS.Output.Subrect.Base.X"||name=="DLSS.Output.Subrect.Base.Y")
        {if(missingOutputOrigin)return 1;*value=name.ends_with(".X")?outputX:outputY;return 0;}
        *value=name=="OutHeight"||name=="Height"?8:width;return 0;}
    int Get(const char* key,float* value){++reads;if(missingJitter&&std::string_view(key)=="Jitter.Offset.Y")return 1;*value=scale;return 0;}
    int Get(const char* key,void** value){++reads;if(throws)throw std::runtime_error("parameter owner failure");
        constexpr std::array names={"Color","Output","Depth","MotionVectors","ExposureTexture","DLSS.Input.Bias.Current.Color.Mask"};
        for(std::size_t i=0;i<names.size();++i)if(key==std::string_view(names[i])){if(missingExposure&&i==4)return 1;*value=resources[i];return 0;}return 1;}
};
template<class Check>static void CheckStreamlineSource(ID3D12Device* device,ID3D12GraphicsCommandList* list,
    const Parameters& base,Check& check)
{
#ifdef NR_STREAMLINE_SCOPE_AVAILABLE
    using Scope=DlssNr::StreamlineSourceScope;using Access=DlssNr::StreamlineSourceScopeTestAccess;
    using State=Scope::ReturnStatus;using Ngx=DlssNr::NativeNgxCallCaptureTestAccess;
    check(!Access::Capture(list),"no SL scope means no source transport observation");
    ComPtr<ID3D12CommandAllocator> allocator,computeAllocator;ComPtr<ID3D12GraphicsCommandList> other,compute;
    Api(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Api(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&other)));
    Api(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_PPV_ARGS(&computeAllocator)));
    Api(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,computeAllocator.Get(),nullptr,IID_PPV_ARGS(&compute)));
    auto outer=Access::Open(true,0,0,list);auto observed=Access::Capture(list);
    check(observed&&observed->Frame()==0&&observed->Viewport()==0&&observed->Status()==State::Pending&&!observed->Result(),
        "zero frame and viewport are actual values with pending return");
    check(observed==Access::Capture(list)&&!Access::Capture(other.Get())&&!Access::Capture(nullptr),
        "one call shares its observation only with the exact native list");
    auto params=base;auto first=Ngx::Capture(list,&params);auto second=Ngx::Capture(list,&params);
    check(first&&second&&Access::Source(*first)==observed&&Access::Source(*second)==observed,
        "actual nested NGX captures share the enclosing SL call observation");
    {
        auto barrier=Access::Barrier();check(!Access::Capture(list),"scope entry blocks inherited association before argument processing");
        Access::Configure(*barrier,false,4,1,list);Access::Configure(*barrier,true,4,1,list);
        check(!Access::Capture(list),"unselected configuration cannot retry into a selected association");
    }
    for(unsigned invalid=0;invalid<4;++invalid)
    {
        auto blocked=Access::Open(invalid!=0,1,invalid==1?std::nullopt:std::optional<std::uint32_t>{0},
            invalid==2?nullptr:invalid==3?compute.Get():list);
        check(!Access::Capture(list),"unsupported nested scope never inherits outer source");
    }
    check(Access::Capture(list)==observed,"invalid nested scopes restore exact outer association");
    std::shared_ptr<const Scope::Observation> innerObserved;
    {
        auto inner=Access::Open(true,UINT32_MAX,UINT32_MAX,other.Get());innerObserved=Access::Capture(other.Get());
        check(innerObserved&&innerObserved->Frame()==UINT32_MAX&&innerObserved->Viewport()==UINT32_MAX&&
            !Access::Capture(list),"nested selected scope keeps full unsigned tuple and its own list");
        Access::Complete(*inner,UINT32_MAX,false);Access::Complete(*inner,0,true);
        check(innerObserved->Status()==State::Failed&&innerObserved->Result()==UINT32_MAX&&!Access::Capture(other.Get()),
            "failed return remains exact and cannot be revised or captured again");
    }
    check(innerObserved->Status()==State::Failed&&Access::Capture(list)==observed,"failed inner return survives closure and restores outer scope");
    bool isolated=false,threadValid=false;
    std::thread worker([&]{isolated=!Access::Capture(list);auto local=Access::Open(true,9,2,list);
        auto value=Access::Capture(list);threadValid=value&&value->Frame()==9&&value!=observed;});worker.join();
    check(isolated&&threadValid&&Access::Capture(list)==observed,"scope state and restoration are thread local");
    std::vector<std::unique_ptr<Scope>> nesting;
    for(std::size_t i=1;i<Scope::MaximumDepth;++i)nesting.push_back(Access::Open(true,10,0,list));
    auto deepest=Access::Capture(list);check(deepest!=nullptr,"maximum supported nesting retains selected source");
    {auto excess=Access::Open(true,11,0,list);check(!Access::Capture(list),"excess nesting installs refusal barrier");
     auto excessChild=Access::Open(true,12,0,list);check(!Access::Capture(list),"nested excess cannot escape depth refusal");}
    check(Access::Capture(list)==deepest,"depth refusal restores exact supported parent");
    while(!nesting.empty())nesting.pop_back();
    check(Access::Capture(list)==observed,"unwinding bounded nesting restores initial scope");
    std::shared_ptr<const Scope::Observation> abandoned;
    {auto scope=Access::Open(true,3,0,list);abandoned=Access::Capture(list);}
    check(abandoned&&abandoned->Status()==State::Abandoned&&!abandoned->Result(),"missing actual return stays abandoned without manufactured result");
    // A foreign parameter getter cannot change the association captured at NGX entry.
    std::unique_ptr<DlssNr::NativeNgxCallCapture> reentrant;
    params.onRead=[&]{auto blocked=Access::Open(false,0,0,list);auto nestedParams=base;reentrant=Ngx::Capture(list,&nestedParams);};
    auto preRead=Ngx::Capture(list,&params);
    check(preRead&&Access::Source(*preRead)==observed&&reentrant&&!Access::Source(*reentrant),
        "NGX entry captures before foreign Gets and unsupported reentry inherits nothing");
    Access::Complete(*outer,0x12345678u,true);check(!Access::Capture(list),"completed scope offers no new association");outer.reset();
    check(observed->Status()==State::Succeeded&&observed->Result()==0x12345678u&&!Access::Capture(list),
        "retained observation receives exact successful API result after closure");
    auto repeated=Access::Open(true,0,0,list);auto repeatedObservation=Access::Capture(list);
    check(repeatedObservation&&repeatedObservation!=observed&&repeatedObservation->Frame()==observed->Frame()&&
        repeatedObservation->Viewport()==observed->Viewport()&&repeatedObservation->Status()==State::Pending,
        "repeated same tuple is a separate call and makes no canonical identity claim");
    repeated.reset();
    // Exercise the actual Bootstrap -> source publication join separately from
    // direct capture: all three return outcomes remain tied to the same sample.
    namespace L=Neurotic::Lifecycle;namespace X=Neurotic::Context;
    using Process=L::NativeProcessTestAccess;
    for(unsigned outcome=0;outcome<4;++outcome)
    {
        auto publishedScope=outcome<3?Access::Open(true,27,6,list):std::unique_ptr<Scope>{};
        const auto callObservation=Access::Capture(list);
        auto host=Process::Open();DlssNr::NativeFeatureRegistry<unsigned> registry;auto sourceParams=base;
        const auto creation=DlssNr::NativeNgxCreationParameters::Capture(&sourceParams,0);registry.Set(7,11,creation);
        auto callback=Process::Capture(*host,registry);
        check(callback&&Process::Bind(*host,*callback,list,&sourceParams),"source association fixture binds actual Bootstrap callback");
        if(!callback||!callback->ObservedSource()||!callback->ObservedSource()->Complete())return;
        const auto* publication=callback->ObservedSource();const auto retained=publication->SourceFrame();
        const auto* frame=X::ResolveMetadata(publication->Source()->seed.frame,*publication);
        const auto* sample=frame?X::ResolveNativeSample(*frame,*publication):nullptr;
        check(sample&&*sample==callback->Sample()&&publication->Source()->callback==callback->Sample().callback,
            "source transport observation stays attached to the exact native callback sample");
        check(frame&&!frame->baseRealFrameId.IsKnown()&&!frame->renderId.IsKnown()&&
            X::Established(frame->episodeId)&&callback->TransactionObservation()&&
            frame->episodeId.KnownPart()->value==callback->TransactionObservation()->Subject().episode&&
            !frame->evaluationId.IsKnown()&&!frame->generatedFrameId.IsKnown()&&frame->associationEvidence.Size()==0,
            "transport publication retains issued episode without canonical frame or association authority");
        if(outcome==3)
        {
            check(!retained&&!callback->OriginalCall()->SourceFrame(),"source publication without enclosing SL call preserves absent association");
            continue;
        }
        check(retained&&retained==callObservation&&callback->OriginalCall()->SourceFrame()==retained&&
            retained->Frame()==27&&retained->Viewport()==6&&retained->Status()==State::Pending,
            "Bootstrap publication retains the same pending enclosing SL observation");
        if(!retained)return;
        if(outcome==0)Access::Complete(*publishedScope,0,true);
        if(outcome==1)Access::Complete(*publishedScope,0xffffffffu,false);
        publishedScope.reset();
        const auto expectedState=outcome==0?State::Succeeded:outcome==1?State::Failed:State::Abandoned;
        const auto expectedResult=outcome==0?std::optional<std::uint32_t>{0}:
            outcome==1?std::optional<std::uint32_t>{0xffffffffu}:std::nullopt;
        check(publication->SourceFrame()==retained&&retained->Status()==expectedState&&retained->Result()==expectedResult,
            "already-published source observes exact success failure or abandonment after enclosing call closes");
        check(sample&&*sample==callback->Sample()&&frame&&!frame->baseRealFrameId.IsKnown()&&
            !frame->renderId.IsKnown()&&X::Established(frame->episodeId)&&callback->TransactionObservation()&&
            frame->episodeId.KnownPart()->value==callback->TransactionObservation()->Subject().episode,
            "SL completion preserves sample and issued episode without canonical frame facts");
    }
    Api(other->Close());Api(compute->Close());
#else
    (void)device;(void)list;(void)base;
    check(false,"bounded Streamline source call observation exists");
#endif
}
template<class Callback,class Check>static void CheckPublication(const Callback& callback,Check& check)
{
    if constexpr(requires{callback.ObservedSource();})
    {
        namespace C=Neurotic::Contracts;namespace X=Neurotic::Context;
        const auto* publication=callback.ObservedSource();
        check(publication&&publication->Complete(),"actual callback owns complete source publication");
        if(!publication||!publication->Complete())return;
        const auto* source=publication->Source();const auto* set=publication->Observations();
        check(source&&set&&set->candidates.count==28&&publication->Candidates().size()==28&&publication->Receipts().size()==28,
              "exact NGX observation set and owner receipts retained");
        if(!source)return;
        const auto* frame=X::ResolveMetadata(source->seed.frame,*publication);
        const auto* sample=frame?X::ResolveNativeSample(*frame,*publication):nullptr;
        check(sample&&*sample==callback.Sample()&&source->callback==sample->callback&&source->enrolledScope==callback.Scope().scope,
              "observations bind actual callback sample and enrolled scope");
        check(frame&&!frame->baseRealFrameId.IsKnown()&&!frame->renderId.IsKnown()&&!frame->finalSealId.IsKnown()&&
              !frame->evaluationId.IsKnown()&&X::Established(frame->episodeId)&&callback.TransactionObservation()&&
              frame->episodeId.KnownPart()->value==callback.TransactionObservation()->Subject().episode,
              "source publication retains issued episode without downstream frame identities");
        check(X::Established(source->seed.providerIncarnation)&&!source->seed.sourceVersion.IsKnown(),
              "genuine provider incarnation does not invent API source version");
        check(source->seed.boundary.kind.IsKnown()&&source->seed.boundary.kind.KnownPart()->value==C::BoundaryKind::BeforeUpscale&&
              !source->seed.boundary.afterRequiredHostRendering.IsKnown(),"original source boundary never claims final host rendering");
        bool views=true,fresh=true,rawWidth=false,unknownJitter=false,unqualified=true,resourceFacts=true;
        std::size_t resourceCount=0;
        for(const auto& candidate:publication->Candidates())
        {
            const auto* evidence=X::ResolveMetadata(candidate.evidence,*publication);
            fresh=fresh&&evidence&&frame&&X::NativeEvidenceFresh(*evidence,*frame,*publication);
            unqualified=unqualified&&!candidate.color.IsKnown()&&!candidate.motion.IsKnown()&&!candidate.depth.IsKnown();
            const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
            if(!X::ResolveList(candidate.claims,*publication,claims)||!claims||!claims->Size()){views=false;continue;}
            const auto& claim=*claims->Get(0);const auto field=claim.field.View();
            if(field=="Width")rawWidth=claim.raw.IsKnown()&&claim.raw.KnownPart()->value==C::ScalarValue{std::uint64_t{99}}&&!claim.effective.IsKnown();
            if(field=="Jitter.Offset.Y")unknownJitter=!claim.raw.IsKnown()&&!claim.effective.IsKnown();
            if(candidate.payload.IsKnown())
            {
                const auto* reference=std::get_if<C::MetadataRef<C::ResourceView>>(&candidate.payload.KnownPart()->value);
                if(!reference)continue;
                ++resourceCount;const auto* view=X::ResolveMetadata(*reference,*publication);
                const auto* registered=callback.RegisteredResource(field);
                views=views&&view&&registered&&*view==*registered&&reference->owner==C::OwnerDomain::Resource;
                resourceFacts=resourceFacts&&!candidate.contentRevision.IsKnown()&&view&&!view->identity.contentRevision.IsKnown();
            }
        }
        check(views&&resourceCount==4,"source resource views are exact retained resource-owner publications");
        check(fresh,"every observation uses Native freshness without real-frame age");
        check(rawWidth&&unknownJitter,"original raw control and unknown component are preserved");
        check(unqualified&&resourceFacts,"raw source transport grants neither semantic qualification nor current content");
    }
    else check(false,"actual callback owns complete source publication");
}
template<class Callback>static bool SameProvider(const Callback& first,const Callback& second)
{
    if constexpr(requires{first.ObservedSource();})
    {
        const auto* a=first.ObservedSource();const auto* b=second.ObservedSource();
        return a&&b&&a->Complete()&&b->Complete()&&a->Source()->seed.providerIncarnation==b->Source()->seed.providerIncarnation&&
            a->Source()->seed.frame!=b->Source()->seed.frame&&first.Sample()!=second.Sample();
    }
    else return false;
}
int main()
{
    int count=0,failed=0;
    const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
#ifdef NR_CAPTURE_AVAILABLE
    using Access=DlssNr::NativeNgxCallCaptureTestAccess;
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Api(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Api(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    std::array<ComPtr<ID3D12Resource>,4> textures;Parameters params;
    for(std::size_t i=0;i<textures.size();++i)
    {
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=16;desc.Height=8;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&textures[i])));
        params.resources[i]=textures[i].Get();
    }
    CheckStreamlineSource(device.Get(),list.Get(),params,check);
    auto captured=Access::Capture(list.Get(),&params);
    check(captured&&params.reads==34,"one bounded original parameter read including output origin and feeder declaration probes");
    check(captured&&!captured->DeclaresPreparedSource(),"missing feeder declaration preserves native source eligibility");
    params.prepared=true;
    auto preparedCapture=DlssNr::NativeNgxCallCaptureTestAccess::Capture(list.Get(),&params);
    check(preparedCapture&&preparedCapture->DeclaresPreparedSource(),"declared feeder is retained as prepared source");
    preparedCapture.reset();params.prepared=false;
    if(!captured)return 1;
    unsigned width=0;float scale=0;void* output=nullptr;
    params.width=99;params.scale=7;auto originalOutput=params.resources[1];params.resources[1]=nullptr;
    check(captured->Get("Width",&width)==0&&width==16,"original dimensions immutable after caller edit");
    check(captured->Get("MV.Scale.Y",&scale)==0&&scale==-2.5f,"original motion scale preserved");
    check(captured->Get("Output",&output)==0&&output==originalOutput,"original caller target preserved");
    check(captured->Get("ExposureTexture",&output)==0&&output==nullptr,"successful null remains explicit null");
    check(captured->Get("Unknown",&width)!=0,"unknown key is not invented");
    check(captured->Resource("Output")==textures[1].Get()&&captured->Description("Output")->Width==16,
          "normalized resource and actual descriptor retained");
    const auto region=captured->CallerOutputRegion(16,8);
    check(region&&region->resource==textures[1].Get()&&region->x==0&&region->y==0&&
          region->width==16&&region->height==8&&region->format==DXGI_FORMAT_R16G16B16A16_FLOAT,
          "exact original caller target and full active region retained without a write claim");
    params.resources[1]=originalOutput;
    params.outputX=1;auto shifted=Access::Capture(list.Get(),&params);
    check(shifted&&!shifted->CallerOutputRegion(16,8),"caller region exceeding allocation refuses");
    params.outputX=UINT_MAX;auto overflow=Access::Capture(list.Get(),&params);
    check(overflow&&!overflow->CallerOutputRegion(16,8),"caller region arithmetic cannot wrap");
    params.outputX=0;params.missingOutputOrigin=true;auto missingOrigin=Access::Capture(list.Get(),&params);
    check(missingOrigin&&!missingOrigin->CallerOutputRegion(16,8),"unknown output origin cannot become a full-write claim");
    params.missingOutputOrigin=false;
    check(captured->CommandList()==list.Get()&&captured->Device()==device.Get(),"exact actual command list and device retained");
    const auto support=[]<class Capture>(const Capture& value,std::string_view key)->const D3D12_FEATURE_DATA_FORMAT_SUPPORT*{
        if constexpr(requires{value.FormatSupport(key);})return value.FormatSupport(key);else return nullptr;};
    D3D12_FEATURE_DATA_FORMAT_SUPPORT expected{DXGI_FORMAT_R16G16B16A16_FLOAT};
    Api(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&expected,sizeof(expected)));
    const auto* actual=support(*captured,"Color");
    check(actual&&actual->Format==expected.Format&&actual->Support1==expected.Support1&&actual->Support2==expected.Support2,
          "original capture retains actual immutable device format capabilities");
    check(!support(*captured,"ExposureTexture")&&!support(*captured,"Unknown"),"absent resource grants no format capabilities");
    params.missingWidth=true;params.missingJitter=true;
    auto missing=Access::Capture(list.Get(),&params);
    check(missing&&missing->Get("Width",&width)!=0&&missing->Get("Jitter.Offset.Y",&scale)!=0,
          "failed reads remain missing without defaults");
    params.missingWidth=false;params.missingJitter=false;params.scale=std::numeric_limits<float>::infinity();
    auto nonfinite=Access::Capture(list.Get(),&params);
    check(nonfinite&&nonfinite->Get("MV.Scale.X",&scale)!=0,"nonfinite control remains missing");
    check(nonfinite&&nonfinite->GetOriginalFloat("MV.Scale.X",&scale)==0&&std::isinf(scale),"original nonfinite evidence remains distinguishable from absence");
    params.resources[0]=nullptr;check(!Access::Capture(list.Get(),&params),"missing mandatory source refuses capture");
    params.resources[0]=textures[0].Get();params.throws=true;
    check(!Access::Capture(list.Get(),&params),"parameter exception refuses capture");
    check(!Access::Capture(nullptr,&params),"missing actual command list refuses capture");
    params.throws=false;params.missingExposure=true;auto absentExposure=Access::Capture(list.Get(),&params);
    check(absentExposure&&absentExposure->Get("ExposureTexture",&output)!=0,"failed optional resource read stays absent");
    params.missingExposure=false;
    bool proxyDestroyed=false;ComPtr<IUnknown> proxy;
    proxy.Attach(new DlssNr::CaptureProxy(textures[0].Get(),proxyDestroyed));params.resources[0]=proxy.Get();
    auto wrapped=Access::Capture(list.Get(),&params);void* rawProxy=proxy.Get();proxy.Reset();
    check(wrapped&&wrapped->Get("Color",&output)==0&&output==rawProxy&&wrapped->Resource("Color")==textures[0].Get(),
          "original wrapped interface and normalized resource remain distinct");
    check(!proxyDestroyed,"capture alone retains original wrapper interface");
    wrapped.reset();check(proxyDestroyed,"capture destruction releases original wrapper reference");
    params.resources[0]=textures[0].Get();
    {
    namespace L=Neurotic::Lifecycle;using Process=L::NativeProcessTestAccess;
    auto host=Process::Open(),foreign=Process::Open();DlssNr::NativeFeatureRegistry<unsigned> registry;
    auto creationParams=params;creationParams.width=16;
    const auto creation=DlssNr::NativeNgxCreationParameters::Capture(&creationParams,0);registry.Set(7,11,creation);
    params.missingJitter=true;
    auto callback=Process::Capture(*host,registry);
    check(callback&&Process::Bind(*host,*callback,list.Get(),&params),"authentic root attaches original call");
    if(!callback)return 1;
    const auto callerTarget=callback->CallerOutputRegion();
    check(callerTarget&&callerTarget->resource==textures[1].Get()&&callerTarget->width==16&&callerTarget->height==8,
          "actual callback binds target region to immutable creation and source sample");
    const auto ownerTarget=callback->CallerOutputTarget();
    check(ownerTarget&&ownerTarget->region.resource==textures[1].Get()&&
          ownerTarget->view.identity.resourceGeneration.IsKnown()&&
          callback->RegisteredResource("Output")&&
          ownerTarget->view.identity.resourceGeneration==callback->RegisteredResource("Output")->identity.resourceGeneration&&
          ownerTarget->view.raster.active.IsKnown()&&
          ownerTarget->view.raster.active.KnownPart()->value==Neurotic::Contracts::Rectangle{0,0,16,8}&&
          !ownerTarget->view.identity.contentRevision.IsKnown(),
          "caller target expectation retains exact Resource-owner generation and active rectangle without write revision");
    check(Process::ObserveSrReturn(*host,*callback,&params,0),"authentic SR return is recorded once at its callback boundary");
    const auto srReturn=callback->RequiredSrReturn();
    check(srReturn&&srReturn->result==0&&srReturn->succeeded&&srReturn->callerBindingMatches,
          "SR result and original caller binding are separate observed facts");
    check(!Process::ObserveSrReturn(*host,*callback,&params,0),"duplicate SR return cannot revise the first result");
    CheckPublication(*callback,check);
    const auto originalCall=callback->OriginalCall();
    const auto Registered=[]<class Callback>(const Callback& value,const char* key){
        if constexpr(requires{value.RegisteredResource(key);})return value.RegisteredResource(key);
        else return static_cast<const Neurotic::Contracts::ResourceView*>(nullptr);
    };
    const auto* registered=Registered(*callback,"Color");
    check(registered&&Neurotic::Context::CompleteResourceStructure(*registered)&&!Neurotic::Context::CompleteContent(*registered),
          "authentic callback attaches structural resource owner without content");
    auto repeated=Process::Capture(*host,registry);
    check(registered&&repeated&&Process::Bind(*host,*repeated,list.Get(),&params)&&Registered(*repeated,"Color")==registered,
          "callbacks share same canonical retained resource registration");
    params.resources[1]=nullptr;
    check(repeated&&Process::ObserveSrReturn(*host,*repeated,&params,0)&&
          repeated->RequiredSrReturn()&&!repeated->RequiredSrReturn()->callerBindingMatches,
          "SR result cannot certify a changed caller Output binding");
    params.resources[1]=originalOutput;
    auto failedSr=Process::Capture(*host,registry);
    check(failedSr&&Process::Bind(*host,*failedSr,list.Get(),&params)&&
          Process::ObserveSrReturn(*host,*failedSr,&params,1)&&failedSr->RequiredSrReturn()&&
          failedSr->RequiredSrReturn()->result==1&&!failedSr->RequiredSrReturn()->succeeded&&
          failedSr->RequiredSrReturn()->callerBindingMatches,
          "actual SR failure result remains distinct from unchanged caller binding");
    auto thrownRead=Process::Capture(*host,registry);
    check(thrownRead&&Process::Bind(*host,*thrownRead,list.Get(),&params),"throwing-result fixture binds before return");
    params.throws=true;
    check(thrownRead&&Process::ObserveSrReturn(*host,*thrownRead,&params,0)&&
          thrownRead->RequiredSrReturn()&&!thrownRead->RequiredSrReturn()->callerBindingMatches,
          "post-SR parameter read exception cannot certify target binding");
    params.throws=false;
    check(repeated&&SameProvider(*callback,*repeated),"successive samples retain one source provider incarnation");
    check(!Process::Bind(*host,*callback,list.Get(),&params)&&callback->OriginalCall()==originalCall,
          "same callback never recaptures changed parameters");
    auto retry=Process::Capture(*host,registry);
    check(retry&&!Process::Bind(*host,*retry,nullptr,&params)&&!Process::Bind(*host,*retry,list.Get(),&params),
          "refused capture cannot retry the same sample");
    auto foreignCallback=Process::Capture(*host,registry);
    check(foreignCallback&&!Process::Bind(*foreign,*foreignCallback,list.Get(),&params),"foreign root cannot attach a call");
    auto moved=std::move(*callback);callback.reset();
    check(moved.OriginalCall()==originalCall&&moved.Current(),"moving callback preserves exact retained capture");
    CheckPublication(moved,check);
    Process::Close(*host);
    check(!moved.Current()&&!Process::Bind(*host,*foreignCallback,list.Get(),&params),"closed source cannot attach a new call");
    check(!moved.CallerOutputTarget(),"closed source revokes caller-target generation expectation");
    CheckPublication(moved,check);
    check(!Process::Capture(*host,registry),"closed registry refuses another callback without dereferencing an empty pin");
    DlssNr::NativeFeatureRegistry<unsigned> closingRegistry;closingRegistry.Set(7,11,creation);
    auto closing=Process::Open();auto closingCallback=Process::Capture(*closing,closingRegistry);
    params.onRead=[&]{Process::Close(*closing);};
    check(closingCallback&&!Process::Bind(*closing,*closingCallback,list.Get(),&params)&&!closingCallback->OriginalCall(),
          "reentrant source closure refuses partial input capture");
    DlssNr::NativeFeatureRegistry<unsigned> refusingRegistry;refusingRegistry.Set(7,11,creation);
    auto refusing=Process::Open();auto refusingCallback=Process::Capture(*refusing,refusingRegistry);
    auto unavailableJournal=Process::RemoveSourceJournal(*refusing);(void)unavailableJournal;
    check(refusingCallback&&!Process::Bind(*refusing,*refusingCallback,list.Get(),&params)&&refusingCallback->OriginalCall()&&
          refusingCallback->ObservedSource()&&!refusingCallback->ObservedSource()->Complete(),
          "source publication failure retains descriptive capture and refuses completion");
    check(refusingCallback&&!Process::Bind(*refusing,*refusingCallback,list.Get(),&params),
          "source publication failure cannot retry original callback");
    retry.reset();foreignCallback.reset();absentExposure.reset();repeated.reset();
    } // every source callback capture is gone before the isolated retention check
    for(auto& texture:textures)texture.Reset();missing.reset();nonfinite.reset();
    ComPtr<ID3D12Device> held;Api(captured->Resource("Color")->GetDevice(IID_PPV_ARGS(&held)));
    check(held.Get()==device.Get(),"captured resource remains live after caller releases reference");
    Api(list->Close());
#else
    check(false,"actual original NGX call capture exists");
#endif
    std::cout<<"native-ngx-call-capture checks="<<count<<" failed="<<failed<<"; controlled WARP plus synthetic NGX parameters only\n";
    return failed?1:0;
}
