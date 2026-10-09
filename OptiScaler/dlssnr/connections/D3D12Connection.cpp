#include "GpuBudget.h"
#include "D3D12Connection.h"
#include "../FgLifecycle.h"
#include <d3dcompiler.h>
#include <array>
#include <cstring>
#include <limits>

namespace DlssNr::Connections {
namespace {
bool Refuse(std::string& r,const char* why) { r=why;return false; }
bool Same(IUnknown* a,IUnknown* b) {
    if(!a||!b)return false;ComPtr<IUnknown>x,y;
    return SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x==y;
}
bool OnDevice(ID3D12DeviceChild* r,ID3D12Device* d) {
    ComPtr<ID3D12Device>x;return r&&SUCCEEDED(r->GetDevice(IID_PPV_ARGS(&x)))&&Same(x.Get(),d);
}
bool Completed(ID3D12Fence* f,UINT64 n) {
    if(!f||!n)return false;auto v=f->GetCompletedValue();if(v==UINT64_MAX)return false;if(v>=n)return true;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return false;
    bool ok=SUCCEEDED(f->SetEventOnCompletion(n,event))&&WaitForSingleObject(event,2000)==WAIT_OBJECT_0;
    CloseHandle(event);v=f->GetCompletedValue();return ok&&v!=UINT64_MAX&&v>=n;
}
void Transition(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
    if(a==b)return;D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&x);
}
DXGI_FORMAT DepthView(DXGI_FORMAT f) {
    switch(f){case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_R16_UNORM:return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24G8_TYPELESS:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;default:return DXGI_FORMAT_UNKNOWN;}
}
DXGI_FORMAT DepthRaw(DXGI_FORMAT f) {
    switch(DepthView(f)){case DXGI_FORMAT_R32_FLOAT:return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_R16_UNORM:return DXGI_FORMAT_R16_TYPELESS;
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return DXGI_FORMAT_R24G8_TYPELESS;default:return DXGI_FORMAT_UNKNOWN;}
}
bool Shape(ID3D12Resource* r,UINT w,UINT h) {
    if(!r)return false;auto d=r->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==w&&d.Height==h&&d.DepthOrArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1;
}
bool SdrColor(DXGI_FORMAT f) {
    return f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_B8G8R8A8_UNORM||f==DXGI_FORMAT_R10G10B10A2_UNORM;
}
}
struct D3D12Connection::Impl {
    struct Slot {
        std::shared_ptr<GpuBudget::Reservation> budget;
        ComPtr<ID3D12Resource> color,depth,rawColor,rawDepth,motion,distrust,rawMotion,rawDistrust;
        UINT width=0,height=0;DXGI_FORMAT colorFormat=DXGI_FORMAT_UNKNOWN,depthFormat=DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT motionFormat=DXGI_FORMAT_UNKNOWN,maskFormat=DXGI_FORMAT_UNKNOWN;
    };
    struct Ticket { Impl* owner=nullptr;std::shared_ptr<Slot> slot;D3D12ObservedFrame source;bool live=true; };
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;UINT64 value=0;
    ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pso,copyPso;
    DXGI_FORMAT copyFormat=DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3D12DescriptorHeap> heap,rtvHeap;
    std::array<std::shared_ptr<Slot>,2> slots{};std::shared_ptr<Ticket> active;
    nrpg::CpuMainlineClient::GpuOutput heldOutput;
    std::array<std::uint64_t,11> diagnosedEpisode{};
    bool restart=false;
    explicit Impl(ID3D12Device* d):device(d){}
    bool Quarantine(std::string& reason,const char* boundary,HRESULT operationResult,const char* why) {
        if(!restart) {
            // Record the first failed boundary before the game reaches Present.
            // Fence/device observations localize the failure, not its cause.
            const auto removed=device?device->GetDeviceRemovedReason():E_POINTER;
            NR_FG_EVENT("capture-fault", "boundary={} device={:p} privateQueue={:p} list={:p} "
                "operationResult={} deviceReason={} fenceValue={} fenceCompleted={} "
                "completionProven=false resourceAssociation=unknown",
                boundary,static_cast<void*>(device.Get()),static_cast<void*>(queue.Get()),
                static_cast<void*>(list.Get()),static_cast<uint32_t>(operationResult),
                static_cast<uint32_t>(removed),value,fence?fence->GetCompletedValue():0);
        }
        restart=true;return Refuse(reason,why);
    }
    bool Healthy(std::string& reason,const char* boundary) {
        const auto removed=device?device->GetDeviceRemovedReason():S_OK;
        return SUCCEEDED(removed)||Quarantine(reason,boundary,removed,"capture device removed; restart required");
    }
    void DiagnoseCapture(const D3D12ObservedFrame& f) noexcept {
        if(!FgLifecycle::Enabled())return;
        const auto& d=f.description;auto* target=f.outputTarget?f.outputTarget.Get():f.color.Get();
        const std::array<std::uint64_t,11> episode{d.producer,d.session,d.stream,d.generation,
            d.color.width,d.color.height,std::uint64_t(f.color->GetDesc().Format),
            std::uint64_t(f.depth->GetDesc().Format),std::uint64_t(target->GetDesc().Format),
            f.deriveMotion?1u:0u,d.device};
        if(episode==diagnosedEpisode)return;diagnosedEpisode=episode;
        NR_FG_EVENT("capture-boundary", "device={:p} sourceQueue={:p} privateQueue={:p} list={:p} "
            "producerFence={:p} producerValue={} producerCompleted={} producer={} session={} stream={} "
            "capture={} generation={} sourceLease={:p} deriveMotion={} "
            "association=caller-supplied-same-device-full-raster depthSceneAssociation=unverified stateSource=caller",
            static_cast<void*>(device.Get()),static_cast<void*>(f.sourceQueue.Get()),static_cast<void*>(queue.Get()),
            static_cast<void*>(list.Get()),static_cast<void*>(f.producer.Get()),f.producerValue,
            f.producer?f.producer->GetCompletedValue():0,d.producer,d.session,d.stream,d.capture,d.generation,
            f.lease.get(),f.deriveMotion);
        auto resource=[&](const char* role,ID3D12Resource* image,D3D12_RESOURCE_STATES state){
            const auto desc=image->GetDesc();D3D12_HEAP_PROPERTIES properties{};D3D12_HEAP_FLAGS flags{};
            const auto result=image->GetHeapProperties(&properties,&flags);
            NR_FG_EVENT("capture-resource", "role={} resource={:p} device={:p} raster={}x{} format={} "
                "dimension={} mips={} arraySize={} samples={} resourceFlags={} callerState={} "
                "heapPropertiesKnown={} heapPropertiesResult={} heapType={} heapFlags={} "
                "creationNodeMask={} visibleNodeMask={} sharedAccessRights=unobserved stateSource=caller",
                role,static_cast<void*>(image),static_cast<void*>(device.Get()),desc.Width,desc.Height,
                unsigned(desc.Format),unsigned(desc.Dimension),desc.MipLevels,desc.DepthOrArraySize,
                desc.SampleDesc.Count,unsigned(desc.Flags),unsigned(state),SUCCEEDED(result),
                static_cast<uint32_t>(result),unsigned(properties.Type),unsigned(flags),
                properties.CreationNodeMask,properties.VisibleNodeMask);
        };
        resource("color",f.color.Get(),f.colorState);resource("depth",f.depth.Get(),f.depthState);
        resource("output",target,f.outputTarget?f.outputState:f.colorState);
    }
    bool Initialize(std::string& reason) {
        if(queue)return true;if(!device)return Refuse(reason,"missing renderer device");
        D3D12_COMMAND_QUEUE_DESC q{};
        if(FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)))||FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||
           FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))||FAILED(list->Close())||
           FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))) { queue.Reset();return Refuse(reason,"capture command resources unavailable"); }
        if(FgLifecycle::Enabled()&&FgLifecycle::OptIn("NEUROTIC_DRED")) {
            queue->SetName(L"NeuRotic D3D12 guide private queue");
            list->SetName(L"NeuRotic D3D12 guide capture/normalize/copyback list");
            fence->SetName(L"NeuRotic D3D12 guide capture/copyback completion fence");
        }
        static constexpr char shader[]=R"(
Texture2D<float4> inputColor:register(t0); Texture2D<float> inputDepth:register(t1);
Texture2D<float2> inputMotion:register(t2); Texture2D<float> inputMask:register(t3);
RWTexture2D<float4> outputColor:register(u0); RWTexture2D<float> outputDepth:register(u1);
RWTexture2D<float2> outputMotion:register(u2); RWTexture2D<float> outputMask:register(u3);
cbuffer Mode:register(b0) { uint mode; }
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) { uint w,h; outputDepth.GetDimensions(w,h);
if(p.x<w && p.y<h) { outputColor[p.xy]=inputColor.Load(int3(p.xy,0)); outputDepth[p.xy]=inputDepth.Load(int3(p.xy,0));
if(mode&1) { float2 m=inputMotion.Load(int3(p.xy,0)); if(mode&2)m.y=-m.y; outputMotion[p.xy]=m;outputMask[p.xy]=inputMask.Load(int3(p.xy,0)); } } })";
        ComPtr<ID3DBlob> cs,rs,error;D3D12_DESCRIPTOR_RANGE ranges[2]{{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,4,0,0,4}};
        D3D12_ROOT_PARAMETER param[2]{};param[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;param[0].DescriptorTable={2,ranges};
        param[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;param[1].Constants={0,0,1};
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=param;
        if(FAILED(D3DCompile(shader,sizeof(shader),"canonical-capture",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&cs,&error))||
            FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&rs,&error))||
            FAILED(device->CreateRootSignature(0,rs->GetBufferPointer(),rs->GetBufferSize(),IID_PPV_ARGS(&root)))) {queue.Reset();return Refuse(reason,"capture normalization shader unavailable");}
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=8;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(FAILED(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)))||FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)))) {queue.Reset();return Refuse(reason,"capture shader resources unavailable");}
        return true;
    }
    bool Texture(ComPtr<ID3D12Resource>& r,UINT w,UINT h,DXGI_FORMAT f,bool uav) {
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=f;
        desc.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        return SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,uav?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));
    }
    bool Begin(std::string& r) { if(!Healthy(r,"command-reset"))return false;return SUCCEEDED(allocator->Reset())&&SUCCEEDED(list->Reset(allocator.Get(),nullptr))?true:Refuse(r,"capture command reset failed"); }
    bool CopyPipeline(DXGI_FORMAT format,std::string& r) {
        if(copyPso&&copyFormat==format)return true;
        copyPso.Reset();copyFormat=DXGI_FORMAT_UNKNOWN;
        static constexpr char shader[]=R"(
Texture2D<float4> image:register(t0);
float4 vs(uint id:SV_VertexID):SV_Position { return float4(id==2?3:-1,id==1?3:-1,0,1); }
float4 ps(float4 p:SV_Position):SV_Target { return image.Load(int3(p.xy,0)); })";
        ComPtr<ID3DBlob> vs,ps,error;
        if(FAILED(D3DCompile(shader,sizeof(shader),"copyback",nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&error))||
           FAILED(D3DCompile(shader,sizeof(shader),"copyback",nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&error)))return Refuse(r,"color conversion copyback shader unavailable");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
        pd.DepthStencilState.DepthEnable=FALSE;pd.DepthStencilState.StencilEnable=FALSE;pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=format;pd.SampleDesc.Count=1;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
        if(FAILED(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&copyPso)))||(!rtvHeap&&FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvHeap))))) {copyPso.Reset();return Refuse(r,"color conversion copyback render target unavailable");}copyFormat=format;return true;
    }
    bool End(std::string& r,const char* boundary) {
        if(FAILED(list->Close()))return Refuse(r,"capture command close failed");
        if(!Healthy(r,boundary))return false;
        ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);
        const auto signal=queue->Signal(fence.Get(),++value);
        if(FAILED(signal)||!Completed(fence.Get(),value))return Quarantine(r,boundary,signal,"capture completion unknown; restart required");
        return Healthy(r,boundary);
    }
    std::shared_ptr<Ticket> Find(PendingFrame& p) {
        return active&&p.ownerLease.get()==active.get()&&active->live?active:nullptr;
    }
};
D3D12Connection::D3D12Connection(ID3D12Device* d):impl_(std::make_unique<Impl>(d)){}
D3D12Connection::~D3D12Connection(){if(impl_->restart)(void)impl_.release();}
bool D3D12Connection::RequiresRestart()const{return impl_->restart;}
bool D3D12Connection::Capture(const D3D12ObservedFrame& f,PendingFrame& out,std::string& r) {
    auto& i=*impl_;if(i.restart)return Refuse(r,"capture quarantined; restart required");
    if(!i.Healthy(r,"capture-entry"))return false;
    if(i.active||out.ownerLease)return Refuse(r,"capture already leased");
    const auto& d=f.description;auto target=f.outputTarget?f.outputTarget:f.color;
    const UINT w=d.color.width,h=d.color.height;
    if(!GpuBudget::AdmitRaster(w,h,f.deriveMotion))return Refuse(r,"capture raster exceeds bounded GPU admission");
    if(!f.statesKnown||!f.lease||!d.device||!d.generation||!d.capture||!w||!h||d.color.allocationWidth!=w||d.color.allocationHeight!=h||d.color.x||d.color.y||d.depth!=d.color||
        d.colorDomain!=Neurotic::Contracts::ColorDomain::EncodedDisplay||d.rasterOrigin!=Neurotic::Contracts::RasterOrigin::TopLeft||
        !Shape(f.color.Get(),w,h)||!Shape(f.depth.Get(),w,h)||!Shape(target.Get(),w,h))return Refuse(r,"capture requires leased full-raster SDR color/depth");
    if(!OnDevice(f.color.Get(),i.device.Get())||!OnDevice(f.depth.Get(),i.device.Get())||!OnDevice(target.Get(),i.device.Get())||
        !OnDevice(f.sourceQueue.Get(),i.device.Get())||f.sourceQueue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return Refuse(r,"capture requires same logical device and direct source queue");
    auto cf=f.color->GetDesc().Format,df=f.depth->GetDesc().Format;
    DXGI_FORMAT mf=DXGI_FORMAT_UNKNOWN,kf=DXGI_FORMAT_UNKNOWN;
    if(!f.deriveMotion){
        if(!Shape(f.motion.Get(),w,h)||!Shape(f.distrust.Get(),w,h)||!OnDevice(f.motion.Get(),i.device.Get())||!OnDevice(f.distrust.Get(),i.device.Get())||
           d.motion!=d.color||d.distrust!=d.color||d.motionGridWidth!=1||d.motionGridHeight!=1||d.scaleX!=1||d.scaleY!=1||
           d.motionUnits!=Neurotic::Contracts::MotionUnits::Pixels||d.motionDirection!=Neurotic::Contracts::MotionDirection::CurrentToPrevious||
           !(d.flags&Neurotic::Feed::Prepared::HasDistrust)||d.maskCapture!=d.capture)return Refuse(r,"supplied guide raster or semantics unavailable");
        mf=f.motion->GetDesc().Format;kf=f.distrust->GetDesc().Format;
        if((mf!=DXGI_FORMAT_R16G16_FLOAT&&mf!=DXGI_FORMAT_R32G32_FLOAT)||(kf!=DXGI_FORMAT_R8_UNORM&&kf!=DXGI_FORMAT_R32_FLOAT))return Refuse(r,"unsupported supplied guide formats");
    }
    auto tf=target->GetDesc().Format;
    if(!SdrColor(cf)||DepthView(df)==DXGI_FORMAT_UNKNOWN||!SdrColor(tf)||
       (tf!=DXGI_FORMAT_R8G8B8A8_UNORM&&!(target->GetDesc().Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)))
        return Refuse(r,"unsupported capture/copyback format");
    if(f.producer&&(!OnDevice(f.producer.Get(),i.device.Get())||!Completed(f.producer.Get(),f.producerValue))) {
        if(!i.Healthy(r,"producer-completion"))return false;
        return Refuse(r,"producer dependency not completed");
    }
    if(!i.Initialize(r)||(tf!=DXGI_FORMAT_R8G8B8A8_UNORM&&!i.CopyPipeline(tf,r)))return false;
    std::shared_ptr<Impl::Slot>* available=nullptr;
    for(auto& s:i.slots)if(!s||s.use_count()==1){available=&s;break;}
    if(!available)return Refuse(r,"both canonical pairs still leased by readers");
    if(!*available)*available=std::make_shared<Impl::Slot>();auto s=*available;
    if(s->width!=w||s->height!=h||s->colorFormat!=cf||s->depthFormat!=df||s->motionFormat!=mf||s->maskFormat!=kf) {
        auto fresh=std::make_shared<Impl::Slot>();fresh->width=w;fresh->height=h;fresh->colorFormat=cf;fresh->depthFormat=df;
        fresh->motionFormat=mf;fresh->maskFormat=kf;
        std::uint64_t bytes=0;
        for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R8_UNORM}){auto n=GpuBudget::ImageBytes(i.device.Get(),w,h,format,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);if(!n)return Refuse(r,"canonical allocation size unavailable");bytes+=n;}
        for(auto format:{cf,DepthRaw(df),mf,kf})if(format!=DXGI_FORMAT_UNKNOWN){auto n=GpuBudget::ImageBytes(i.device.Get(),w,h,format);if(!n)return Refuse(r,"raw allocation size unavailable");bytes+=n;}
        fresh->budget=GpuBudget::Reserve(bytes);if(!fresh->budget)return Refuse(r,"aggregate GPU allocation budget exceeded");
        if(!i.Texture(fresh->color,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,true)||!i.Texture(fresh->depth,w,h,DXGI_FORMAT_R32_FLOAT,true)||
           !i.Texture(fresh->motion,w,h,DXGI_FORMAT_R16G16_FLOAT,true)||!i.Texture(fresh->distrust,w,h,DXGI_FORMAT_R8_UNORM,true)||
           !i.Texture(fresh->rawColor,w,h,cf,false)||!i.Texture(fresh->rawDepth,w,h,DepthRaw(df),false)||
           (!f.deriveMotion&&(!i.Texture(fresh->rawMotion,w,h,mf,false)||!i.Texture(fresh->rawDistrust,w,h,kf,false))))return Refuse(r,"canonical texture allocation unavailable");
        *available=fresh;s=fresh;
    }
    auto t=std::make_shared<Impl::Ticket>();t->owner=&i;t->slot=s;t->source=f;i.active=t;
    i.DiagnoseCapture(f);
    // Signal on the observed queue before any private work, avoiding a cyclic dependency.
    const auto sourceSignal=f.sourceQueue->Signal(i.fence.Get(),++i.value);
    if(FAILED(sourceSignal)||!Completed(i.fence.Get(),i.value))return i.Quarantine(r,"source-completion",sourceSignal,"source queue completion unknown; restart required");
    if(!i.Begin(r)){if(!i.restart)i.active.reset();return false;}
    ID3D12Resource* sources[]{f.color.Get(),f.depth.Get(),f.motion.Get(),f.distrust.Get()};
    ID3D12Resource* raw[]{s->rawColor.Get(),s->rawDepth.Get(),s->rawMotion.Get(),s->rawDistrust.Get()};
    ID3D12Resource* canonical[]{s->color.Get(),s->depth.Get(),s->motion.Get(),s->distrust.Get()};
    D3D12_RESOURCE_STATES states[]{f.colorState,f.depthState,f.motionState,f.distrustState};
    DXGI_FORMAT formats[]{cf,DepthView(df),f.deriveMotion?DXGI_FORMAT_R16G16_FLOAT:mf,f.deriveMotion?DXGI_FORMAT_R8_UNORM:kf};
    for(UINT n=0;n<(f.deriveMotion?2u:4u);++n) {
        Transition(i.list.Get(),sources[n],states[n],D3D12_RESOURCE_STATE_COPY_SOURCE);i.list->CopyResource(raw[n],sources[n]);
        Transition(i.list.Get(),sources[n],D3D12_RESOURCE_STATE_COPY_SOURCE,states[n]);
        Transition(i.list.Get(),raw[n],D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    auto cpu=i.heap->GetCPUDescriptorHandleForHeapStart();const UINT step=i.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for(UINT n=0;n<4;++n){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=formats[n];v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        i.device->CreateShaderResourceView(raw[n],&v,cpu);cpu.ptr+=step;}
    for(auto* resource:canonical){Transition(i.list.Get(),resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=resource->GetDesc().Format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;i.device->CreateUnorderedAccessView(resource,nullptr,&v,cpu);cpu.ptr+=step;}
    ID3D12DescriptorHeap* heaps[]{i.heap.Get()};i.list->SetDescriptorHeaps(1,heaps);i.list->SetComputeRootSignature(i.root.Get());i.list->SetPipelineState(i.pso.Get());
    i.list->SetComputeRootDescriptorTable(0,i.heap->GetGPUDescriptorHandleForHeapStart());i.list->SetComputeRoot32BitConstant(1,(f.deriveMotion?0u:1u)|(f.invertMotionY?2u:0u),0);i.list->Dispatch((w+7)/8,(h+7)/8,1);
    for(auto* resource:canonical)Transition(i.list.Get(),resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for(auto* resource:raw)if(resource)Transition(i.list.Get(),resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    if(!i.End(r,"capture-normalize")){if(!i.restart)i.active.reset();return false;}
    out.textures.description=d;auto luid=i.device->GetAdapterLuid();std::memcpy(&out.textures.adapterLuid,&luid,sizeof(luid));
    auto& n=out.textures.normalized;n.device=i.device;n.color=s->color;n.depth=s->depth;n.producer=i.fence;n.value=i.value;n.lease=s;
    if(!f.deriveMotion){n.motion=s->motion;n.distrust=s->distrust;}
    out.textures.deriveMotion=f.deriveMotion;out.ownerLease=t;r.clear();return true;
}
bool D3D12Connection::Finish(PendingFrame& p,nrpg::CpuMainlineClient::GpuOutput& output,CopybackReceipt& receipt,std::string& r) {
    receipt={};auto& i=*impl_;auto t=i.Find(p);
    if(i.restart||!t)return Refuse(r,"invalid, replayed or quarantined copyback ticket");
    if(!i.Healthy(r,"copyback-entry"))return false;
    auto target=t->source.outputTarget?t->source.outputTarget:t->source.color;
    if(!output.lease||!OnDevice(output.color.Get(),i.device.Get())||!OnDevice(output.completion.fence.Get(),i.device.Get())||
       !Shape(output.color.Get(),t->slot->width,t->slot->height)||output.color->GetDesc().Format!=DXGI_FORMAT_R8G8B8A8_UNORM||
       !Completed(output.completion.fence.Get(),output.completion.value)) {
        if(!i.Healthy(r,"output-completion"))return false;
        return Refuse(r,"output is not a completed matching renderer texture");
    }
    if(!i.Begin(r))return false;i.heldOutput=output;
    auto state=t->source.outputTarget?t->source.outputState:t->source.colorState;
    if(target->GetDesc().Format!=DXGI_FORMAT_R8G8B8A8_UNORM){
        Transition(i.list.Get(),output.color.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Transition(i.list.Get(),target.Get(),state,D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        i.device->CreateShaderResourceView(output.color.Get(),&srv,i.heap->GetCPUDescriptorHandleForHeapStart());
        auto rtv=i.rtvHeap->GetCPUDescriptorHandleForHeapStart();i.device->CreateRenderTargetView(target.Get(),nullptr,rtv);
        ID3D12DescriptorHeap* heaps[]{i.heap.Get()};i.list->SetDescriptorHeaps(1,heaps);i.list->SetGraphicsRootSignature(i.root.Get());i.list->SetPipelineState(i.copyPso.Get());
        i.list->SetGraphicsRootDescriptorTable(0,i.heap->GetGPUDescriptorHandleForHeapStart());i.list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
        D3D12_VIEWPORT vp{0,0,float(t->slot->width),float(t->slot->height),0,1};D3D12_RECT scissor{0,0,LONG(t->slot->width),LONG(t->slot->height)};
        i.list->RSSetViewports(1,&vp);i.list->RSSetScissorRects(1,&scissor);i.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);i.list->DrawInstanced(3,1,0,0);
        Transition(i.list.Get(),target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,state);
        Transition(i.list.Get(),output.color.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }else{
        Transition(i.list.Get(),output.color.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(i.list.Get(),target.Get(),state,D3D12_RESOURCE_STATE_COPY_DEST);i.list->CopyResource(target.Get(),output.color.Get());
        Transition(i.list.Get(),target.Get(),D3D12_RESOURCE_STATE_COPY_DEST,state);
        Transition(i.list.Get(),output.color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if(!i.End(r,"copyback")){if(!i.restart)i.heldOutput={};return false;}
    receipt={t->source.description.capture,t->source.description.generation,true};t->live=false;t->source={};i.active.reset();p={};output={};i.heldOutput={};r.clear();return true;
}
bool D3D12Connection::Cancel(PendingFrame& p,std::string& r) {
    auto& i=*impl_;auto t=i.Find(p);if(i.restart||!t)return Refuse(r,"invalid or quarantined cancellation");
    t->live=false;t->source={};i.active.reset();p={};r.clear();return true;
}
}
