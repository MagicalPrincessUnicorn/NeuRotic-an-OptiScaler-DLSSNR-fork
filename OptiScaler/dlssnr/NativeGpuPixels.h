#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#include "NativeIdentity.h"
#include <array>
#include <cstring>
#include <string>
namespace DlssNr::NativeGuides {
class GpuPixels {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D12Device> device_;
    Ptr<ID3D12RootSignature> root_;
    Ptr<ID3D12PipelineState> decode_,encode_;
    Ptr<ID3D12DescriptorHeap> heap_;
    Ptr<ID3D12Resource> flag_,zero_,readback_;
    UINT increment_=0;
    static bool No(std::string& r,const char* s){r=s;return false;}
    bool Owned(ID3D12DeviceChild* child)const{Ptr<ID3D12Device> d;return child&&SUCCEEDED(child->GetDevice(IID_PPV_ARGS(&d)))&&NativeIdentity::CompareDevices(device_.Get(),d.Get()).equal;}
    bool Buffer(D3D12_HEAP_TYPE type,Ptr<ID3D12Resource>& out){
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=4;d.Height=d.DepthOrArraySize=d.MipLevels=1;
        d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=type==D3D12_HEAP_TYPE_DEFAULT?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES h{};h.Type=type;
        return SUCCEEDED(device_->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out)));
    }
    void View(ID3D12Resource* resource,DXGI_FORMAT format,unsigned index){
        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=format;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        auto h=heap_->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(index)*increment_;
        device_->CreateUnorderedAccessView(resource,nullptr,&view,h);
    }
    void Bind(ID3D12GraphicsCommandList* list,ID3D12PipelineState* pipeline,unsigned w,unsigned h,unsigned type,bool bgra,unsigned dw=0,unsigned dh=0){
        ID3D12DescriptorHeap* heaps[]={heap_.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root_.Get());list->SetPipelineState(pipeline);
        const unsigned constants[]={w,h,type,bgra?1u:0u,dw,dh};list->SetComputeRoot32BitConstants(0,6,constants,0);
        list->SetComputeRootDescriptorTable(3,heap_->GetGPUDescriptorHandleForHeapStart());
    }
public:
    static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
    }
    bool Initialize(ID3D12Device* device,std::string& reason){
        if(device_)return (Ready()&&NativeIdentity::CompareDevices(device_.Get(),device).equal)||No(reason,"GPU conversion initialization incomplete or device changed");
        if(!device)return No(reason,"GPU conversion device missing");device_=device;
        for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT}){
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
            const auto required=D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE|(format==DXGI_FORMAT_R8G8B8A8_UNORM?D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD:0);
            if(FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||
               !(support.Support1&D3D12_FORMAT_SUPPORT1_TEXTURE2D)||(support.Support2&required)!=required)return No(reason,"GPU conversion typed image operations unsupported");}
        static constexpr char shader[]=R"(
cbuffer Parameters:register(b0){uint width,height,depthType,bgra,depthWidth,depthHeight;}
ByteAddressBuffer colorRaw:register(t0);ByteAddressBuffer depthRaw:register(t1);
RWTexture2D<float4> color:register(u0);RWTexture2D<float> depth:register(u1);
RWByteAddressBuffer raw:register(u2);
float ReadDepth(uint p){
 uint address=p*(depthType==16?2:4);uint bits=depthRaw.Load(address&~3u);
 float z=depthType==16?float((bits>>((address&2)*8))&65535)/65535.:
   depthType==24?float(bits&16777215)/16777215.:asfloat(bits);
 return z;
}
[numthreads(8,8,1)] void Decode(uint3 id:SV_DispatchThreadID){
 // Validate the entire source, including pixels omitted by downsampling.
 if(id.x<depthWidth&&id.y<depthHeight){float z=ReadDepth(id.y*depthWidth+id.x);
  if(!isfinite(z)||z<0||z>1)raw.InterlockedOr(0,1);}
 if(id.x>=width||id.y>=height)return;uint p=id.y*width+id.x;uint c=colorRaw.Load(p*4);
 float4 v=float4(c&255,(c>>8)&255,(c>>16)&255,c>>24)/255.;color[id.xy]=bgra?v.bgra:v;
 uint2 source=uint2(((2*id.x+1)*depthWidth)/(2*width),((2*id.y+1)*depthHeight)/(2*height));
 float z=ReadDepth(source.y*depthWidth+source.x);depth[id.xy]=isfinite(z)&&z>=0&&z<=1?z:0;
}
[numthreads(8,8,1)] void Encode(uint3 id:SV_DispatchThreadID){
 if(id.x>=width||id.y>=height)return;float4 v=color[id.xy];if(bgra)v=v.bgra;
 uint4 c=(uint4)round(saturate(v)*255.);raw.Store((id.y*width+id.x)*4,c.x|(c.y<<8)|(c.z<<16)|(c.w<<24));
})";
        std::array<D3D12_ROOT_PARAMETER,5> parameters{};
        parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[0].Constants={0,0,6};
        parameters[1].ParameterType=parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[1].Descriptor={0,0};parameters[2].Descriptor={1,0};
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,0};
        parameters[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[3].DescriptorTable={1,&range};
        parameters[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;parameters[4].Descriptor={2,0};
        D3D12_ROOT_SIGNATURE_DESC signature{};signature.NumParameters=UINT(parameters.size());signature.pParameters=parameters.data();
        Ptr<ID3DBlob> blob,error;
        if(FAILED(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))||
           FAILED(device_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root_))))return No(reason,"GPU conversion root signature refused");
        for(unsigned i=0;i<2;++i){Ptr<ID3DBlob> code;error.Reset();
            if(FAILED(D3DCompile(shader,sizeof(shader),"NativeGpuPixels",nullptr,nullptr,i?"Encode":"Decode","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error))){
                reason="GPU conversion shader compilation failed";if(error)reason.append(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize());return false;}
            D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=root_.Get();p.CS={code->GetBufferPointer(),code->GetBufferSize()};
            if(FAILED(device_->CreateComputePipelineState(&p,IID_PPV_ARGS(i?&encode_:&decode_))))return No(reason,"GPU conversion pipeline refused");}
        D3D12_DESCRIPTOR_HEAP_DESC h{};h.NumDescriptors=2;h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;h.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(FAILED(device_->CreateDescriptorHeap(&h,IID_PPV_ARGS(&heap_)))||!Buffer(D3D12_HEAP_TYPE_DEFAULT,flag_)||
           !Buffer(D3D12_HEAP_TYPE_UPLOAD,zero_)||!Buffer(D3D12_HEAP_TYPE_READBACK,readback_))return No(reason,"GPU conversion validation resources refused");
        increment_=device_->GetDescriptorHandleIncrementSize(h.Type);void* p=nullptr;D3D12_RANGE none{0,0};
        if(FAILED(zero_->Map(0,&none,&p)))return No(reason,"GPU conversion validation clear refused");std::memset(p,0,4);zero_->Unmap(0,nullptr);return true;
    }
    bool Ready()const{return root_&&decode_&&encode_&&heap_&&flag_&&zero_&&readback_;}
    bool Decode(ID3D12GraphicsCommandList* list,ID3D12Resource* rawColor,ID3D12Resource* rawDepth,unsigned w,unsigned h,unsigned type,bool bgra,
                Ptr<ID3D12Resource>& color,Ptr<ID3D12Resource>& depth,std::string& reason,bool reuse=false,unsigned dw=0,unsigned dh=0){
        if(!dw&&!dh){dw=w;dh=h;}
        const auto n=uint64_t(w)*h;
        if(!Ready()||!Owned(list)||!Owned(rawColor)||!Owned(rawDepth)||!w||!h||w>4096||h>2160||n>160ull*1024*1024/20||
           !dw||!dh||dw>4096||dh>2160||uint64_t(dw)*dh>160ull*1024*1024/20||uint64_t(dw)*h!=uint64_t(w)*dh||
           (type!=16&&type!=24&&type!=32))return No(reason,"GPU conversion shape/owner/depth format refused");
        for(unsigned i=0;i<2;++i){const auto d=(i?rawDepth:rawColor)->GetDesc();const auto bytes=((i?uint64_t(dw)*dh:n)*(i&&type==16?2:4)+3)&~3ull;
            if(d.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||d.Width<bytes||(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))return No(reason,"GPU conversion raw buffer too small");}
        // Reuse is explicit: the caller owns the history lease and must prove
        // all readers complete. Both images must be restored to NP_SHADER.
        if(reuse){
            for(unsigned i=0;i<2;++i){auto* image=i?depth.Get():color.Get();
                if(!Owned(image))return No(reason,"GPU normalized reuse owner refused");
                const auto d=image->GetDesc();
                if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Width!=w||d.Height!=h||d.DepthOrArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||
                   d.Format!=(i?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM)||!(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return No(reason,"GPU normalized reuse shape refused");
            }
            Transition(list,color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Transition(list,depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }else{
        color.Reset();depth.Reset();
        for(unsigned i=0;i<2;++i){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;
            d.Format=i?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(i?&depth:&color))))return No(reason,"GPU normalized texture allocation refused");}
        }
        View(color.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,0);View(depth.Get(),DXGI_FORMAT_R32_FLOAT,1);
        list->CopyBufferRegion(flag_.Get(),0,zero_.Get(),0,4);Transition(list,flag_.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(list,rawColor,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(list,rawDepth,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Bind(list,decode_.Get(),w,h,type,bgra,dw,dh);list->SetComputeRootShaderResourceView(1,rawColor->GetGPUVirtualAddress());list->SetComputeRootShaderResourceView(2,rawDepth->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(4,flag_->GetGPUVirtualAddress());list->Dispatch(((w>dw?w:dw)+7)/8,((h>dh?h:dh)+7)/8,1);
        Transition(list,rawColor,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);Transition(list,rawDepth,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        Transition(list,color.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(list,depth.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,flag_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);list->CopyBufferRegion(readback_.Get(),0,flag_.Get(),0,4);
        Transition(list,flag_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);return true;
    }
    // Caller proves Decode completion before this four-byte validation read.
    bool Valid(std::string& reason){void* p=nullptr;D3D12_RANGE range{0,4};if(FAILED(readback_->Map(0,&range,&p)))return No(reason,"GPU depth validation unavailable");
        uint32_t invalid=0;std::memcpy(&invalid,p,4);D3D12_RANGE none{0,0};readback_->Unmap(0,&none);return !invalid||No(reason,"GPU captured depth outside finite [0,1]");}
    bool Encode(ID3D12GraphicsCommandList* list,ID3D12Resource* color,ID3D12Resource* output,bool bgra,std::string& reason){
        if(!Ready()||!Owned(list)||!Owned(color)||!Owned(output))return No(reason,"GPU packing owner unavailable");
        const auto c=color->GetDesc(),b=output->GetDesc();
        if(c.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||c.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||c.DepthOrArraySize!=1||c.MipLevels!=1||c.SampleDesc.Count!=1||
           !c.Width||!c.Height||c.Width>4096||c.Height>2160||b.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||b.Width<c.Width*c.Height*4||
           !(c.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||!(b.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return No(reason,"GPU packing format/shape refused");
        View(color,DXGI_FORMAT_R8G8B8A8_UNORM,0);View(nullptr,DXGI_FORMAT_R32_FLOAT,1);
        D3D12_RESOURCE_BARRIER u{};u.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;u.UAV.pResource=color;list->ResourceBarrier(1,&u);
        Transition(list,output,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Bind(list,encode_.Get(),UINT(c.Width),c.Height,0,bgra);
        list->SetComputeRootUnorderedAccessView(4,output->GetGPUVirtualAddress());list->Dispatch((UINT(c.Width)+7)/8,(c.Height+7)/8,1);
        Transition(list,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);return true;
    }
};
}
