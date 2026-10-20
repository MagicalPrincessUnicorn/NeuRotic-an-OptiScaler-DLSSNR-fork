#define NR_AFNR_TEST
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../OptiScaler/dlssnr/NrAlternateFrameDx12.cpp"
#include "afnr_warp_fixture.h"
using namespace DlssNr::AlternateFrame;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
// Arithmetic oracle: no production shader bit conversion supplies expectations.
static float Decode(unsigned code,unsigned mantissa) {
    const unsigned exp=code>>mantissa,frac=code&((1u<<mantissa)-1);
    if(exp==31)return frac?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity();
    return std::ldexp(float(exp?(1u<<mantissa)+frac:frac),int(exp?exp:1)-15-int(mantissa));
}
static unsigned Encode(float value,unsigned mantissa) {
    if(value==0)return 0;
    int exp=0;std::frexp(value,&exp);exp=(std::max)(exp-1,-14);
    const double scaled=std::ldexp(double(value),int(mantissa)-exp);
    auto integer=static_cast<unsigned>(std::floor(scaled));const auto fraction=scaled-integer;
    if(fraction>.5||(fraction==.5&&(integer&1)))++integer;
    return unsigned(exp+14)*(1u<<mantissa)+integer;
}
static unsigned Pack(float r,float g,float b){return Encode(r,6)|(Encode(g,6)<<11)|(Encode(b,5)<<22);}
static std::array<float,3> Unpack(unsigned value){return {Decode(value&2047,6),Decode((value>>11)&2047,6),Decode(value>>22,5)};}
static ComPtr<ID3D12Resource> Depth(AfnrWarp& w,DXGI_FORMAT format) {
    auto d=CD3DX12_RESOURCE_DESC::Tex2D(format,8,8,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    auto h=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);ComPtr<ID3D12Resource> r;
    D3D12_CLEAR_VALUE clear{};clear.Format=format==DXGI_FORMAT_R32G8X24_TYPELESS?DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        format==DXGI_FORMAT_R24G8_TYPELESS?DXGI_FORMAT_D24_UNORM_S8_UINT:
        format==DXGI_FORMAT_R32_TYPELESS?DXGI_FORMAT_D32_FLOAT:DXGI_FORMAT_D16_UNORM;clear.DepthStencil.Depth=.5f;
    HR(w.device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&r)));
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> heap;HR(w.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=clear.Format;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
    w.device->CreateDepthStencilView(r.Get(),&view,heap->GetCPUDescriptorHandleForHeapStart());
    w.Run([&](auto* l){l->ClearDepthStencilView(heap->GetCPUDescriptorHandleForHeapStart(),D3D12_CLEAR_FLAG_DEPTH,.5f,0,0,nullptr);
        AfnrWarp::Barrier(l,r.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});return r;
}
int main(){
    AfnrWarp w;
    CHECK(Pack(1,1,1)==(960u|(960u<<11)|(480u<<22)));
    CHECK(Encode(1.0078125f,6)==960);CHECK(Encode(1.0234375f,6)==962);
    CHECK(Encode(std::ldexp(1.f,-21),6)==0);CHECK(Encode(std::ldexp(3.f,-21),6)==2);
    for(auto depthFormat:{DXGI_FORMAT_R32G8X24_TYPELESS,DXGI_FORMAT_R24G8_TYPELESS,DXGI_FORMAT_R32_TYPELESS,DXGI_FORMAT_R16_TYPELESS})
    for(bool fractional:{false,true}) {
        auto depth=Depth(w,depthFormat);
        const bool halfMotion=depthFormat==DXGI_FORMAT_R16_TYPELESS;
        const auto motionFormat=halfMotion?DXGI_FORMAT_R16G16_TYPELESS:DXGI_FORMAT_R32G32_TYPELESS;
        Renderer renderer(w.device.Get());
        auto base=w.Texture(DXGI_FORMAT_R11G11B10_FLOAT,8,8),enhanced=w.Texture(DXGI_FORMAT_R11G11B10_FLOAT,8,8);
        auto target=w.Texture(DXGI_FORMAT_R11G11B10_FLOAT,8,8),motion=w.Texture(motionFormat,8,8);
        std::array<unsigned,64>b,e,current;std::array<std::array<float,2>,64>mv{};
        std::array<ResidualSample,64>residual;
        for(unsigned n=0;n<64;++n){b[n]=Pack(10,20,30);e[n]=Pack(10+float(n%8)*.25f,19,31);
            current[n]=Pack(3+float(n%5)*.03125f,4,5);mv[n]={fractional?.5f:0,0};}
        current[9]=Pack(3,.125f,5);current[20]=(current[20]&~2047u)|1985u;
        current[24]=Pack(65024,4,5);e[24]=Pack(12,19,31);e[40]=(e[40]&~2047u)|1984u;
        // Halfway, subnormal and zero-to-small output transitions.
        b[2]=Pack(1,1,1);e[2]=Pack(1.125f,1,1);current[2]=Pack(1,1,1);
        b[3]=b[4]=b[5]=b[6]=0;current[3]=current[4]=0;
        e[3]=Pack(std::ldexp(1.f,-19),std::ldexp(1.f,-19),std::ldexp(1.f,-18));
        e[4]=Pack(std::ldexp(3.f,-19),std::ldexp(3.f,-19),std::ldexp(3.f,-18));
        e[5]=Pack(.03125f,.03125f,.0625f);e[6]=Pack(.09375f,.09375f,.1875f);
        current[5]=current[6]=0;
        for(unsigned n=0;n<64;++n){auto x=Unpack(b[n]),y=Unpack(e[n]);residual[n]=CaptureResidual({x[0],x[1],x[2],1},{y[0],y[1],y[2],1});}
        w.Upload(base.Get(),b.data(),4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        w.Upload(enhanced.Get(),e.data(),4,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        w.Upload(target.Get(),current.data(),4,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(halfMotion){std::array<DirectX::PackedVector::XMHALF2,64> half;
            for(unsigned n=0;n<64;++n)half[n]=DirectX::PackedVector::XMHALF2(mv[n][0],mv[n][1]);
            w.Upload(motion.Get(),half.data(),4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        } else w.Upload(motion.Get(),mv.data(),8,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        GpuRequest r;r.queue=w.queue.Get();r.base=base.Get();r.target=enhanced.Get();r.depth=depth.Get();r.motion=motion.Get();
        auto& v=r.source;v.source=SourceRef::Fixture(1);
        v.identityObserved=v.d3d12=v.postSr=v.rgb11f=v.currentAccess=v.clockKnown=v.originalMetadata=true;
        v.width=v.height=v.workWidth=v.workHeight=8;v.samples=1;v.timeUs=10000;v.scope=v.linearDomain=v.scale.domain=1;
        v.motion=v.depth={0,0,8,8,8,8};v.motionConvention=v.depthEncoding=Provenance::SchemaDefined;
        v.motionToSceneUv={1.f/8,1.f/8};v.scale.scale=1.f;
        w.Run([&](auto* l){r.list=l;auto p=renderer.PrepareCapture(r);CHECK(p);CHECK(renderer.RecordCapture(*p).recorded);});
        CHECK(renderer.AcceptCapture(*v.source));v.predecessor=v.source;v.source=SourceRef::Fixture(2);v.timeUs=20000;
        v.scale.scale=fractional?1.125f:.25f;r.target=target.Get();
        w.Run([&](auto* l){r.list=l;auto p=renderer.PrepareCarry(r);CHECK(p);CHECK(renderer.RecordCarry(*p).recorded);});
        const auto bytes=w.Read(target.Get(),4);
        for(unsigned n=0;n<64;++n){unsigned actual;memcpy(&actual,bytes.data()+4*n,4);auto c=Unpack(current[n]);bool valid=true;
            std::array<float,3>value=c;const unsigned right=n+1;
            if(!residual[n].valid||!std::isfinite(c[0])||(fractional&&(n%8==7||!residual[right].valid)))valid=false;
            // Independently evaluate the exposure-corrected log-luminance confidence.
            const auto luma=[](std::array<float,3> x){return .2126f*x[0]+.7152f*x[1]+.0722f*x[2];};
            const auto storedLuma=[&](unsigned index){return std::exp2(DirectX::PackedVector::XMConvertHalfToFloat(
                DirectX::PackedVector::XMConvertFloatToHalf(std::log2(1+luma(Unpack(b[index]))))))-1;};
            const auto confidence=[&](unsigned index){
                if(!residual[index].valid)return 0.f;
                const float previous=*v.scale.scale*storedLuma(index),now=luma(c);
                const float difference=std::abs(now-previous)/(std::max)(.001f,(std::max)(now,previous));
                const float t=std::clamp((difference-.10f)/.25f,0.f,1.f);return 1-t*t*(3-2*t);
            };
            std::array<float,3> sum{};float total=0;
            const auto add=[&](unsigned index,float spatial){float weight=spatial*confidence(index);if(weight<=0)return;
                for(unsigned k=0;k<3;++k)sum[k]+=weight*residual[index].rgb[k];total+=weight;};
            if(valid){add(n,fractional?.5f:1.f);if(fractional)add(right,.5f);
                if(total<.05f){sum={};total=0;
                    const float px=float(n%8)+(fractional?.5f:0),py=float(n/8);
                    const int cx=int(std::floor(px+.5f)),cy=int(n/8);
                    for(int y=cy-2;y<=cy+2;++y)for(int x=cx-2;x<=cx+2;++x){
                        if(x<0||x>=8||y<0||y>=8)continue;
                        const float dx=float(x)-px,dy=float(y)-py;add(unsigned(y*8+x),1/(1+dx*dx+dy*dy));}}
                if(total<.05f)valid=false;}
            if(valid)for(unsigned k=0;k<3;++k){value[k]+=*v.scale.scale*(sum[k]/total);
                if(!std::isfinite(value[k])||value[k]<0||value[k]>(k==2?64512.f:65024.f))valid=false;}
            const unsigned expected=valid?Pack(value[0],value[1],value[2]):current[n];
            if(actual!=expected)std::cerr<<"pixel "<<n<<" got "<<std::hex<<actual<<" expected "<<expected<<std::dec<<'\n';
            CHECK(actual==expected);
        }
    }
    w.Clean();std::cout<<"AFNR formats PASS "<<checks<<" checks; R11G11B10 scene, real two-plane depth, exact stored outputs, debug clean\n";
}
