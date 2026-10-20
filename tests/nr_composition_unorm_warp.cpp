#include "nr_color_compose_fixture.h"
#include <array>
#include <cstddef>
#include <iomanip>

static_assert(sizeof(DlssNrConstants)==256);
static_assert(offsetof(DlssNrConstants,FinalLuminanceGuard)==104);
static_assert(offsetof(DlssNrConstants,OutputIsUnorm)==108);
using Pixel=std::array<float,4>;
struct Case { const char* name; std::array<unsigned char,4> original; Pixel model; };
static Pixel Decode(const std::array<unsigned char,4>& bytes) {
    Pixel p{};for(unsigned i=0;i<4;++i)p[i]=bytes[i]/255.f;return p;
}
static double Luma(const Pixel& p) { return .2126*p[0]+.7152*p[1]+.0722*p[2]; }
static Pixel Run(Warp& w,const Case& test,bool unorm,bool guard,float colour,
                 unsigned replace=0,unsigned debug=0,bool apply=true) {
    const auto fp=DXGI_FORMAT_R32G32B32A32_FLOAT;
    const auto actualFormat=unorm?DXGI_FORMAT_R8G8B8A8_UNORM:fp;
    auto original=w.texture(actualFormat,1,1,D3D12_RESOURCE_STATE_COPY_DEST);
    auto proxy=w.texture(fp,1,1,D3D12_RESOURCE_STATE_COPY_DEST);
    auto model=w.texture(fp,1,1,D3D12_RESOURCE_STATE_COPY_DEST);
    auto output=w.texture(actualFormat,1,1,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto decoded=Decode(test.original);
    if(unorm)w.upload(original.Get(),test.original.data(),4);
    else w.upload(original.Get(),decoded.data(),16);
    w.upload(proxy.Get(),decoded.data(),16);w.upload(model.Get(),test.model.data(),16);
    DlssNrConstants c{};c.Mode=DlssNrMode_Resolve;c.Width=c.Height=1;
    c.WhitePoint=1;c.Passthrough=1;c.TransferStrength=1.4f;c.ColourStrength=colour;
    c.MaxRatio=2;c.Transfer=1;c.ApplyModel=apply?1u:0u;
    c.FinalLuminanceGuard=guard?1u:0u;
    c.OutputIsUnorm=output->GetDesc().Format==DXGI_FORMAT_R8G8B8A8_UNORM?1u:0u;
    c.ReversibleMode=replace;c.DebugView=debug;c.DebugScale=1;
    Compose resolve(w,c,proxy.Get(),model.Get(),original.Get(),output.Get(),nullptr);
    w.run([&](auto* list){resolve.dispatch(list,1,1);});
    const auto bytes=w.read(output.Get(),unorm?4:16);
    Pixel result{};
    if(unorm)for(unsigned i=0;i<4;++i)result[i]=bytes[i]/255.f;
    else std::memcpy(result.data(),bytes.data(),16);
    return result;
}
static bool Equal(const Pixel& a,const Pixel& b) { return std::memcmp(a.data(),b.data(),16)==0; }
static void Output(const Case& test,bool unorm,bool guard,float colour,const Pixel& p) {
    std::cout<<"OUTPUT "<<test.name<<" unorm="<<unorm<<" colour="<<colour<<" guard="<<guard<<" bits=";
    for(float v:p){uint32_t b;std::memcpy(&b,&v,4);std::cout<<std::hex<<b<<',';}
    std::cout<<std::dec<<'\n';
}
int main() {
    std::cout.setf(std::ios::unitbuf);Warp w;unsigned checks=0,failures=0;
    const Case cases[]={
        {"red-blue",{255,0,0,128},{0,0,1,1}},
        {"green-blue",{0,255,0,64},{0,0,1,1}},
        {"white-gain",{204,204,204,255},{1,1,1,1}}
    };
    // Actual byte readback is the oracle. Each R8 channel rounds by <=0.5/255;
    // positive luminance weights sum to one. The extra 2e-6 covers FP arithmetic,
    // far below the red-blue clipping defect (~0.0341). No relaxed dark floor.
    constexpr double unormTolerance=(.2126+.7152+.0722)*.5/255+2e-6;
    for(const auto& test:cases)for(float colour:{1.f,1.3f})for(bool guard:{false,true}) {
        const auto p=Run(w,test,true,guard,colour);Output(test,true,guard,colour,p);
        if(!guard)continue;
        const auto o=Decode(test.original);const double y=Luma(o),actual=Luma(p);
        const bool bounded=actual>=y/2-unormTolerance&&actual<=y*2+unormTolerance;
        bool finite=true;for(float v:p)finite=finite&&std::isfinite(v);
        const bool alpha=p[3]==o[3];
        const bool edit=std::max({std::abs(p[0]-o[0]),std::abs(p[1]-o[1]),std::abs(p[2]-o[2])})>1.f/255;
        ++checks;if(!bounded||!finite||!alpha||!edit){++failures;
            std::cerr<<"FAIL typed-guard "<<test.name<<" colour="<<colour<<" original="<<y
                <<" result="<<actual<<" bounded="<<bounded<<" finite="<<finite<<" alpha="<<alpha<<" visible-edit="<<edit<<'\n';}
    }
    // Same red->blue input stored as float must retain legitimate >1 chroma.
    // Both guard states are also compared bitwise against review02 externally.
    for(float colour:{1.f,1.3f})for(bool guard:{false,true}) {
        const auto p=Run(w,cases[0],false,guard,colour);Output(cases[0],false,guard,colour,p);
        if(!guard)continue;
        const auto o=Decode(cases[0].original);const double y=Luma(o),actual=Luma(p);
        bool finite=true;for(float v:p)finite=finite&&std::isfinite(v);
        ++checks;if(!finite||p[3]!=o[3]||actual<y/2-2e-6||actual>y*2+2e-6||p[2]<=1.f){
            ++failures;std::cerr<<"FAIL float-range colour="<<colour<<" result="<<actual<<" blue="<<p[2]<<'\n';}
    }
    // Exclusions exercise the newly bounded storage path itself.
    for(unsigned replace:{2u,4u}) {
        const auto a=Run(w,cases[0],true,false,1.3f,replace),b=Run(w,cases[0],true,true,1.3f,replace);
        ++checks;if(!Equal(a,b)){++failures;std::cerr<<"FAIL replace "<<replace<<'\n';}
    }
    for(unsigned debug:{1u,2u,3u}) {
        const auto a=Run(w,cases[0],true,false,1.3f,0,debug),b=Run(w,cases[0],true,true,1.3f,0,debug);
        ++checks;if(!Equal(a,b)){++failures;std::cerr<<"FAIL debug "<<debug<<'\n';}
    }
    const auto off=Run(w,cases[0],true,true,1.3f,0,0,false);
    ++checks;if(!Equal(off,Decode(cases[0].original))){++failures;std::cerr<<"FAIL apply-off\n";}
    auto identity=cases[0];identity.model=Decode(identity.original);
    const auto same=Run(w,identity,true,true,1.3f);
    ++checks;if(!Equal(same,Decode(identity.original))){++failures;std::cerr<<"FAIL zero-edit\n";}
    w.clean();std::cout<<"Checks "<<checks<<" failures "<<failures<<'\n';return failures?1:0;
}
