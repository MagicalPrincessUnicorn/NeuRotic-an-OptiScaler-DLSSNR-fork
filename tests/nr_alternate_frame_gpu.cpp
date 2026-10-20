#ifndef NR_AFNR_TEST
#define NR_AFNR_TEST
#endif
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#define NR_AFNR_TEST
#include "../OptiScaler/dlssnr/NrAlternateFrameDx12.cpp"
#include "afnr_warp_fixture.h"
using namespace DlssNr::AlternateFrame;
using namespace DirectX::PackedVector;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
int main(){
    AfnrWarp w;
    for(auto sceneFormat:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_TYPELESS}) {
    Renderer renderer(w.device.Get());
    auto base=w.Texture(sceneFormat,8,8),enhanced=w.Texture(sceneFormat,8,8);
    auto target=w.Texture(sceneFormat,8,8),depth=w.Texture(DXGI_FORMAT_R32_FLOAT,8,8);
    auto motion=w.Texture(DXGI_FORMAT_R32G32_FLOAT,8,8);
    std::array<XMHALF4,64> b,e,current;std::array<float,64> z;std::array<std::array<float,2>,64> mv{};
    for(unsigned n=0;n<64;++n){b[n]=XMHALF4(10,20,30,.75f);e[n]=XMHALF4(12,19,31,.5f);current[n]=XMHALF4(3,4,5,.25f);z[n]=.5f;}
    // One invalid anchor pixel and one invalid current pixel must preserve current bits.
    e[9].x=0x7c00;current[20].w=0x7e55;
    w.Upload(base.Get(),b.data(),8,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    w.Upload(enhanced.Get(),e.data(),8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    w.Upload(target.Get(),current.data(),8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    w.Upload(depth.Get(),z.data(),4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    w.Upload(motion.Get(),mv.data(),8,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    GpuRequest r;r.queue=w.queue.Get();r.base=base.Get();r.target=enhanced.Get();r.depth=depth.Get();r.motion=motion.Get();
    auto& v=r.source;auto source=SourceRef::Fixture(1);
    v.source=source;v.identityObserved=true;v.d3d12=v.postSr=v.rgba16f=v.currentAccess=v.clockKnown=v.originalMetadata=true;
    v.width=v.height=v.workWidth=v.workHeight=8;v.samples=1;v.timeUs=10000;v.scope=v.linearDomain=v.scale.domain=1;
    v.motion=v.depth={0,0,8,8,8,8};v.motionConvention=v.depthEncoding=Provenance::SchemaDefined;
    w.Run([&](auto* list){r.list=list;auto p=renderer.PrepareCapture(r);CHECK(p);CHECK(renderer.RecordCapture(*p).recorded);});
    CHECK(renderer.AcceptCapture(source));CHECK(renderer.Anchor());
    v.predecessor=source;source=SourceRef::Fixture(2);v.source=source;v.timeUs=20000;r.target=target.Get();
    w.Run([&](auto* list){r.list=list;auto p=renderer.PrepareCarry(r);CHECK(p);auto result=renderer.RecordCarry(*p);CHECK(result.recorded);CHECK(result.possibleEffects);});
    const auto bytes=w.Read(target.Get(),8);
    for(unsigned n=0;n<64;++n){XMHALF4 pixel;memcpy(&pixel,bytes.data()+n*8,8);
        if(n==9||n==20){if(memcmp(&pixel,&current[n],8)!=0)std::cerr<<"pixel "<<n<<" actual "<<pixel.x<<","<<pixel.y<<","<<pixel.z<<","<<pixel.w<<" expected "<<current[n].x<<","<<current[n].y<<","<<current[n].z<<","<<current[n].w<<'\n';CHECK(memcmp(&pixel,&current[n],8)==0);continue;}
        CHECK(XMConvertHalfToFloat(pixel.x)==3);CHECK(XMConvertHalfToFloat(pixel.y)==4);CHECK(XMConvertHalfToFloat(pixel.z)==5);CHECK(pixel.w==current[n].w);
    }
    unsigned metrics=0;while(auto sample=renderer.Poll()){CHECK(sample->milliseconds.has_value());++metrics;}
    CHECK(metrics==2);CHECK(!renderer.AcceptCapture(source));
    renderer.Invalidate();
    // Fractional two-axis reprojection, signed gradient, jitter removal and
    // exposure rescaling are compared against the independent CPU oracle.
    auto gradient=w.Texture(sceneFormat,8,8),warped=w.Texture(sceneFormat,8,8);
    auto moving=w.Texture(DXGI_FORMAT_R32G32_FLOAT,8,8);
    std::array<ResidualSample,64> residual;
    for(unsigned n=0;n<64;++n){
        e[n]=XMHALF4(10+static_cast<float>(n%8),20-static_cast<float>(n/8),31,.5f);
        residual[n]=CaptureResidual({10,20,30,.75f},{XMConvertHalfToFloat(e[n].x),XMConvertHalfToFloat(e[n].y),31,.5f});
        mv[n]={.5f,.25f};
        current[n]=XMHALF4(20,40,60,.25f);if(n==20)current[n].w=0x7e55;
    }
    w.Upload(gradient.Get(),e.data(),8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    w.Upload(warped.Get(),current.data(),8,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    w.Upload(moving.Get(),mv.data(),8,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    v.source=SourceRef::Fixture(3);v.predecessor.reset();v.timeUs=30000;v.motionToSceneUv={1.f/8,1.f/8};
    v.guidesJittered=v.includesJitter=true;v.jitterUv={-.01f,.005f};v.scale.scale=1.f;
    r.target=gradient.Get();r.motion=moving.Get();
    w.Run([&](auto* list){r.list=list;auto p=renderer.PrepareCapture(r);CHECK(p);CHECK(renderer.RecordCapture(*p).recorded);});
    CHECK(renderer.AcceptCapture(*v.source));
    v.predecessor=v.source;v.source=SourceRef::Fixture(4);v.timeUs=40000;v.jitterUv={.01f,-.005f};v.scale.scale=2.f;r.target=warped.Get();
    w.Run([&](auto* list){r.list=list;auto p=renderer.PrepareCarry(r);CHECK(p);CHECK(renderer.RecordCarry(*p).recorded);});
    const auto mapped=w.Read(warped.Get(),8);
    for(unsigned n=0;n<64;++n){
        ReferenceInput oracle;oracle.base={20,40,60,n==20?std::numeric_limits<float>::quiet_NaN():.25f};
        oracle.qx=(float(n%8)+.5f)/8+.5f/8+.02f;oracle.qy=(float(n/8)+.5f)/8+.25f/8-.01f;
        oracle.width=oracle.height=8;oracle.residual=residual;oracle.depths=z;oracle.currentDepth=.5f;oracle.ratio=2;
        const auto expected=ReconstructPixel(oracle);XMHALF4 pixel;memcpy(&pixel,mapped.data()+n*8,8);
        if(!expected.writeCurrentTarget)CHECK(memcmp(&pixel,&current[n],8)==0);
        else {const XMHALF4 rounded(expected.pixel.r,expected.pixel.g,expected.pixel.b,expected.pixel.a);if(memcmp(&pixel,&rounded,8)!=0)std::cerr<<"fractional pixel "<<n<<" got "<<XMConvertHalfToFloat(pixel.x)<<","<<XMConvertHalfToFloat(pixel.y)<<","<<XMConvertHalfToFloat(pixel.z)<<" expected "<<XMConvertHalfToFloat(rounded.x)<<","<<XMConvertHalfToFloat(rounded.y)<<","<<XMConvertHalfToFloat(rounded.z)<<"\n";CHECK(memcmp(&pixel,&rounded,8)==0);}
    }
    }
    w.Clean();std::cout<<"AFNR GPU PASS "<<checks<<" checks; debug layer clean\n";
}
