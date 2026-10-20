#define main InheritedNativeCaptureTest
#include "../vertical-slice/native_ngx_call_capture.cpp"
#undef main
#include <nr/protocol/NativeEncodeControls.h>
#include "../vertical-slice/native_fixture.h"
namespace C=Neurotic::Contracts;namespace S=Neurotic::Context;namespace L=Neurotic::Lifecycle;namespace P=Neurotic::Protocol;
struct RepresentationParameters
{
    unsigned renderWidth=16;std::array<void*,6> resources{};
    int Get(const char* key,unsigned* value)
    {
        const std::string_view name=key;
        if(name=="Width"||name=="OutWidth")*value=16;
        else if(name=="Height"||name=="OutHeight"||name=="DLSS.Render.Subrect.Dimensions.Height")*value=8;
        else if(name=="DLSS.Render.Subrect.Dimensions.Width")*value=renderWidth;
        else *value=0;return 0;
    }
    int Get(const char* key,float* value)
    {
        const std::string_view name=key;
        *value=name=="Jitter.Offset.X"?.5f:name=="Jitter.Offset.Y"?-.25f:name=="MV.Scale.X"?-2.f:1.f;return 0;
    }
    int Get(const char* key,void** value)
    {
        const auto& names=DlssNr::NativeNgxCallCapture::ResourceNames();
        for(std::size_t i=0;i<names.size();++i)if(std::string_view(key)==names[i]){*value=resources[i];return 0;}return 1;
    }
};
template<class Publication>struct ProfileReader
{
    const Publication& publication;NrContextTests::Store& synthetic;
    const C::AcquisitionCandidate* ResolveCandidate(const C::ContractRef<C::ContractId::C01>& ref)const
    {return publication.ResolveCandidate(ref);}
    template<class T>auto Resolve(const C::MetadataRef<T>& ref)const
    {auto result=publication.Resolve(ref);return result.value?result:synthetic.Resolve(ref);}
    template<class T>auto Publish(const T& value){return synthetic.Publish(value);}
};
template<class Publication,class Check>void VerifyAttachment(L::NativeProcessBootstrap& host,
    L::NativeProcessBootstrap::Callback& callback,const Publication& publication,C::Placement placement,
    const P::NativeEncodeControls& controls,bool expected,NativeTests::Fixture& synthetic,Check& check)
{
    using Access=L::NativeProcessTestAccess;
    if constexpr(requires{publication.RepresentationPreparation();})
    {
        const auto* result=Access::Represent(host,callback,placement,controls);
        check(result&&result==publication.RepresentationPreparation()&&result->bundle.has_value()==expected,
              "same callback retains the genuine C12 result or precise refusal");
        if(!result)return;
        if(!expected)
        {
            const auto reason=result->reason;check(!reason.Empty(),"refused C12 attachment retains reason");
            check(!Access::Represent(host,callback,placement,controls)&&publication.RepresentationPreparation()->reason==reason,
                  "refused preparation cannot retry or replace retained failure");return;
        }
        if(!result->bundle)return;
        const auto& bundle=*result->bundle;
        check(bundle.representations.Size()==4,"four independent Native representation purposes");
        const auto* context=S::ResolveMetadata(bundle.context,publication);
        const auto* certificate=S::ResolveMetadata(bundle.semantic,publication);
        check(context&&certificate&&*context==*publication.ContextPreparation()->context&&
              *certificate==*publication.ContextPreparation()->certificate,"C12 retains exact prior C02 and C04");
        for(std::size_t i=0;i<bundle.representations.Size();++i)
        {
            const auto& use=*bundle.representations.Get(i);
            const auto* plan=S::ResolveMetadata(use.plan,publication);const auto* source=S::ResolveMetadata(use.currentSource,publication);
            check(plan&&source&&plan->purpose==P::Symbol(C::EnumName(use.purpose))&&
                  S::SemanticEqual(plan->source,source->identity)&&!use.currentContent.IsKnown()&&!use.prepared.IsKnown(),
                  "C12 use keeps exact source identity and unknown current pixels");
            if(!plan||!source)continue;
            const char* expectedRole=i<2?(placement==C::Placement::NativeAfter?"Output":"Color"):i==2?"Depth":"MotionVectors";
            const C::AcquisitionCandidate* selected=nullptr;
            for(const auto& candidate:publication.Candidates())
            {
                const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
                if(S::ResolveList(candidate.claims,publication,claims)&&claims&&claims->Size()&&claims->Get(0)->field.View()==expectedRole)selected=&candidate;
            }
            const auto* ref=selected&&selected->payload.IsKnown()?std::get_if<C::MetadataRef<C::ResourceView>>(&selected->payload.KnownPart()->value):nullptr;
            check(ref&&*ref==use.currentSource,"placement selects exact original role and independent guide");
            if(selected)
            {
                C::ContractRef<C::ContractId::C01> original{P::Symbol(C::AcquisitionCandidate::WireName),selected->header.record,selected->header.revision};
                auto wrong=original;++wrong.revision;
                check(publication.ResolveCandidate(original)==selected&&!publication.ResolveCandidate(wrong),"C01 resolution matches complete reference");
                check(use.sourceCandidate&&*use.sourceCandidate==original,"C12 retains exact selected C01 candidate");
            }
            if(i==0)check(P::NativeEncodeForUse(use,publication)==controls&&plan->transforms.count==1,
                          "color declares exact deferred seven-constant encode without prepared pixels");
            if(i==1)check(plan->keys.allocation.IsKnown()&&plan->transforms.count==0&&
                          plan->requiredCapabilities.Size()==2&&plan->accessRequirements.uses.Size()==1&&
                          *plan->accessRequirements.uses.Get(0)==C::UsageKind::WriteExclusive&&
                          plan->accessRequirements.requiresExactContent.IsKnown()&&!plan->accessRequirements.requiresExactContent.KnownPart()->value,
                          "output describes a new writable target without a source conversion");
            if(i>1)check(plan->transforms.count==0&&!plan->keys.allocation.IsKnown(),"original guide remains raw and unscaled");
        }
        // Genuine captured C01/C02/C04/C12; ONLY configuration/runtime/history
        // below are explicitly synthetic. This is not production readiness.
        auto& input=synthetic.request;input.context=bundle.context;input.semantic=bundle.semantic;
        input.currentContext=P::Reference<C::ContractId::C02>(*context);input.profileHeader.scope=context->header.scope;
        input.representations=bundle.representations;ProfileReader<Publication> reader{publication,synthetic.store};
        auto profile=P::PrepareNativeProfile(input,reader);
        if(!profile.projection)std::cerr<<"Profile attachment: "<<profile.reason.View()<<'\n';
        check(profile.projection.has_value(),"actual C12 prepares existing structural profile with explicit synthetic other owners");
        if(profile.projection)
        {
            const auto& projection=profile.projection->Value();
            check(projection.profileCertificate.purpose==P::Symbol("RENDER.NativeStructuralBindings")&&
                  P::ReadNativeEncodeBindings(projection.bindingPlan)==controls,
                  "profile keeps structural evidence distinct and exact encode constants");
            const auto* mv=P::FindBinding(projection.bindingPlan,"DLSSNR.MVecScaleX");
            const auto* jitter=P::FindBinding(projection.bindingPlan,"Jitter.Offset.X");
            check(mv&&jitter&&P::Number(mv->effective)&&P::Number(jitter->effective)&&
                  *P::Number(mv->effective)==-2.0&&*P::Number(jitter->effective)==.5,
                  "source to profile leaves raw signed motion and pixel jitter unapplied");
        }
        const auto current=P::QualifyPreparation(input,reader);
        check(placement==C::Placement::NativeBefore?current.Empty():current.View()=="Native.PostSrOutputPending",
              "Native Before accepts same-callback publication; Native After awaits post-SR Output");
        if(placement==C::Placement::NativeBefore)
        {
            auto& selected=*input.representations.Get(0)->sourceCandidate;
            ++selected.revision;
            check(!P::QualifyPreparation(input,reader).Empty(),"stale selected C01 cannot establish invocation currentness");
            --selected.revision;
            const auto original=selected;selected=*input.representations.Get(2)->sourceCandidate;
            check(!P::QualifyPreparation(input,reader).Empty(),"wrong C01 semantic/role cannot establish invocation currentness");
            selected=original;
            auto savedSource=input.representations.Get(0)->currentSource;
            input.representations.Get(0)->currentSource=input.representations.Get(2)->currentSource;
            check(!P::QualifyPreparation(input,reader).Empty(),"substituted source view cannot enter native invocation");
            input.representations.Get(0)->currentSource=savedSource;
            auto sample=input.representations.Get(0)->nativeSample;
            input.representations.Get(0)->nativeSample={};
            check(!P::QualifyPreparation(input,reader).Empty(),"missing invocation sample cannot enter native invocation");
            input.representations.Get(0)->nativeSample=sample;
        }
        auto savedRuntime=input.runtime;input.runtime.available={};
        check(!P::PrepareNativeProfile(input,reader).projection,"missing runtime availability remains unqualified");input.runtime=savedRuntime;
        check(!Access::Represent(host,callback,placement,controls),"one callback cannot republish C12");
        Access::Close(host);
        check(!callback.Current()&&!Access::Represent(host,callback,placement,controls)&&publication.RepresentationPreparation()->bundle,
              "source close retains historical C12 but refuses current re-entry");
    }
    else check(false,"actual source-to-C12 attachment exists");
}
int main()
{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    Api(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Api(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Api(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Api(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Api(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    std::array<ComPtr<ID3D12Resource>,4> textures;RepresentationParameters parameters;
    for(std::size_t i=0;i<textures.size();++i)
    {
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=16;desc.Height=8;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;
        desc.Format=i==2?DXGI_FORMAT_R32_FLOAT:i==3?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&textures[i])));
        parameters.resources[i]=textures[i].Get();
    }
    using Access=L::NativeProcessTestAccess;
    const auto run=[&](C::Placement placement,P::NativeEncodeControls controls,bool expect,unsigned renderWidth=16,bool loseSupport=false){
        parameters.renderWidth=renderWidth;DlssNr::NativeFeatureRegistry<unsigned> registry;
        registry.Set(7,11,DlssNr::NativeNgxCreationParameters::Capture(&parameters,0));
        auto host=Access::Open();auto callback=Access::Capture(*host,registry);
        check(callback&&Access::Bind(*host,*callback,list.Get(),&parameters),"controlled original callback capture positive control");
        if(!callback||!callback->ObservedSource())return;
        auto synthetic=std::make_unique<NativeTests::Fixture>(false);synthetic->request.placement=placement;
        synthetic->request.requestedProfile=*P::BuildProfileKey(*P::LookupProfile(P::Family::LegacyCompatibleNativeDlssNr,1),
            synthetic->request.runtime,C::GraphicsApi::D3D12,placement,synthetic->request.strategy);
        S::SemanticProfile policy;policy.key=synthetic->request.requestedProfile;
        policy.purpose=P::Symbol("RENDER.NativeSemantics");policy.rule=P::Symbol("CTX.Semantics");
        policy.publication=Access::PolicyPublication(*host);policy.nativeSampleDomainVersion=1;
        policy.boundary=C::BoundaryKind::BeforeUpscale;policy.acceptedEvidence.Push(C::SourceClass::Native);
        policy.sourceSchemas.Push({P::Symbol("NGX"),{1,0},S::SourceSchemaDomain::NativeNgxAdapter});
        for(const auto field:{"color.domain","depth.device","motion.native","jitter"})policy.requirements.Push({P::Symbol(field),true});
        const auto* qualified=Access::Qualify(*host,*callback,policy);
        check(qualified&&qualified->context&&qualified->certificate&&qualified->certificate->eligibility==C::Eligibility::Eligible,
              "real FEED NFC and semantic qualification positive control");
        if(loseSupport)DlssNr::NativeNgxCallCaptureTestAccess::ForgetFormatSupport(
            const_cast<DlssNr::NativeNgxCallCapture&>(*callback->OriginalCall()));
        VerifyAttachment(*host,*callback,*callback->ObservedSource(),placement,controls,expect,*synthetic,check);
    };
    const P::NativeEncodeControls controls{0,203,0,0,3,16,8}; // synthetic configuration, actual WARP source metadata
    run(C::Placement::NativeAfter,controls,true);run(C::Placement::NativeBefore,controls,true);
    auto bad=controls;bad.width=8;run(C::Placement::NativeAfter,bad,false);
    bad=controls;bad.whitePoint=0;run(C::Placement::NativeAfter,bad,false);
    run(C::Placement::NativeBefore,controls,false,8);
    run(C::Placement::NativeAfter,controls,false,16,true);
    std::cout<<"Native C12 attachment: "<<count<<" checks, "<<failed<<" failures; WARP metadata only; model/provider/game NOT_RUN\n";
    return failed?1:0;
}
