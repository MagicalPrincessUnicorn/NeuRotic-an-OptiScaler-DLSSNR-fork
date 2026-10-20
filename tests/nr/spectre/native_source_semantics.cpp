#define main InheritedNativeCaptureTest
#include "../vertical-slice/native_ngx_call_capture.cpp"
#undef main
#include <nr/context/NativeContextQualification.h>
#include <nr/protocol/BindingPlan.h>
namespace C=Neurotic::Contracts;namespace S=Neurotic::Context;namespace L=Neurotic::Lifecycle;
struct SemanticParameters
{
    unsigned flags=0,colorX=2;bool missingFlags=false;
    std::string_view missingInteger;bool missingScale=false;
    std::array<void*,6> resources{};
    int Get(const char* key,unsigned* value)
    {
        const std::string_view name=key;
        if(name.starts_with("DFC.Feeder.")||name.starts_with("NR.Prepared."))return 1;
        if(name==missingInteger)return 1;
        if(name=="DLSS.Feature.Create.Flags"){if(missingFlags)return 1;*value=flags;return 0;}
        if(name=="Width"||name=="DLSS.Render.Subrect.Dimensions.Width")*value=8;
        else if(name=="Height"||name=="DLSS.Render.Subrect.Dimensions.Height")*value=4;
        else if(name=="OutWidth")*value=16;
        else if(name=="OutHeight")*value=8;
        else if(name=="DLSS.Input.Color.Subrect.Base.X")*value=colorX;
        else if(name=="DLSS.Input.Color.Subrect.Base.Y")*value=1;
        else *value=0;
        return 0;
    }
    int Get(const char* key,float* value)
    {
        const std::string_view name=key;
        if(missingScale&&name=="MV.Scale.Y")return 1;
        *value=name=="Jitter.Offset.X"?.5f:name=="Jitter.Offset.Y"?-.25f:
            name=="MV.Scale.X"?2.5f:name=="MV.Scale.Y"?-3.f:1.f;return 0;
    }
    int Get(const char* key,void** value)
    {
        const auto& names=DlssNr::NativeNgxCallCapture::ResourceNames();
        for(std::size_t i=0;i<names.size();++i)if(key==std::string_view(names[i])){*value=resources[i];return 0;}return 1;
    }
};
template<class Publication,class Check>void CheckContextPreparation(L::NativeProcessBootstrap& host,
    L::NativeProcessBootstrap::Callback& callback,const Publication& publication,bool hdr,Check& check)
{
    if constexpr(requires{publication.ContextPreparation();})
    {
        using Access=L::NativeProcessTestAccess;
        const auto symbol=[](std::string_view text){C::Symbol result;result.Assign(text);return result;};
        S::SemanticProfile policy;policy.key={symbol("NativeTestConsumer"),symbol("OriginalNgxSemantics"),1};
        policy.purpose=symbol("RENDER.NativeSemantics");policy.rule=symbol("CTX.Semantics");
        policy.publication=Access::PolicyPublication(host);policy.nativeSampleDomainVersion=1;
        policy.boundary=C::BoundaryKind::BeforeUpscale;policy.acceptedEvidence.Push(C::SourceClass::Native);
        policy.sourceSchemas.Push({symbol("NGX"),{1,0},S::SourceSchemaDomain::NativeNgxAdapter});
        for(const auto field:{"color.domain","depth.device","motion.native","jitter"})policy.requirements.Push({symbol(field),true});
        std::vector<C::AcquisitionCandidate> original(publication.Candidates().begin(),publication.Candidates().end());
        const auto* result=Access::Qualify(host,callback,policy);
        if(!result||!result->context||!result->certificate)
            std::cerr<<"Context preparation diagnostic: "<<(result?result->reason.View():"no retained result")<<'\n';
        check(result&&result->context&&result->certificate,"actual captured observations prepare NFC through same owner set");
        if(!result||!result->context||!result->certificate)return;
        const auto& context=*result->context;
        check(result==publication.ContextPreparation()&&context.header.scope==callback.Scope().scope&&
              result->certificate->eligibility==(hdr?C::Eligibility::Ineligible:C::Eligibility::Eligible),
              "retained NFC certificate records actual LDR eligibility and HDR uncertainty");
        const C::BoundedList<C::Symbol,32>* missing=nullptr;
        const bool resolved=S::ResolveList(result->certificate->missingFields,publication,missing);
        check(resolved&&(hdr?(missing&&missing->Size()==1&&missing->Get(0)->View()=="color.domain"):
              result->certificate->missingFields.count==0),
              "HDR refusal identifies exactly color.domain; LDR has no missing semantic fields");
        if(hdr&&resolved&&missing)for(const auto& field:*missing)
            std::cout<<"HDR context missing field: "<<field.View()<<'\n';
        check(S::Established(context.jitter)&&context.jitter.KnownPart()->value==C::Vec2{.5/8,-.25/4},
              "actual callback jitter qualifies on independent render raster");
        Neurotic::Protocol::BindingPlan binding;
        const bool built=Neurotic::Protocol::BuildFrameControls(context,C::EvidenceRef{context.header.record},publication,binding);
        const auto* x=Neurotic::Protocol::FindBinding(binding,"Jitter.Offset.X");
        check(built&&x&&Neurotic::Protocol::Number(x->effective)&&*Neurotic::Protocol::Number(x->effective)==.5,
              "actual NFC controls preserve raw pixel jitter for later profile binding");
        check(std::equal(original.begin(),original.end(),publication.Candidates().begin()),"NFC never rewrites published original C01 observations");
        const auto record=context.header.record;const auto* repeated=Access::Qualify(host,callback,policy);
        check(!repeated&&publication.ContextPreparation()->context->header.record==record,
              "one callback preparation cannot republish or replace immutable context");
        Access::Close(host);
        check(!callback.Current()&&!Access::Qualify(host,callback,policy)&&publication.ContextPreparation()->context->header.record==record,
              "source closure retains historical NFC without current admission");
    }
    else check(false,"actual captured observations prepare NFC through same owner set");
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
    std::array<ComPtr<ID3D12Resource>,4> textures;SemanticParameters parameters;
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
    const auto run=[&](unsigned flags,bool missing=false,bool outside=false,std::string_view absent={},bool wrongMotion=false,bool missingScale=false){
        parameters.flags=flags;parameters.missingFlags=missing;parameters.colorX=outside?15u:2u;
        parameters.missingInteger=absent;parameters.missingScale=missingScale;
        parameters.resources[3]=textures[wrongMotion?2:3].Get();
        const auto original=DlssNr::NativeNgxCreationParameters::Capture(&parameters,0);
        DlssNr::NativeFeatureRegistry<unsigned> registry;registry.Set(7,11,original);
        auto host=Access::Open();auto callback=Access::Capture(*host,registry);parameters.flags=15;
        check(callback&&Access::Bind(*host,*callback,list.Get(),&parameters),"semantic fixture retains authentic original callback");
        if(!callback||!callback->ObservedSource()||!callback->ObservedSource()->Complete())return;
        const auto& publication=*callback->ObservedSource();const auto& seed=publication.Source()->seed;
        check(callback->OriginalCreation()==original,"authentic callback retains generation-bound original creation");
        const auto* color=S::ResolveOptional(seed.color,publication);
        const auto* depth=S::ResolveOptional(seed.depth,publication);
        const auto* motion=S::ResolveOptional(seed.motion,publication);
        if(missing||outside||!absent.empty()||wrongMotion||(flags&~0xefu))
        {check(!color&&!depth&&!motion,"unproven original flags geometry or format has no invented declarations");return;}
        check(color&&depth&&motion,"real source declares original API semantics before C01 publication");
        if(!color||!depth||!motion)return;
        check((flags&1)?!color->domain.IsKnown():(S::Established(color->domain)&&color->domain.KnownPart()->value==C::ColorDomain::EncodedDisplay),
              "LDR is encoded while precise HDR domain stays unknown");
        check(S::Established(depth->kind)&&depth->kind.KnownPart()->value==C::DepthKind::Device&&
              S::Established(depth->reversed)&&depth->reversed.KnownPart()->value==bool(flags&8),"original depth flags survive evaluation overrides");
        check(S::Established(motion->units)&&motion->units.KnownPart()->value==C::MotionUnits::Pixels&&
              S::Established(motion->direction)&&motion->direction.KnownPart()->value==C::MotionDirection::CurrentToPrevious&&
              (missingScale?!motion->scale.IsKnown():(S::Established(motion->scale)&&motion->scale.KnownPart()->value==C::Vec2{2.5,-3})),
              "declared signed motion scale stays unapplied or unknown");
        check(motion->jitterConvention.IsKnown()&&motion->jitterConvention.KnownPart()->value==
              ((flags&4)?C::JitterConvention::Jittered:C::JitterConvention::Unjittered),"original motion jitter convention retained");
        check(!motion->currentJitter.IsKnown()&&!motion->previousJitter.IsKnown(),
              "raw source jitter does not impersonate canonical motion jitter");
        const C::Rectangle motionRect{0,0,(flags&2)?8u:16u,(flags&2)?4u:8u};
        check(S::ValidRaster(motion->currentRaster)&&motion->currentRaster.active.KnownPart()->value==motionRect,
              "guide resolution follows original low-resolution flag");
        const auto* frame=S::ResolveMetadata(seed.frame,publication);
        check(frame&&motion->nativeSample==frame->nativeSample&&!motion->framePair.current.IsKnown()&&
              !motion->framePair.previous.IsKnown()&&!frame->baseRealFrameId.IsKnown(),"motion basis invents no previous image or base-frame identity");
        bool jitter=false,rawFlags=false,claimed=true;
        for(const auto& candidate:publication.Candidates())
        {
            const auto* evidence=S::ResolveMetadata(candidate.evidence,publication);
            claimed=claimed&&evidence&&S::Established(evidence->semanticCertainty)&&
                evidence->semanticCertainty.KnownPart()->value==C::SemanticCertainty::Claimed&&!candidate.sourceVersion.IsKnown();
            const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
            if(!S::ResolveList(candidate.claims,publication,claims)||!claims||!claims->Size())continue;
            if(claims->Get(0)->field.View()=="DLSS.Feature.Create.Flags")
                rawFlags=claims->Get(0)->raw.IsKnown()&&claims->Get(0)->raw.KnownPart()->value==C::ScalarValue{std::uint64_t{15}};
            if(candidate.semantic!=C::SemanticKind::Jitter)continue;
            const auto* ref=candidate.payload.IsKnown()?std::get_if<C::MetadataRef<C::ResourceView>>(&candidate.payload.KnownPart()->value):nullptr;
            const auto* view=ref?S::ResolveMetadata(*ref,publication):nullptr;bool basis=false;
            for(const auto& claim:*claims)if(claim.field.View()=="jitter.basis"&&claim.raw.IsKnown())
            {const auto* value=std::get_if<C::Symbol>(&claim.raw.KnownPart()->value);basis=value&&value->View()=="payload.raster";}
            jitter=view&&ref->owner==C::OwnerDomain::Resource&&view->raster.active.KnownPart()->value==C::Rectangle{2,1,8,4}&&
                !candidate.motion.IsKnown()&&candidate.jitter.IsKnown()&&candidate.jitter.KnownPart()->value==C::Vec2{.5,-.25}&&basis&&
                !view->identity.contentRevision.IsKnown()&&view->identity.resourceViewIncarnation!=callback->RegisteredResource("Color")->identity.resourceViewIncarnation;
        }
        check(jitter,"jitter has independent genuine render-raster payload without motion-bundle conflict");
        check(rawFlags&&claimed,"original evaluation values and unqualified evidence remain unchanged");
        if(!missingScale)CheckContextPreparation(*host,*callback,publication,bool(flags&1),check);
    };
    run(0);run(14);run(1);run(3);run(0,true);run(0,false,true);
    run(0x100u);run(0x10u);
    for(const auto name:{"Width","OutWidth","DLSS.Render.Subrect.Dimensions.Height","DLSS.Input.MV.Subrect.Base.X"})run(0,false,false,name);
    run(0,false,false,{},true);run(0,false,false,{},false,true);
    std::cout<<"Native source semantics: "<<count<<" checks, "<<failed<<" failures; WARP resource API and synthetic NGX parameters; no command submission/model\n";
    return failed?1:0;
}
