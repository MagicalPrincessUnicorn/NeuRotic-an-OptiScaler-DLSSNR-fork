#include "nr_color_compose_fixture.h"
#include <array>
#include <cstddef>
#include <iomanip>

static_assert(sizeof(DlssNrConstants) == 256);
static_assert(offsetof(DlssNrConstants, SourceX) == 96);
static_assert(offsetof(DlssNrConstants, SourceY) == 100);
static_assert(offsetof(DlssNrConstants, FinalLuminanceGuard) == 104);

using Pixel = std::array<float, 4>;
struct Case {
    const char* name; Pixel original, model; bool signedOriginal;
    bool passthrough=true; float white=1.f; bool requireEdit=true;
};

// Independent radiometric oracle: the final positive light must remain inside
// the selected multiplicative guard. It is not a second compositor formula.
static double Luma(const Pixel& p) { return .2126*p[0]+.7152*p[1]+.0722*p[2]; }
static Pixel Run(Warp& w, const Case& test, bool enabled, float colour,
                 unsigned reversible=0, unsigned debug=0, bool apply=true)
{
    const auto format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    auto original=w.texture(format,1,1,D3D12_RESOURCE_STATE_COPY_DEST);
    auto proxy=w.texture(format,1,1,test.passthrough?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto model=w.texture(format,1,1,D3D12_RESOURCE_STATE_COPY_DEST);
    auto output=w.texture(format,1,1,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Pixel positive=test.original;
    for(unsigned i=0;i<3;++i)positive[i]=std::max(positive[i],0.f);
    w.upload(original.Get(),test.original.data(),16);
    Pixel modelValue=test.model;
    if(!test.passthrough)for(unsigned i=0;i<3;++i){
        const float v=std::clamp(modelValue[i],0.f,1.f);
        modelValue[i]=v<.0031308f?12.92f*v:1.055f*std::pow(v,1.f/2.4f)-.055f;
    }
    w.upload(model.Get(),modelValue.data(),16);
    DlssNrConstants c{};
    c.Mode=DlssNrMode_Resolve;c.Width=c.Height=1;c.WhitePoint=test.white;
    c.Passthrough=test.passthrough?1u:0u;c.TransferStrength=1.4f;c.ColourStrength=colour;
    c.MaxRatio=2;c.ApplyModel=apply?1u:0u;c.Transfer=1;
    c.SignedPresent=test.signedOriginal?1u:0u;
    c.FinalLuminanceGuard=enabled?1u:0u;
    c.ReversibleMode=reversible;c.DebugView=debug;c.DebugScale=1;
    ComPtr<ID3D12Resource> keep;
    if(test.passthrough)w.upload(proxy.Get(),positive.data(),16);
    else{
        // HDR proxy and retained original come from the actual production
        // encode; the authored model answer is independent of that encoder.
        keep=w.texture(format,1,1,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto encodeConstants=c;encodeConstants.Mode=DlssNrMode_Encode;
        Compose encode(w,encodeConstants,original.Get(),nullptr,nullptr,proxy.Get(),keep.Get());
        w.run([&](auto* list){encode.dispatch(list,1,1);
            Warp::barrier(list,proxy.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Warp::barrier(list,keep.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
    }
    Compose resolve(w,c,proxy.Get(),model.Get(),keep?keep.Get():original.Get(),output.Get(),nullptr);
    w.run([&](auto* list){resolve.dispatch(list,1,1);});
    const auto bytes=w.read(output.Get(),16);
    Pixel result{};std::memcpy(result.data(),bytes.data(),16);
    return result;
}

int main()
{
    std::cout.setf(std::ios::unitbuf);
    Warp w;
    const Case cases[]={
        {"dark-bright",{.001f,.001f,.001f,.25f},{.004f,.004f,.004f,1},false},
        {"dark-dim",{.004f,.004f,.004f,.5f},{.001f,.001f,.001f,1},false},
        {"near-black",{.0001f,.0001f,.0001f,0},{.002f,.002f,.002f,1},false},
        {"lit-edit",{.25f,.25f,.25f,1},{.30f,.30f,.30f,1},false},
        {"skin-colour",{.012f,.006f,.003f,.75f},{.048f,.012f,.0045f,1},false},
        {"signed-dark",{-.001f,.002f,.004f,.25f},{.003f,.008f,.016f,1},true},
        {"black",{0,0,0,.5f},{.002f,.002f,.002f,1},false,true,1,false},
        {"near-zero",{1e-8f,1e-8f,1e-8f,.25f},{4e-8f,4e-8f,4e-8f,1},false,true,1,false},
        {"hdr-dark",{.016f,.016f,.016f,.25f},{.004f,.004f,.004f,1},false,false,16},
        {"hdr-lit",{4,4,4,.5f},{.30f,.30f,.30f,1},false,false,16},
        {"hdr-signed",{-.001f,.005f,.01f,.75f},{.003f,.008f,.016f,1},true,false,2.5f},
        // Production encode makes this finite bright red a red unit proxy.
        // The authored blue answer exposes the final carrier-clamp regression.
        {"hdr-red-blue-bound",{65504,0,0,.5f},{0,0,1,1},true,false,1}
    };
    unsigned failures=0,checks=0;
    for(const auto& test:cases)for(float colour:{1.f,1.3f})
    {
        Pixel positive=test.original;
        for(unsigned i=0;i<3;++i)positive[i]=std::max(positive[i],0.f);
        const double originalLuma=Luma(positive);
        for(bool enabled:{false,true})
        {
            const auto result=Run(w,test,enabled,colour);
            // Exact legacy outputs are emitted for baseline-vs-candidate
            // byte comparison: flag0 is the protected Pre-SR resolve path.
            std::cout<<"OUTPUT "<<test.name<<" colour="<<colour<<" guard="<<enabled<<" bits=";
            for(float channel:result){uint32_t bits;std::memcpy(&bits,&channel,4);std::cout<<std::hex<<bits<<',';}
            std::cout<<std::dec<<'\n';
            if(!enabled)continue;
            Pixel positiveResult=result;
            if(test.signedOriginal)for(unsigned i=0;i<3;++i)
                positiveResult[i]-=std::min(test.original[i],0.f);
            const double actual=Luma(positiveResult),tolerance=std::max(1e-8,originalLuma*2e-4);
            const bool bounded=actual>=originalLuma/2-tolerance&&actual<=originalLuma*2+tolerance;
            bool finite=true;for(float channel:result)finite=finite&&std::isfinite(channel);
            const bool alpha=result[3]==test.original[3];
            double rgbChange=0,reference=originalLuma;
            for(unsigned i=0;i<3;++i){rgbChange=std::max(rgbChange,double(std::abs(result[i]-test.original[i])));
                reference=std::max(reference,double(std::abs(test.original[i])));}
            const bool visibleEdit=!test.requireEdit||rgbChange>reference*.001;
            ++checks;
            if(!bounded||!finite||!alpha||!visibleEdit)
            {
                ++failures;std::cerr<<"FAIL final-guard "<<test.name<<" colour="<<colour
                    <<" original="<<originalLuma<<" result="<<actual
                    <<" bounded="<<bounded<<" finite="<<finite<<" alpha="<<alpha<<" visible-edit="<<visibleEdit<<'\n';
            }
        }
    }
    // Explicitly excluded views must not change when the correction is enabled.
    const auto& control=cases[4];
    for(unsigned replace:{2u,4u}){
        const auto a=Run(w,control,false,1.3f,replace),b=Run(w,control,true,1.3f,replace);
        ++checks;if(std::memcmp(a.data(),b.data(),16)!=0){++failures;std::cerr<<"FAIL replace preservation "<<replace<<'\n';}
    }
    for(unsigned debug:{1u,2u,3u}){
        const auto a=Run(w,control,false,1.3f,0,debug),b=Run(w,control,true,1.3f,0,debug);
        ++checks;if(std::memcmp(a.data(),b.data(),16)!=0){++failures;std::cerr<<"FAIL debug preservation "<<debug<<'\n';}
    }
    const auto off=Run(w,control,true,1.3f,0,0,false);
    ++checks;if(std::memcmp(off.data(),control.original.data(),16)!=0){++failures;std::cerr<<"FAIL apply-off identity\n";}
    Case identity=control;identity.model=identity.original;
    const auto same=Run(w,identity,true,1.3f);
    ++checks;if(std::memcmp(same.data(),identity.original.data(),16)!=0){++failures;std::cerr<<"FAIL zero-edit identity\n";}
    // Signed carrier endpoint is a separate representability control: channel
    // clamping may constrain attainable chromaticity/luminance at extreme HDR.
    const Case high={"hdr-carrier-edge",{65504,65504,65504,.5f},{.4f,.3f,.2f,1},true,false,4096,false};
    const auto highResult=Run(w,high,true,1.3f);
    bool highSafe=highResult[3]==high.original[3];
    for(unsigned i=0;i<3;++i)highSafe=highSafe&&std::isfinite(highResult[i])&&std::abs(highResult[i])<=65504.f;
    ++checks;if(!highSafe){++failures;std::cerr<<"FAIL signed carrier endpoint\n";}
    w.clean();
    std::cout<<"Checks "<<checks<<" failures "<<failures<<'\n';
    return failures?1:0;
}
