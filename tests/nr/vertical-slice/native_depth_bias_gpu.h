// Public WARP oracle. Included after native_recording_warp_scene.h helpers.
// No private provider/model or game; unsupported v9/dynamic-bias is reported.
static void DepthBiasGpuRegressions()
{
    ComPtr<ID3D12Debug> debug;
    const bool validated=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    if(validated)debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    Ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));Ok(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Ok(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 support{};
    if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16,&support,sizeof(support)))||!support.DynamicDepthBiasSupported){
        std::cout<<"UNTESTED depth-bias GPU oracle: public WARP dynamic-bias capability unavailable\n";return;
    }
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    Ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12GraphicsCommandList9> version9;
    if(FAILED(list.As(&version9))){
        std::cout<<"UNTESTED depth-bias GPU oracle: public WARP command-list9 unavailable\n";return;
    }
    Ok(list->Close());
    D3D12_ROOT_SIGNATURE_DESC rd{};ComPtr<ID3DBlob> serialized,error;
    Ok(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error));
    ComPtr<ID3D12RootSignature> root;
    Ok(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root)));
    auto vs=Compile("float4 main(uint id:SV_VertexID):SV_Position {float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[id],0.5,1);}","vs_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd{};gd.pRootSignature=root.Get();gd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
    gd.SampleMask=UINT_MAX;gd.SampleDesc.Count=1;gd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;gd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    gd.RasterizerState.DepthClipEnable=TRUE;gd.RasterizerState.DepthBias=1;
    gd.DepthStencilState.DepthEnable=TRUE;gd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    gd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    gd.DepthStencilState.StencilReadMask=gd.DepthStencilState.StencilWriteMask=255;
    gd.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    gd.DepthStencilState.BackFace=gd.DepthStencilState.FrontFace;gd.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    gd.Flags=D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS;
    ComPtr<ID3D12PipelineState> graphics,inserted,compute;
    Ok(device->CreateGraphicsPipelineState(&gd,IID_PPV_ARGS(&graphics)));
    gd.RasterizerState.DepthBias=0;Ok(device->CreateGraphicsPipelineState(&gd,IID_PPV_ARGS(&inserted)));
    auto cs=Compile("[numthreads(1,1,1)]void main(){}","cs_5_0");
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd{};cd.pRootSignature=root.Get();cd.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
    Ok(device->CreateComputePipelineState(&cd,IID_PPV_ARGS(&compute)));
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC depthDesc{};depthDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width=depthDesc.Height=4;depthDesc.DepthOrArraySize=depthDesc.MipLevels=1;
    depthDesc.Format=DXGI_FORMAT_D32_FLOAT;depthDesc.SampleDesc.Count=1;depthDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    ComPtr<ID3D12Resource> depth,readback;
    Ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&depthDesc,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&depth)));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;
    device->GetCopyableFootprints(&depthDesc,0,1,0,&footprint,&rows,nullptr,nullptr);
    heap.Type=D3D12_HEAP_TYPE_READBACK;auto buffer=Buffer(footprint.Offset+UINT64(footprint.Footprint.RowPitch)*rows);
    Ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> dsv;Ok(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv)));
    D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
    const auto handle=dsv->GetCPUDescriptorHandleForHeapStart();device->CreateDepthStencilView(depth.Get(),&view,handle);
    D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;
    Ok(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));Ok(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    struct Event {HANDLE value=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Event(){if(value)CloseHandle(value);}} event;
    assert(event.value);UINT64 completed=0;
    const auto render=[&](unsigned scenario){
        Ok(allocator->Reset());Ok(list->Reset(allocator.Get(),graphics.Get()));
        NativeRecordingState state;assert(state.Begin(list.Get(),1,graphics.Get(),true));
        const auto bind=[&](ID3D12PipelineState* p){list->SetPipelineState(p);assert(state.ObservePipeline(1,p));};
        const auto bias=[&](float value){version9->RSSetDepthBias(value,0.0f,0.0f);assert(state.ObserveDepthBias(1,value,0.0f,0.0f));};
        list->SetGraphicsRootSignature(root.Get());
        if(scenario!=2)bias(64.0f);
        if(scenario==3)bind(graphics.Get());
        if(scenario==4)bind(compute.Get());
        if(scenario==1||scenario==3||scenario==4){
            NativeRecordingState::OriginalSetters original;
            original.pipeline=[&](auto* target,auto* p){assert(target==list.Get());target->SetPipelineState(p);};
            if(scenario==1)original.depthBias=[&](auto* target,float a,float b,float c){assert(target==list.Get());version9->RSSetDepthBias(a,b,c);};
            auto saved=state.CaptureRestorable(list.Get(),1,RestoreMask::Pipeline,original);assert(saved);
            bind(inserted.Get());bias(8.0f);assert(state.Restore(*saved,original));
        }
        if(scenario==4)bind(graphics.Get()); // Valid raster continuation defines its own state.
        D3D12_VIEWPORT viewport{0,0,4,4,0,1};D3D12_RECT scissor{0,0,4,4};
        list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->OMSetRenderTargets(0,nullptr,FALSE,&handle);
        list->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,1.0f,0,0,nullptr);list->DrawInstanced(3,1,0,0);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={depth.Get(),0,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=depth.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
        list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
        Ok(list->Close());ID3D12CommandList* submitted[]={list.Get()};queue->ExecuteCommandLists(1,submitted);
        Ok(queue->Signal(fence.Get(),++completed));Ok(fence->SetEventOnCompletion(completed,event.value));
        assert(WaitForSingleObject(event.value,5000)==WAIT_OBJECT_0);
        void* bytes=nullptr;Ok(readback->Map(0,nullptr,&bytes));std::array<float,16> pixels{};
        for(UINT y=0;y<4;++y)for(UINT x=0;x<4;++x)
            std::memcpy(&pixels[y*4+x],static_cast<const char*>(bytes)+footprint.Offset+UINT64(y)*footprint.Footprint.RowPitch+x*4,4);
        readback->Unmap(0,nullptr);return pixels;
    };
    const auto dynamic=render(0),restored=render(1),pipeline=render(2),rebound=render(3),computeContinuation=render(4);
    assert(dynamic==restored&&pipeline==rebound&&pipeline==computeContinuation&&dynamic!=pipeline);
    if(validated){
        ComPtr<ID3D12InfoQueue> info;Ok(device.As(&info));
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i){
            SIZE_T size=0;info->GetMessage(i,nullptr,&size);std::vector<char> bytes(size);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Ok(info->GetMessage(i,message,&size));
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::cerr<<message->pDescription<<'\n';assert(false);}
        }
    }
    std::cout<<"PASS actual WARP v9 depth-bias pixels: dynamic restore, same-PSO reset, compute-to-graphics continuation; debug="<<validated<<'\n';
}
