#define NR_AFNR_TEST
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../OptiScaler/dlssnr/NrAlternateFrameDx12.cpp"
#include "afnr_warp_fixture.h"
using namespace DlssNr::AlternateFrame;
using namespace DirectX::PackedVector;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
// Full output must not acquire a leading bright mask. Partial valid history
// must retain correction strength; missing same-surface history stays rejected.
int main(){
    AfnrWarp w;
    for(bool packed:{false,true})for(unsigned scenario=0;scenario<15;++scenario){
        const auto format=packed?DXGI_FORMAT_R11G11B10_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;
        Renderer renderer(w.device.Get());
        auto base=w.Texture(format,16,16);
        auto full=w.Texture(format,16,16);
        auto target=w.Texture(format,16,16);
        auto depth=w.Texture(DXGI_FORMAT_R32_FLOAT,16,16),nextDepth=w.Texture(DXGI_FORMAT_R32_FLOAT,16,16);
        auto motion=w.Texture(DXGI_FORMAT_R32G32_FLOAT,16,16);
        std::array<XMHALF4,256>b,e,c;std::array<float,256>z,nz;std::array<std::array<float,2>,256>mv{};
        for(unsigned n=0;n<256;++n){
            b[n]=XMHALF4(10,10,10,.75f);e[n]=XMHALF4(8,8,8,.5f);
            c[n]=XMHALF4(scenario==3||scenario==5?20.f:10.f,scenario==3||scenario==5?20.f:10.f,scenario==3||scenario==5?20.f:10.f,.25f);
            z[n]=nz[n]=.001f;
            if((scenario==1||scenario==7)&&n%16>=8)z[n]=nz[n]=.0001f;
            if(scenario==8&&n%16>=8)z[n]=nz[n]=.00094f;
            if(scenario==9)z[n]=nz[n]=0.f;
            if(scenario==10)e[n]=XMHALF4(42,42,42,.5f);
            if(scenario==11)c[n]=XMHALF4(12,12,12,.25f);
            if(scenario>=12){
                const unsigned boundary=scenario==14?12u:8u;
                z[n]=n%16<boundary?.001f:.0001f;nz[n]=.0001f;
                e[n]=n%16<boundary?XMHALF4(6,6,6,.5f):XMHALF4(8,8,8,.5f);
                mv[n]={scenario==12?.5f:-1.f,0};
            }
            if(scenario==7){z[n]=1-z[n];nz[n]=1-nz[n];}
            if(scenario==2)nz[n]=.0002f;
            if(scenario==4&&n%16>=8)mv[n]={8,0};
            if(scenario==6){e[n]=XMHALF4(10+float(n%16)*.125f,10,10,.5f);mv[n]={1,0};}
        }
        const auto upload=[&](ID3D12Resource* texture,const auto& pixels,D3D12_RESOURCE_STATES state){
            if(packed){std::array<XMFLOAT3PK,256> values;
                for(unsigned i=0;i<256;++i)values[i]=XMFLOAT3PK(XMConvertHalfToFloat(pixels[i].x),XMConvertHalfToFloat(pixels[i].y),XMConvertHalfToFloat(pixels[i].z));
                w.Upload(texture,values.data(),4,state);
            }else w.Upload(texture,pixels.data(),8,state);
        };
        upload(base.Get(),b,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        upload(full.Get(),e,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        upload(target.Get(),c,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        w.Upload(depth.Get(),z.data(),4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        w.Upload(nextDepth.Get(),nz.data(),4,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        w.Upload(motion.Get(),mv.data(),8,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        GpuRequest r;r.queue=w.queue.Get();r.base=base.Get();r.target=full.Get();r.depth=depth.Get();r.motion=motion.Get();
        auto& v=r.source;v.source=SourceRef::Fixture(1);
        v.identityObserved=v.d3d12=v.postSr=v.currentAccess=v.clockKnown=v.originalMetadata=true;v.rgb11f=packed;v.rgba16f=!packed;
        v.width=v.height=v.workWidth=v.workHeight=16;v.samples=1;v.timeUs=10000;v.scope=v.linearDomain=v.scale.domain=1;
        v.motion=v.depth={0,0,16,16,16,16};v.motionConvention=v.depthEncoding=Provenance::SchemaDefined;
        v.motionToSceneUv={1.f/16,1.f/16};v.depthInverted=scenario!=7;v.scale.scale=1.f;
        ComPtr<ID3D12DescriptorHeap> producerHeap,producerCpu;
        if(scenario==10){D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=1;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            HR(w.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&producerHeap)));hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            HR(w.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&producerCpu)));
            D3D12_UNORDERED_ACCESS_VIEW_DESC vd{};vd.Format=format;vd.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            w.device->CreateUnorderedAccessView(full.Get(),nullptr,&vd,producerHeap->GetCPUDescriptorHandleForHeapStart());
            w.device->CreateUnorderedAccessView(full.Get(),nullptr,&vd,producerCpu->GetCPUDescriptorHandleForHeapStart());}
        w.Run([&](auto* l){r.list=l;
            if(scenario==10){ID3D12DescriptorHeap* heaps[]{producerHeap.Get()};l->SetDescriptorHeaps(1,heaps);
                const float fresh[]{8,8,8,.5f};l->ClearUnorderedAccessViewFloat(producerHeap->GetGPUDescriptorHandleForHeapStart(),producerCpu->GetCPUDescriptorHandleForHeapStart(),full.Get(),fresh,0,nullptr);}
            auto p=renderer.PrepareCapture(r);CHECK(p);CHECK(renderer.RecordCapture(*p).recorded);});
        CHECK(renderer.AcceptCapture(*v.source));
        v.predecessor=v.source;v.source=SourceRef::Fixture(2);v.timeUs=20000;
        r.target=target.Get();r.depth=nextDepth.Get();if(scenario==5)v.scale.scale=2.f;
        w.Run([&](auto* l){r.list=l;auto p=renderer.PrepareCarry(r);CHECK(p);CHECK(renderer.RecordCarry(*p).recorded);});
        const unsigned stride=packed?4u:8u;auto f=w.Read(full.Get(),stride),t=w.Read(target.Get(),stride);
        const unsigned probe=8*16+7;float fv,tv;
        if(packed){XMFLOAT3PK fp,tp;memcpy(&fp,f.data()+probe*4,4);memcpy(&tp,t.data()+probe*4,4);
            fv=DirectX::XMVectorGetX(XMLoadFloat3PK(&fp));tv=DirectX::XMVectorGetX(XMLoadFloat3PK(&tp));
        }else {XMHALF4 fp,tp;memcpy(&fp,f.data()+probe*8,8);memcpy(&tp,t.data()+probe*8,8);
            CHECK(fp.w==e[probe].w);CHECK(tp.w==c[probe].w);fv=XMConvertHalfToFloat(fp.x);tv=XMConvertHalfToFloat(tp.x);}
        std::cout<<"scenario="<<scenario<<" packed="<<packed<<" full="<<fv<<" carry="<<tv<<"\n";
        if(scenario==0){CHECK(fv==8);CHECK(tv==8);}
        if(scenario==1||scenario==4||scenario==7){CHECK(fv==8);CHECK(tv==8);}
        if(scenario==2){CHECK(tv==10);}
        if(scenario==3){CHECK(tv==20);}
        if(scenario==5){CHECK(tv==16);}
        if(scenario==6){CHECK(tv==11);}
        if(scenario==8){CHECK(fv==8);CHECK(tv==fv);}
        if(scenario==9||scenario==10){CHECK(fv==8);CHECK(tv==8);}
        if(scenario==11){CHECK(fv==8);CHECK(tv==10);}
        if(scenario==12||scenario==13){CHECK(fv==6);CHECK(tv==8);}
        if(scenario==14){CHECK(fv==6);CHECK(tv==10);}
        if(scenario!=10){
            if(packed){for(unsigned i=0;i<256;++i){XMFLOAT3PK expected(XMConvertHalfToFloat(e[i].x),XMConvertHalfToFloat(e[i].y),XMConvertHalfToFloat(e[i].z));CHECK(memcmp(f.data()+i*4,&expected,4)==0);}}
            else CHECK(memcmp(f.data(),e.data(),sizeof(e))==0);
        }
    }
    w.Clean();std::cout<<"AFNR stability PASS "<<checks<<" checks; unchanged full output, normalized partial history, bounded same-surface repair, reverse depth, shading change, exposure, moving detail, debug clean\n";
}
