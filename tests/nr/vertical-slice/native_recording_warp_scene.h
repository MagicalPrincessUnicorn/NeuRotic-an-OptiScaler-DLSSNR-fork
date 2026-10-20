// Shared WARP compute/graphics oracle for the state model and actual production detours.
#include <source_location>
static void Ok(HRESULT result, const std::source_location location = std::source_location::current()) { if (FAILED(result)) { std::cerr << "WARP line=" << location.line() << " HRESULT=" << std::hex << result << std::dec << "\n"; throw std::runtime_error("WARP API failed"); } }
static ComPtr<ID3DBlob> Compile(const char* source, const char* target)
{
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
                                   "main", target, 0, 0, &code, &errors);
    if (FAILED(hr)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "shader compile failed");
    return code;
}
static D3D12_RESOURCE_DESC Buffer(UINT64 bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes; d.Height = 1;
    d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
    return d;
}
static std::array<unsigned char, 4> WarpOracle(bool perturb, bool adapter = false
#ifdef NATIVE_INSTALLED_HOOK_TEST
    , bool installedHooks = false, bool computeOnly = false
#endif
    , bool invalidatedTables = false
)
{
    ComPtr<ID3D12Debug> debug;
    Ok(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter> warp; ComPtr<ID3D12Device> device;
    Ok(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    Ok(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Ok(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue; Ok(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;
    Ok(device->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&allocator)));
    D3D12_ROOT_PARAMETER params[2]{};
    D3D12_DESCRIPTOR_RANGE range{}; range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1; range.BaseShaderRegister = 0;
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1; params[0].DescriptorTable.pDescriptorRanges = &range;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.Num32BitValues = 1; params[1].Constants.ShaderRegister = 0;
    D3D12_ROOT_SIGNATURE_DESC rd{}; rd.NumParameters = 2; rd.pParameters = params;
    ComPtr<ID3DBlob> blob, error;
    Ok(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
    ComPtr<ID3D12RootSignature> computeRoot;
    Ok(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&computeRoot)));
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Ok(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
    ComPtr<ID3D12RootSignature> graphicsRoot;
    Ok(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&graphicsRoot)));
    auto cs = Compile("RWBuffer<uint> dst:register(u0); cbuffer C:register(b0){uint value;} [numthreads(1,1,1)] void main(){dst[0]=value;}", "cs_5_0");
    auto vs = Compile("float4 main(uint id:SV_VertexID):SV_Position { float2 p=float2((id << 1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),0,1);}", "vs_5_0");
    auto ps = Compile("Buffer<uint> src:register(t0); cbuffer C:register(b0){uint extra;} float4 main():SV_Target{return float4((src[0]+extra)/255.0,0.5,0,1);}", "ps_5_0");
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd{}; cd.pRootSignature = computeRoot.Get();
    cd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    ComPtr<ID3D12PipelineState> computePso; Ok(device->CreateComputePipelineState(&cd, IID_PPV_ARGS(&computePso)));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd{}; gd.pRootSignature = graphicsRoot.Get();
    gd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()}; gd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    gd.SampleMask = UINT_MAX; gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    gd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; gd.RasterizerState.DepthClipEnable = TRUE;
    gd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    gd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gd.NumRenderTargets = 1; gd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; gd.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> graphicsPso; Ok(device->CreateGraphicsPipelineState(&gd, IID_PPV_ARGS(&graphicsPso)));
    ComPtr<ID3D12GraphicsCommandList> list;
    Ok(device->CreateCommandList(0, qd.Type, allocator.Get(), computePso.Get(), IID_PPV_ARGS(&list)));
    D3D12_HEAP_PROPERTIES defaultHeap{}; defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES readbackHeap{}; readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    auto bd = Buffer(4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> data;
    Ok(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&data)));
    D3D12_RESOURCE_DESC td{}; td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = 1; td.Height = 1; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{}; clear.Format = td.Format;
    ComPtr<ID3D12Resource> target;
    Ok(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &td,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&target)));
    // Query the production HUD state proof against a real single-subresource resource.
    NativeRecordingState hudState;
    assert(hudState.Begin(list.Get(),301));
    assert(!hudState.KnownSingleSubresourceState(301,target.Get()));
    assert(hudState.WatchResource(301,target.Get(),1,0,D3D12_RESOURCE_STATE_RENDER_TARGET));
    assert(hudState.KnownSingleSubresourceState(301,target.Get())==D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_RESOURCE_BARRIER hudTransition{};hudTransition.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    const auto combined=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    hudTransition.Transition={target.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,combined};
    assert(hudState.ObserveBarriers(301,1,&hudTransition));
    assert(hudState.KnownSingleSubresourceState(301,target.Get())==combined);
    assert(!hudState.KnownSingleSubresourceState(302,target.Get()));
    hudTransition.Flags=D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
    hudTransition.Transition={target.Get(),0,combined,D3D12_RESOURCE_STATE_COPY_SOURCE};
    assert(!hudState.ObserveBarriers(301,1,&hudTransition));
    assert(!hudState.KnownSingleSubresourceState(301,target.Get()));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT rows = 0; UINT64 rowBytes = 0, totalBytes = 0;
    device->GetCopyableFootprints(&td, 0, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);
    auto rb = Buffer(totalBytes); ComPtr<ID3D12Resource> readback;
    Ok(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &rb,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 2; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap, alien;
    Ok(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)));
    Ok(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&alien)));
    const UINT step = device->GetDescriptorHandleIncrementSize(hd.Type);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; uav.Buffer.NumElements = 1;
    device->CreateUnorderedAccessView(data.Get(), nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = DXGI_FORMAT_R32_UINT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = 1;
    auto cpuSrv = heap->GetCPUDescriptorHandleForHeapStart(); cpuSrv.ptr += step;
    device->CreateShaderResourceView(data.Get(), &srv, cpuSrv);
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{}; rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rtvDesc.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> rtv; Ok(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtv)));
    device->CreateRenderTargetView(target.Get(), nullptr, rtv->GetCPUDescriptorHandleForHeapStart());
    NativeRecordingState state;
#ifdef NATIVE_INSTALLED_HOOK_TEST
    NativeRecordingObserver& observer = TestObserver();
    NativeRecordingHooks& hooks = TestHooks();
#else
    NativeRecordingObserver observer;
#endif
#ifdef NATIVE_INSTALLED_HOOK_TEST
    if (installedHooks)
    {
        assert(hooks.Install(list.Get()));
        assert(!observer.Observe(list.Get()).completeCoverage);
        // Register exactly the same serialized root inputs as production creation.
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        Ok(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
        assert(hooks.RegisterRootSignature(computeRoot.Get(), blob->GetBufferPointer(), blob->GetBufferSize(), D3D12CreateVersionedRootSignatureDeserializer));
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Ok(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
        assert(hooks.RegisterRootSignature(graphicsRoot.Get(), blob->GetBufferPointer(), blob->GetBufferSize(), D3D12CreateVersionedRootSignatureDeserializer));
        Ok(list->Close()); Ok(list->Reset(allocator.Get(), computePso.Get()));
        assert(observer.Observe(list.Get()).completeCoverage);
    }
#endif
    ID3D12DescriptorHeap* bound[] = {heap.Get()};
    list->SetDescriptorHeaps(1, bound);
    list->SetComputeRootSignature(computeRoot.Get());
#ifdef NATIVE_INSTALLED_HOOK_TEST
    if (!computeOnly)
#endif
    list->SetGraphicsRootSignature(graphicsRoot.Get());
    auto computeTable = heap->GetGPUDescriptorHandleForHeapStart();
    auto graphicsTable = computeTable; graphicsTable.ptr += step;
    list->SetComputeRootDescriptorTable(0, computeTable);
#ifdef NATIVE_INSTALLED_HOOK_TEST
    if (!computeOnly)
#endif
    list->SetGraphicsRootDescriptorTable(0, graphicsTable);
    list->SetComputeRoot32BitConstant(1, 64, 0);
#ifdef NATIVE_INSTALLED_HOOK_TEST
    if (!computeOnly)
#endif
    list->SetGraphicsRoot32BitConstant(1, 1, 0);
    if (adapter
#ifdef NATIVE_INSTALLED_HOOK_TEST
        && !installedHooks
#endif
    )
    {
        assert(observer.RegisterLayout(computeRoot.Get(), {{RootKind::Table, 0}, {RootKind::Constants, 1}}));
        assert(observer.RegisterLayout(graphicsRoot.Get(), {{RootKind::Table, 0}, {RootKind::Constants, 1}}));
        // This controlled fixture feeds every setter it issues through the adapter.
        observer.SetHookCoverage(true);
        assert(observer.Reset(list.Get(), computePso.Get()));
    }
    else
#ifdef NATIVE_INSTALLED_HOOK_TEST
    if (!installedHooks)
#endif
    {
        assert(state.RegisterLayout(computeRoot.Get(), {{RootKind::Table, 0}, {RootKind::Constants, 1}}));
        assert(state.RegisterLayout(graphicsRoot.Get(), {{RootKind::Table, 0}, {RootKind::Constants, 1}}));
        assert(state.Begin(list.Get(), 1, computePso.Get()));
    }
    auto observe = [&](auto&& action, ID3D12RootSignature* signature = nullptr) {
#ifdef NATIVE_INSTALLED_HOOK_TEST
        if (installedHooks) return true; // Actual calls above, exclusively through installed detours.
#endif
        if (!adapter) return action(state, 1);
        return signature ? observer.WithRootLayout(list.Get(), signature, action) : observer.WithCurrent(list.Get(), action);
    };
    assert(observe([&](auto& s, auto i) { return s.ObserveDescriptorHeaps(i, 1, bound); }));
    assert(observe([&](auto& s, auto i) { return s.ObserveComputeRootSignature(i, computeRoot.Get()); }, computeRoot.Get()));
    assert(observe([&](auto& s, auto i) { return s.ObserveGraphicsRootSignature(i, graphicsRoot.Get()); }, graphicsRoot.Get()));
    assert(observe([&](auto& s, auto i) { return s.ObserveComputeTable(i, 0, computeTable); }));
    assert(observe([&](auto& s, auto i) { return s.ObserveGraphicsTable(i, 0, graphicsTable); }));
    const UINT sixtyFour = 64, one = 1;
    assert(observe([&](auto& s, auto i) { return s.ObserveComputeConstants(i, 1, 1, &sixtyFour, 0); }));
    assert(observe([&](auto& s, auto i) { return s.ObserveGraphicsConstants(i, 1, 1, &one, 0); }));
#ifdef NATIVE_INSTALLED_HOOK_TEST
    if (installedHooks)
    {
        // Exercise redundant binding calls before capture through production
        // detours, then verify restoration with the existing GPU pixel oracle.
        list->SetDescriptorHeaps(1, bound);
        list->SetComputeRootSignature(computeRoot.Get());
        if (!computeOnly) list->SetGraphicsRootSignature(graphicsRoot.Get());
    }
#endif
    if (invalidatedTables)
    {
        // Actual heap switches invalidate compute and graphics tables. Rebind
        // graphics only: restoration must preserve it and leave compute undefined.
        ID3D12DescriptorHeap* other[] = {alien.Get()};
        list->SetDescriptorHeaps(1, other);
        assert(observe([&](auto& s, auto i) { return s.ObserveDescriptorHeaps(i, 1, other); }));
        list->SetDescriptorHeaps(1, bound);
        assert(observe([&](auto& s, auto i) { return s.ObserveDescriptorHeaps(i, 1, bound); }));
#ifdef NATIVE_INSTALLED_HOOK_TEST
        if (!computeOnly)
#endif
        {
            list->SetGraphicsRootDescriptorTable(0, graphicsTable);
            assert(observe([&](auto& s, auto i) { return s.ObserveGraphicsTable(i, 0, graphicsTable); }));
        }
    }
    if (perturb)
    {
        NativeRecordingState::OriginalSetters o;
        o.heaps = [](auto* l, UINT n, auto* const* h) { l->SetDescriptorHeaps(n, h); };
        o.pipeline = [](auto* l, auto* p) { l->SetPipelineState(p); };
        o.computeSignature = [](auto* l, auto* p) { l->SetComputeRootSignature(p); };
        o.graphicsSignature = [](auto* l, auto* p) { l->SetGraphicsRootSignature(p); };
        o.computeTable = [](auto* l, UINT i, auto h) { l->SetComputeRootDescriptorTable(i, h); };
        o.graphicsTable = [](auto* l, UINT i, auto h) { l->SetGraphicsRootDescriptorTable(i, h); };
        o.computeConstants = [](auto* l, UINT i, UINT n, const void* p, UINT d) { l->SetComputeRoot32BitConstants(i, n, p, d); };
        o.graphicsConstants = [](auto* l, UINT i, UINT n, const void* p, UINT d) { l->SetGraphicsRoot32BitConstants(i, n, p, d); };
#ifdef NATIVE_INSTALLED_HOOK_TEST
        if (installedHooks) o = hooks.Originals();
#endif
        auto mask = RestoreMask::All;
#ifdef NATIVE_INSTALLED_HOOK_TEST
        if (computeOnly) mask = RestoreMask::Compute | RestoreMask::Pipeline | RestoreMask::Heaps;
#endif
        if (invalidatedTables) mask = mask | RestoreMask::HeapInvalidatedTables;
        auto saved = adapter ? observer.CaptureRestorable(list.Get(), mask, o)
                             : state.Capture(list.Get(), 1, mask);
        assert(saved);
        ID3D12DescriptorHeap* changed[] = {alien.Get()};
        list->SetDescriptorHeaps(1, changed);
        list->SetPipelineState(graphicsPso.Get());
        list->SetComputeRootSignature(computeRoot.Get());
#ifdef NATIVE_INSTALLED_HOOK_TEST
        if (!computeOnly)
#endif
        list->SetGraphicsRootSignature(graphicsRoot.Get());
        assert(observe([&](auto& s, auto i) { return s.ObserveDescriptorHeaps(i, 1, changed); }));
        if (adapter) assert(!observer.CaptureRestorable(list.Get(), RestoreMask::All, o));
        const auto incarnation = adapter ? observer.Observe(list.Get()).incarnation : 1;
        assert(adapter ? observer.Restore(list.Get(), incarnation, *saved, o) : state.Restore(*saved, o));
    }
    if (invalidatedTables)
    {
        // A real caller rebind is required before using the invalidated table.
        // Do not dispatch through undefined state. Constants and graphics table
        // still have to survive the inserted work for the pixel oracle to pass.
        if (adapter) assert(observer.WithCurrent(list.Get(), [&](auto& s, auto i) { return !s.Capture(list.Get(), i, RestoreMask::Compute); }));
        else assert(!state.Capture(list.Get(), 1, RestoreMask::All));
        list->SetComputeRootDescriptorTable(0, computeTable);
        assert(observe([&](auto& s, auto i) { return s.ObserveComputeTable(i, 0, computeTable); }));
    }
    list->Dispatch(1, 1, 1);
    if (adapter
#ifdef NATIVE_INSTALLED_HOOK_TEST
        && !installedHooks
#endif
    ) assert(observer.Work(list.Get()));
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {data.Get(), 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    list->ResourceBarrier(1, &b);
    list->SetPipelineState(graphicsPso.Get()); // required in both control and perturbed path
#ifdef NATIVE_INSTALLED_HOOK_TEST
    if (computeOnly)
    {
        list->SetGraphicsRootSignature(graphicsRoot.Get());
        list->SetGraphicsRootDescriptorTable(0, graphicsTable);
        list->SetGraphicsRoot32BitConstant(1, 1, 0);
    }
#endif
    const auto rt = rtv->GetCPUDescriptorHandleForHeapStart();
    list->OMSetRenderTargets(1, &rt, FALSE, nullptr);
    D3D12_VIEWPORT vp{}; vp.Width = 1; vp.Height = 1; vp.MaxDepth = 1;
    D3D12_RECT sc{0, 0, 1, 1}; list->RSSetViewports(1, &vp); list->RSSetScissorRects(1, &sc);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    if (adapter
#ifdef NATIVE_INSTALLED_HOOK_TEST
        && !installedHooks
#endif
    ) assert(observer.Work(list.Get()));
    unsigned expectedOrdinal = 2;
#ifdef NATIVE_INSTALLED_HOOK_TEST
    if (installedHooks) expectedOrdinal = 3;
#endif
    if (adapter) assert(observer.Observe(list.Get()).workOrdinal == expectedOrdinal);
    b.Transition = {target.Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = target.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dest{}; dest.pResource = readback.Get(); dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dest.PlacedFootprint = footprint; list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
    Ok(list->Close()); ID3D12CommandList* execute[] = {list.Get()}; queue->ExecuteCommandLists(1, execute);
    ComPtr<ID3D12Fence> fence; Ok(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    Ok(queue->Signal(fence.Get(), 1));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); assert(event);
    if (fence->GetCompletedValue() < 1)
    {
        Ok(fence->SetEventOnCompletion(1, event));
        assert(WaitForSingleObject(event, 30000) == WAIT_OBJECT_0);
    }
    CloseHandle(event);
    void* mapped = nullptr; D3D12_RANGE read{0, 4}; Ok(readback->Map(0, &read, &mapped));
    std::array<unsigned char, 4> result{}; std::memcpy(result.data(), mapped, 4);
    readback->Unmap(0, nullptr);
    ComPtr<ID3D12InfoQueue> info;
    Ok(device.As(&info));
    for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i)
    {
        SIZE_T bytes = 0; Ok(info->GetMessage(i, nullptr, &bytes));
        std::vector<unsigned char> storage(bytes);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Ok(info->GetMessage(i, message, &bytes));
        if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
            message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
            throw std::runtime_error(message->pDescription);
    }
#ifdef NATIVE_INSTALLED_HOOK_TEST
    if (installedHooks) { hooks.Revoke(); assert(!observer.Observe(list.Get()).completeCoverage); }
#endif
    return result;
}

