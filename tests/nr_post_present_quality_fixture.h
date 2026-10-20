#pragma once
#include "nr_color_compose_fixture.h"
#include <nr/context/ColorOperation.h>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>

// Host orchestration for the existing production encode/filter/resolve pipeline.
// The identity oracle is the authored retained scene, not another compositor.
namespace PostPresentQuality {
struct Geometry { const char* name; UINT colorW, colorH, workW, workH, guideW, guideH; };
struct Exposure { const char* name; bool live; float texel, expectedWhite; };
struct View { UINT reversible=0, debug=0, compare=0; };
enum class Control { Unchanged, Edited, ApplyOff, StrengthZero };
inline const char* Name(Control c) {
    switch(c) { case Control::Unchanged:return "unchanged"; case Control::Edited:return "edited";
        case Control::ApplyOff:return "apply-off"; default:return "strength-zero"; }
}
inline uint64_t Hash(const std::vector<unsigned char>& bytes) {
    uint64_t h=14695981039346656037ull; for(auto b:bytes) { h^=b; h*=1099511628211ull; } return h;
}
inline std::vector<HALF> Scene(const Geometry& g, bool sdr) {
    std::vector<HALF> data(size_t(g.colorW)*g.colorH*4);
    const float grey[]={0.f,.02f,.1f,.25f,.5f,1.f,4.f,16.f};
    for(UINT y=0;y<g.colorH;++y) for(UINT x=0;x<g.colorW;++x) {
        const UINT band=(x/4)%12;
        float rgb[3];
        if(band<8) rgb[0]=rgb[1]=rgb[2]=grey[band];
        else {
            const float checker=((x/3+y/5)&1) ? 1.f : .7f;
            const float ramp=.01f+.25f*float(x%17)/16.f;
            rgb[0]=ramp*checker; rgb[1]=ramp*.57f; rgb[2]=ramp*.31f;
        }
        const size_t p=(size_t(y)*g.colorW+x)*4;
        for(UINT c=0;c<3;++c) data[p+c]=XMConvertFloatToHalf(sdr ? std::min(1.f,rgb[c]) : rgb[c]);
        const float alpha[]={0.f,.25f,1.f}; data[p+3]=XMConvertFloatToHalf(alpha[(x+y)%3]);
    }
    return data;
}
// Independent scalar reference for authored grey patches only. It validates the
// live binding; no model or composed output is synthesized with this reference.
inline double EncodedGrey(double scene, double white, bool passthrough) {
    if(passthrough) return std::clamp(scene,0.0,1.0);
    double v=scene/white;
    if(v>.75) v=.75+.25*(1.-std::exp(-(v-.75)/.25));
    v=std::clamp(v,0.0,1.0);
    return v<.0031308 ? 12.92*v : 1.055*std::pow(v,1./2.4)-.055;
}
inline bool CheckEncode(const Geometry& g, const std::vector<HALF>& scene,
                        const std::vector<unsigned char>& bytes, float white, bool sdr) {
    bool ok=true;
    for(UINT x=0;x<std::min(g.colorW,32u);x+=4) {
        const size_t p=size_t(x)*4;
        const double expected=EncodedGrey(XMConvertHalfToFloat(scene[p]),white,sdr)*255.;
        for(UINT c=0;c<3;++c) if(std::abs(double(bytes[p+c])-expected)>1.0) {
            std::cerr<<"FAIL encode reference x="<<x<<" channel="<<c<<" expected="<<expected<<" byte="<<int(bytes[p+c])<<'\n'; ok=false;
        }
    }
    return ok;
}
struct BoundOperation { Neurotic::Context::ModelColorOperation color; ID3D12Resource* exposure; float texel; };
inline bool Coherent(const BoundOperation& a,const BoundOperation& b) {
    const auto& x=a.color; const auto& y=b.color;
    return x.passthrough==y.passthrough && x.whitePoint==y.whitePoint &&
        x.signedOriginal==y.signedOriginal && x.useGameExposure==y.useGameExposure &&
        x.exposurePreMul==y.exposurePreMul && x.reversibleMode==y.reversibleMode &&
        (!x.useGameExposure || (a.exposure==b.exposure && std::memcmp(&a.texel,&b.texel,sizeof(float))==0));
}
inline bool CheckResult(const std::vector<HALF>& expected,const std::vector<unsigned char>& actual,int identity,bool directAlpha=true) {
    if(actual.size()!=expected.size()*sizeof(HALF)) return false;
    size_t changed=0,first=expected.size(),alphaErrors=0,nonfinite=0; double maxAbs=0,sumAbs=0,maxRelative=0; unsigned maxUlp=0;
    for(size_t i=0;i<expected.size();++i) {
        HALF h; std::memcpy(&h,actual.data()+i*sizeof(HALF),sizeof(h));
        const float a=XMConvertHalfToFloat(h),e=XMConvertHalfToFloat(expected[i]);
        if(!std::isfinite(a)) ++nonfinite;
        if(i%4==3) { if(h!=expected[i]) ++alphaErrors; continue; }
        if(h!=expected[i]) { ++changed; first=std::min(first,i); }
        const double error=std::abs(double(a)-e); maxAbs=std::max(maxAbs,error); sumAbs+=error;
        if(e!=0) maxRelative=std::max(maxRelative,error/std::abs(double(e)));
        // All authored originals/results are nonnegative; unsigned half bit
        // distance is the representable ULP distance in this test domain.
        maxUlp=std::max(maxUlp,unsigned(std::abs(int(h)-int(expected[i]))));
    }
    std::cout<<" result changed="<<changed<<" max_abs="<<maxAbs<<" mean_abs="<<sumAbs/(expected.size()/4*3)
        <<" max_relative="<<maxRelative<<" max_ulp="<<maxUlp<<" first_channel="<<first
        <<" alpha_errors="<<(directAlpha?alphaErrors:0)<<" projected_alpha_differences="<<(directAlpha?0:alphaErrors)
        <<" nonfinite="<<nonfinite<<'\n';
    // No tolerance is needed: exact zero edit has a retained FP16 source.
    return nonfinite==0 && (!directAlpha || alphaErrors==0) && (identity<0 || (identity ? changed==0 : changed>0));
}
inline bool RunCase(Warp& w,const Geometry& g,const Exposure& exposure,Control control,bool sdr,bool mismatch=false,
                    std::vector<unsigned char>* captured=nullptr,const std::vector<unsigned char>* unchanged=nullptr,View view={}) {
    const auto pixels=Scene(g,sdr);
    auto source=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,g.colorW,g.colorH,D3D12_RESOURCE_STATE_COPY_DEST);
    w.upload(source.Get(),pixels.data(),8);
    auto proxy=w.texture(DXGI_FORMAT_R8G8B8A8_UNORM,g.colorW,g.colorH,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto keep=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,g.colorW,g.colorH,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto output=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,g.colorW,g.colorH,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto live=w.texture(DXGI_FORMAT_R32_FLOAT,1,1,D3D12_RESOURCE_STATE_COPY_DEST); w.upload(live.Get(),&exposure.texel,4);
    BoundOperation encodeOp{{sdr?1u:0u,2.5f,0u,exposure.live?1u:0u,1.f,view.reversible},exposure.live?live.Get():nullptr,exposure.texel};
    BoundOperation resolveOp=encodeOp;
    ComPtr<ID3D12Resource> differentExposure;
    if(mismatch) {
        const float one=1.f;
        differentExposure=w.texture(DXGI_FORMAT_R32_FLOAT,1,1,D3D12_RESOURCE_STATE_COPY_DEST);
        w.upload(differentExposure.Get(),&one,4);
        resolveOp.exposure=differentExposure.Get();resolveOp.texel=one;
    }
    const bool coherent=Coherent(encodeOp,resolveOp);
    if(coherent==mismatch) return false;
    DlssNrConstants c{}; c.Width=g.colorW;c.Height=g.colorH;c.TransferStrength=c.ColourStrength=1.f;c.MaxRatio=2.f;c.ApplyModel=1;c.Transfer=1;
    Neurotic::Context::ApplyModelColor(encodeOp.color,c);
    Compose encode(w,c,source.Get(),nullptr,nullptr,proxy.Get(),keep.Get(),encodeOp.exposure);
    w.run([&](auto* l){encode.dispatch(l,g.colorW,g.colorH);});
    const auto encoded=w.read(proxy.Get(),4);
    // Other recipes are exact baseline/candidate boundary controls, not claims
    // that the mode0 scalar curve validates their distinct encoders.
    bool ok=view.reversible==0 ? CheckEncode(g,pixels,encoded,exposure.expectedWhite,sdr) : true;
    w.run([&](auto* l){Warp::barrier(l,proxy.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Warp::barrier(l,keep.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
    auto work=w.texture(DXGI_FORMAT_R8G8B8A8_UNORM,g.workW,g.workH,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto dc=c; dc.Mode=2; dc.Width=g.workW; dc.Height=g.workH;
    Compose down(w,dc,proxy.Get(),nullptr,nullptr,work.Get(),nullptr);
    w.run([&](auto* l){down.dispatch(l,g.workW,g.workH);});
    const auto submitted=w.read(work.Get(),4); auto answer=submitted;
    if(control!=Control::Unchanged) for(size_t i=0;i<answer.size();i+=4) {
        answer[i]=static_cast<unsigned char>(std::min(255,int(answer[i])+16));
        answer[i+1]=static_cast<unsigned char>(std::max(0,int(answer[i+1])-8));
    }
    auto model=w.texture(DXGI_FORMAT_R8G8B8A8_UNORM,g.workW,g.workH,D3D12_RESOURCE_STATE_COPY_DEST);
    w.upload(model.Get(),answer.data(),4);
    w.run([&](auto* l){Warp::barrier(l,work.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
    c.Mode=1; c.ApplyModel=control==Control::ApplyOff?0u:1u;
    c.DebugView=view.debug;c.DebugScale=1.f;c.CompareMode=view.compare;c.CompareZoom=1.f;c.CompareSplit=.5f;
    if(control==Control::StrengthZero) c.TransferStrength=c.ColourStrength=0.f;
    Neurotic::Context::ApplyModelColor(resolveOp.color,c);
    Compose resolve(w,c,work.Get(),model.Get(),keep.Get(),output.Get(),nullptr,resolveOp.exposure);
    w.run([&](auto* l){resolve.dispatch(l,g.colorW,g.colorH);});
    const auto actual=w.read(output.Get(),8);
    std::cout<<"CASE "<<g.name<<" recipe="<<(sdr?"Present-SDR-passthrough":"Native-HDR-unsigned")
        <<" control="<<Name(control)<<" color="<<g.colorW<<'x'<<g.colorH<<" work="<<g.workW<<'x'<<g.workH
        <<" final="<<g.colorW<<'x'<<g.colorH<<" guides_metadata_only="<<g.guideW<<'x'<<g.guideH
        <<" exposure="<<exposure.name<<" texel="<<exposure.texel<<" exposure_resource="<<encodeOp.exposure
        <<" resolve_exposure_resource="<<resolveOp.exposure<<" resolve_texel="<<resolveOp.texel
        <<" fallback=2.5 preMul=1 expected_white="<<exposure.expectedWhite<<" coherent="<<coherent
        <<" fault_injected="<<mismatch
        <<" proxy_format=R8G8B8A8_UNORM scene_format=R16G16B16A16_FLOAT mode="<<view.reversible
        <<" debug="<<view.debug<<" compare="<<view.compare
        <<" submitted_hash="<<std::hex<<Hash(submitted)<<" model_hash="<<Hash(answer)
        <<" output_hash="<<Hash(actual)<<std::dec;
    // A deliberately incoherent pair must be rejected by pair provenance even
    // when zero-edit output happens to mask it. Its pixels are only checked for
    // finite values and alpha, never reported as an admitted identity pass.
    const bool boundary=view.debug!=0 || view.compare!=0 || view.reversible==2 || view.reversible==4;
    // Side-by-side projects/samples original alpha too. Its full RGBA hash is
    // checked against the baseline, not against unprojected source alpha.
    ok=CheckResult(pixels,actual,(mismatch||boundary)?-1:int(control!=Control::Edited),view.compare!=1)&&ok;
    if(control==Control::Edited) {
        // Comparing only to the original can pass when an ignored model edit
        // leaves the compositor's own zero-edit representation drift behind.
        // Compare to the actual unchanged run with this exact fixture input.
        const bool editDelivered=unchanged && actual.size()==unchanged->size() && actual!=*unchanged;
        std::cout<<" edited_differs_from_actual_unchanged="<<editDelivered<<'\n';
        ok=editDelivered&&ok;
    }
    if(captured) *captured=actual;
    if(control==Control::Unchanged && submitted!=answer) ok=false;
    if(!ok) std::cerr<<"FAIL CASE "<<g.name<<' '<<exposure.name<<' '<<Name(control)<<'\n';
    return ok;
}
inline bool MismatchNegative(Warp& w) {
    return RunCase(w,{"MISMATCH-NEGATIVE",48,16,29,9,31,11},
        {"live-4",true,4.f,.25f},Control::Unchanged,false,true);
}
inline int Run(Warp& w,bool large) {
    int failures=0;
    ComPtr<IDXGIFactory4> factory;HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;const auto luid=w.d->GetAdapterLuid();
    HR(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 desc{};HR(adapter->GetDesc1(&desc));
    LARGE_INTEGER driver{};const auto driverResult=adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver);
    std::cout<<"DEVICE api=D3D12 adapter=explicit-WARP vendor="<<std::hex<<desc.VendorId
        <<" device="<<desc.DeviceId<<" luid="<<luid.HighPart<<':'<<luid.LowPart
        <<" driver_query_hresult="<<driverResult<<" driver_value="<<driver.QuadPart<<std::dec
        <<" requested_feature_level=11_0 debug_layer=enabled\n";
    const Exposure fixed{"fixed",false,1.f,2.5f};
    const Exposure exposures[]={fixed,{"live-4",true,4.f,.25f},{"live-1",true,1.f,1.f},
        {"live-quarter",true,.25f,4.f},{"fallback-zero",true,0.f,2.5f},
        {"fallback-NaN",true,std::numeric_limits<float>::quiet_NaN(),2.5f},
        {"fallback-outside-domain",true,.0001f,2.5f}};
    const Geometry smoke[]={ {"SMOKE-FULL",48,16,48,16,31,11},{"SMOKE-REDUCED",48,16,29,9,31,11} };
    const Geometry geometry[]={ {"N-FULL",3840,2160,3840,2160,2258,1270},
        {"N-MATCH",3840,2160,2256,1264,2258,1270},{"N-HALF",3840,2160,1920,1080,2258,1270},
        {"PRE-RAW-CONTROL",2258,1270,2256,1264,2258,1270},{"PRESENT-HALF",1920,1080,960,540,0,0} };
    const auto* cases=large?geometry:smoke; const size_t count=large?std::size(geometry):std::size(smoke);
    for(size_t i=0;i<count;++i) {
        const auto& g=cases[i];
        std::vector<unsigned char> unchanged;
        if(!RunCase(w,g,fixed,Control::Unchanged,i==4,false,&unchanged)) ++failures;
        for(Control control:{Control::Edited,Control::ApplyOff,Control::StrengthZero})
            if(!RunCase(w,g,fixed,control,i==4,false,nullptr,&unchanged)) ++failures;
        // Full and matched Native live normalization; no scene-exposure claim
        // for Present. Reduced/full smoke exercises the same bindings cheaply.
        if(i<2) for(size_t j=1;j<std::size(exposures);++j)
            if(!RunCase(w,g,exposures[j],Control::Unchanged,false)) ++failures;
    }
    if(!large) for(const auto& g:smoke) {
        std::vector<unsigned char> unchanged;
        if(!RunCase(w,g,fixed,Control::Unchanged,true,false,&unchanged)) ++failures;
        for(Control control:{Control::Edited,Control::ApplyOff,Control::StrengthZero})
            if(!RunCase(w,g,fixed,control,true,false,nullptr,&unchanged)) ++failures;
    }
    if(!large) for(const auto& g:smoke) {
        // Exact output hashes must match baseline for excluded replacement,
        // debug and compare paths. Composed1/3 separately require identity.
        for(UINT mode:{1u,2u,3u,4u})
            if(!RunCase(w,g,fixed,Control::Unchanged,false,false,nullptr,nullptr,{mode,0,0})) ++failures;
        for(UINT debug:{1u,2u,3u})
            if(!RunCase(w,g,fixed,Control::Unchanged,false,false,nullptr,nullptr,{0,debug,0})) ++failures;
        for(UINT compare:{1u,2u})
            if(!RunCase(w,g,fixed,Control::Unchanged,false,false,nullptr,nullptr,{0,0,compare})) ++failures;
    }
    if(!MismatchNegative(w)) ++failures;
    w.clean();
    std::cout<<"QUALITY failures="<<failures<<" geometry="<<(large?"actual":"smoke")
        <<" private_inference=UNTESTED motion_quality=UNQUALIFIED Pre_SR_consumer=UNTESTED\n";
    return failures?1:0;
}
}
