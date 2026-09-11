#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../OptiScaler/dlssnr/DlssNr_PresentGuides.h"
#include <d3d12sdklayers.h>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace Safety = DlssNr::GpuSafety;
namespace Guides = DlssNr::PresentGuides;
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) { assert(SUCCEEDED(hr)); }
int main(int argc, char** argv)
{
    const bool hardware = argc == 2 && std::strcmp(argv[1], "--hardware") == 0;
    ComPtr<ID3D12Debug> debug;
    const bool debugEnabled = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    if (debugEnabled) debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Check(D3D12CreateDevice(hardware ? nullptr : warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<IDXGIAdapter1> actualAdapter;
    Check(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&actualAdapter)));
    DXGI_ADAPTER_DESC1 adapterDesc {}; Check(actualAdapter->GetDesc1(&adapterDesc));
    std::printf("Guide test adapter: %ls (%s)\n", adapterDesc.Description, hardware ? "hardware" : "WARP");
    ComPtr<ID3D12CommandQueue> queue, other;
    D3D12_COMMAND_QUEUE_DESC qd {}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));
    Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&other)));
    ComPtr<ID3D12CommandAllocator> pa, ca;
    Check(device->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&pa)));
    Check(device->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&ca)));
    ComPtr<ID3D12GraphicsCommandList> producer, consumer;
    Check(device->CreateCommandList(0, qd.Type, pa.Get(), nullptr, IID_PPV_ARGS(&producer)));
    Check(device->CreateCommandList(0, qd.Type, ca.Get(), nullptr, IID_PPV_ARGS(&consumer)));
    auto submit = [&](ID3D12GraphicsCommandList* list)
    {
        Check(list->Close()); ID3D12CommandList* lists[] = {list}; queue->ExecuteCommandLists(1, lists);
    };
    auto reset = [&](ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator)
    { Check(allocator->Reset()); Check(list->Reset(allocator, nullptr)); };
    auto texture = [&](DXGI_FORMAT format, UINT16 mips = 1)
    {
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 8; desc.Height = 4; desc.DepthOrArraySize = 1; desc.MipLevels = mips;
        desc.Format = format; desc.SampleDesc.Count = 1;
        ComPtr<ID3D12Resource> result;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&result)));
        return result;
    };
    auto buffer = [&](D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = type;
        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = 4096; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> result;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
              type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ :
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&result)));
        return result;
    };
    auto depth = texture(DXGI_FORMAT_R32_FLOAT), motion = texture(DXGI_FORMAT_R32G32_FLOAT);
    auto upload = buffer(D3D12_HEAP_TYPE_UPLOAD), readback = buffer(D3D12_HEAP_TYPE_READBACK);
    float* data = nullptr;
    Check(upload->Map(0, nullptr, reinterpret_cast<void**>(&data)));
    for (UINT i = 0; i < 1024; ++i) data[i] = static_cast<float>(i) / 1024.0f;
    upload->Unmap(0, nullptr);
    auto copyTexture = [&](ID3D12GraphicsCommandList* list, ID3D12Resource* image,
                           ID3D12Resource* buf, UINT64 offset, bool toTexture)
    {
        D3D12_TEXTURE_COPY_LOCATION t {}, b {};
        t.pResource = image; t.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        b.pResource = buf; b.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        b.PlacedFootprint.Offset = offset;
        b.PlacedFootprint.Footprint = {image->GetDesc().Format, 8, 4, 1, 256};
        list->CopyTextureRegion(toTexture ? &t : &b, 0, 0, 0, toTexture ? &b : &t, nullptr);
    };
    auto transition = [&](ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                          D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        list->ResourceBarrier(1, &barrier);
    };
    copyTexture(producer.Get(), depth.Get(), upload.Get(), 0, true);
    copyTexture(producer.Get(), motion.Get(), upload.Get(), 1024, true);
    for (auto* image : {depth.Get(), motion.Get()})
        transition(producer.Get(), image, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Guides::Bridge bridge;
    assert(!bridge.Inspect().enabled);
    bridge.Enable(true);
    DlssNrFrameInfo frame {};
    frame.DepthInverted = true; frame.Reset = true;
    frame.MvScaleX = 8; frame.MvScaleY = -4; frame.JitterX = 0.25f; frame.JitterY = -0.25f;
    frame.RenderSubrectWidth = 8; frame.RenderSubrectHeight = 4;
    frame.ExposureTexture = reinterpret_cast<void*>(1); // must never travel to Present
    auto capture = [&]
    {
        bridge.Capture(producer.Get(), depth.Get(), motion.Get(), frame, device.Get(), 2, 16, 8,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    auto bind = [&](const Guides::Selection& selected, Guides::Inputs& inputs,
                    ID3D12CommandQueue* q = nullptr, UINT index = 2)
    { return bridge.Bind(selected, consumer.Get(), q ? q : queue.Get(), device.Get(), index, 16, 8, inputs); };
    capture();
    auto selected = bridge.BeginPresent();
    Guides::Inputs inputs;
    assert(!bind(selected, inputs)); // not submitted
    ComPtr<ID3D12Fence> gate;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
    Check(queue->Wait(gate.Get(), 1));
    submit(producer.Get());
    assert(!bind(selected, inputs, other.Get())); // no fabricated queue dependency
    assert(!bind(selected, inputs, queue.Get(), 1)); // wrong backbuffer
    assert(bind(selected, inputs)); // pending producer, ordered GPU consumer
    assert(inputs.depth.Get() != depth.Get() && inputs.motion.Get() != motion.Get());
    assert(inputs.frame.DepthInverted && inputs.frame.Reset && inputs.frame.MvScaleY == -4 &&
           inputs.frame.JitterX == 0.25f && inputs.frame.RenderSubrectWidth == 8 && !inputs.frame.ExposureTexture);
    assert(!bind(selected, inputs)); // cannot consume twice
    for (auto* image : {inputs.depth.Get(), inputs.motion.Get()})
        transition(consumer.Get(), image, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
    copyTexture(consumer.Get(), inputs.depth.Get(), readback.Get(), 0, false);
    copyTexture(consumer.Get(), inputs.motion.Get(), readback.Get(), 1024, false);
    // Match the production reader's resting state for replay/lifetime tests.
    for (auto* image : {inputs.depth.Get(), inputs.motion.Get()})
        transition(consumer.Get(), image, D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    submit(consumer.Get());
    assert(!Safety::Drain(0));
    Check(gate->Signal(1));
    assert(Safety::Drain(5000));
    Check(readback->Map(0, nullptr, reinterpret_cast<void**>(&data)));
    for (UINT y = 0; y < 4; ++y)
    {
        for (UINT x = 0; x < 8; ++x) assert(data[y * 64 + x] == float(y * 64 + x) / 1024.0f);
        for (UINT x = 0; x < 16; ++x) assert(data[256 + y * 64 + x] == float(256 + y * 64 + x) / 1024.0f);
    }
    readback->Unmap(0, nullptr);
    reset(producer.Get(), pa.Get()); // consumer stays completed-but-replayable and owns one slot
    assert(!bind(bridge.BeginPresent(), inputs)); // no previous-frame reuse
    capture(); capture(); selected = bridge.BeginPresent();
    assert(selected.count == 2 && !bind(selected, inputs)); // ambiguous evaluations
    Check(producer->Close()); reset(producer.Get(), pa.Get()); // cancel safely
    capture(); selected = bridge.BeginPresent();
    bridge.Enable(false); bridge.Enable(true);
    assert(!bind(selected, inputs)); // stale toggle generation
    Check(producer->Close()); reset(producer.Get(), pa.Get());
    // Bound exhaustion: complete-but-unsealed writers cannot be reclaimed or overwritten.
    std::array<ComPtr<ID3D12CommandAllocator>, 7> heldAlloc;
    std::array<ComPtr<ID3D12GraphicsCommandList>, 7> heldList;
    const auto beforeHeld = bridge.Inspect().captures;
    for (UINT i = 0; i < 7; ++i)
    {
        Check(device->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&heldAlloc[i])));
        Check(device->CreateCommandList(0, qd.Type, heldAlloc[i].Get(), nullptr, IID_PPV_ARGS(&heldList[i])));
        bridge.Capture(heldList[i].Get(), depth.Get(), motion.Get(), frame, device.Get(), 2, 16, 8,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        bridge.BeginPresent(); submit(heldList[i].Get());
    }
    assert(Safety::Drain(5000));
    assert(bridge.Inspect().captures == beforeHeld + 7);
    const auto before = bridge.Inspect().captures;
    capture(); assert(bridge.Inspect().captures == before);
    assert(bridge.Inspect().status.find("busy") != std::string::npos);
    bridge.BeginPresent();
    consumer.Reset(); // sealing the original reader, not its writer, frees the eighth slot
    capture(); assert(bridge.Inspect().captures == before + 1);
    for (auto& list : heldList) list.Reset(); // seal completed writes
    Check(producer->Close()); reset(producer.Get(), pa.Get());
    bridge.BeginPresent();
    // Regression: successful Native-compatible base-mip copies, not a whole-resource copy
    // into a differently shaped allocation. All nonzero source mips retain their state.
    Check(device->CreateCommandList(0, qd.Type, ca.Get(), nullptr, IID_PPV_ARGS(&consumer)));
    for (const auto mf : {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
                           DXGI_FORMAT_R16G16_SNORM, DXGI_FORMAT_R16G16_UNORM})
    {
        Guides::Bridge formats; formats.Enable(true);
        auto pyramid = texture(DXGI_FORMAT_R16_FLOAT, 3);
        auto wideMotion = texture(mf, 3);
        copyTexture(producer.Get(), pyramid.Get(), upload.Get(), 0, true);
        copyTexture(producer.Get(), wideMotion.Get(), upload.Get(), 1024, true);
        for (auto* image : {pyramid.Get(), wideMotion.Get()})
            transition(producer.Get(), image, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        formats.Capture(producer.Get(), pyramid.Get(), wideMotion.Get(), frame, device.Get(), 2, 16, 8,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto choice = formats.BeginPresent();
        assert(choice.slot >= 0 && formats.Inspect().captureAttempts == 1);
        submit(producer.Get());
        Guides::Inputs typed;
        assert(formats.Bind(choice, consumer.Get(), queue.Get(), device.Get(), 2, 16, 8, typed));
        assert(typed.depth->GetDesc().MipLevels == 1 && typed.motion->GetDesc().MipLevels == 1);
        for (auto* image : {typed.depth.Get(), typed.motion.Get()})
            transition(consumer.Get(), image, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
        copyTexture(consumer.Get(), typed.depth.Get(), readback.Get(), 0, false);
        copyTexture(consumer.Get(), typed.motion.Get(), readback.Get(), 1024, false);
        for (auto* image : {typed.depth.Get(), typed.motion.Get()})
            transition(consumer.Get(), image, D3D12_RESOURCE_STATE_COPY_SOURCE,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        submit(consumer.Get()); assert(Safety::Drain(5000));
        void* expected = nullptr; void* actual = nullptr;
        Check(upload->Map(0, nullptr, &expected)); Check(readback->Map(0, nullptr, &actual));
        const UINT motionBytes = mf == DXGI_FORMAT_R32G32B32A32_FLOAT ? 128 :
                                 mf == DXGI_FORMAT_R16G16B16A16_FLOAT ? 64 : 32;
        for (UINT y = 0; y < 4; ++y)
        {
            assert(std::memcmp(static_cast<char*>(expected) + y * 256, static_cast<char*>(actual) + y * 256, 16) == 0);
            assert(std::memcmp(static_cast<char*>(expected) + 1024 + y * 256,
                               static_cast<char*>(actual) + 1024 + y * 256, motionBytes) == 0);
        }
        upload->Unmap(0, nullptr); readback->Unmap(0, nullptr);
        reset(producer.Get(), pa.Get()); reset(consumer.Get(), ca.Get());
    }
    // Wilds reports format 19 (R32G8X24_TYPELESS). Its stencil plane must not be
    // mistaken for depth or transitioned from the depth plane's independent state.
    for (auto df : {DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT})
    {
        Guides::Bridge planar; planar.Enable(true);
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        auto desc = depth->GetDesc(); desc.Format = df;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        ComPtr<ID3D12Resource> ds;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr, IID_PPV_ARGS(&ds)));
        D3D12_DESCRIPTOR_HEAP_DESC hd {}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; hd.NumDescriptors = 1;
        ComPtr<ID3D12DescriptorHeap> dsvHeap;
        Check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsvHeap)));
        D3D12_DEPTH_STENCIL_VIEW_DESC vd {}; vd.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        vd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        auto handle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateDepthStencilView(ds.Get(), &vd, handle);
        const auto both = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
        producer->ClearDepthStencilView(handle, both, 0.25f, 0xA5, 0, nullptr);
        D3D12_RECT right {4, 0, 8, 4};
        producer->ClearDepthStencilView(handle, both, 0.75f, 0x5A, 1, &right);
        D3D12_RESOURCE_BARRIER db {}; db.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        db.Transition = {ds.Get(), 0, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        producer->ResourceBarrier(1, &db); // stencil stays DEPTH_WRITE
        auto rg16 = texture(DXGI_FORMAT_R16G16_FLOAT);
        copyTexture(producer.Get(), rg16.Get(), upload.Get(), 1024, true);
        transition(producer.Get(), rg16.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        planar.Capture(producer.Get(), ds.Get(), rg16.Get(), frame, device.Get(), 2, 16, 8,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto choice = planar.BeginPresent();
        assert(choice.slot >= 0 && choice.captureError.empty());
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
        Check(queue->Wait(gate.Get(), 1)); submit(producer.Get());
        Guides::Inputs typed;
        assert(planar.Bind(choice, consumer.Get(), queue.Get(), device.Get(), 2, 16, 8, typed));
        assert(typed.depth->GetDesc().Format == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ComPtr<ID3D12DescriptorHeap> srvHeap;
        Check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap)));
        D3D12_SHADER_RESOURCE_VIEW_DESC srv {}; srv.Format = typed.depth->GetDesc().Format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(typed.depth.Get(), &srv, srvHeap->GetCPUDescriptorHandleForHeapStart());
        // GetCopyableFootprints exposes plane-zero float layout; never guess an
        // interleaved 64-bit depth/stencil texel or copy a stencil plane into NR.
        D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
        src.pResource = typed.depth.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        const auto copiedDesc = typed.depth->GetDesc();
        device->GetCopyableFootprints(&copiedDesc, 0, 1, 0, &dst.PlacedFootprint, nullptr, nullptr, nullptr);
        std::printf("Depth plane footprint format=%u rowPitch=%u\n", dst.PlacedFootprint.Footprint.Format,
                    dst.PlacedFootprint.Footprint.RowPitch); std::fflush(stdout);
        assert(dst.PlacedFootprint.Footprint.Format == DXGI_FORMAT_R32_TYPELESS);
        const UINT depthPitch = dst.PlacedFootprint.Footprint.RowPitch;
        const UINT64 stencilOffset = (UINT64(depthPitch) * 4 + 511) & ~511ull;
        db.Transition = {typed.depth.Get(), 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_COPY_SOURCE};
        consumer->ResourceBarrier(1, &db);
        consumer->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        std::swap(db.Transition.StateBefore, db.Transition.StateAfter); consumer->ResourceBarrier(1, &db);
        // Read ORIGINAL stencil, proving its independent state and values survived capture.
        src.pResource = ds.Get(); src.SubresourceIndex = 1;
        device->GetCopyableFootprints(&desc, 1, 1, stencilOffset, &dst.PlacedFootprint, nullptr, nullptr, nullptr);
        const UINT stencilPitch = dst.PlacedFootprint.Footprint.RowPitch;
        assert(stencilOffset + UINT64(stencilPitch) * 4 <= readback->GetDesc().Width);
        db.Transition = {ds.Get(), 1, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE};
        consumer->ResourceBarrier(1, &db);
        consumer->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        std::swap(db.Transition.StateBefore, db.Transition.StateAfter); consumer->ResourceBarrier(1, &db);
        submit(consumer.Get()); assert(!Safety::Drain(0)); Check(gate->Signal(1)); assert(Safety::Drain(5000));
        Check(readback->Map(0, nullptr, reinterpret_cast<void**>(&data)));
        for (UINT y = 0; y < 4; ++y) for (UINT x = 0; x < 8; ++x)
        {
            assert(data[y * (depthPitch / sizeof(float)) + x] == (x < 4 ? 0.25f : 0.75f));
            assert(reinterpret_cast<unsigned char*>(data)[stencilOffset + y * stencilPitch + x] == (x < 4 ? 0xA5 : 0x5A));
        }
        readback->Unmap(0, nullptr);
        reset(producer.Get(), pa.Get()); reset(consumer.Get(), ca.Get());
    }
    std::puts("D32S8 depth-plane copies PASS: format 19/20, exact float depth, independent stencil, RG16 motion, delayed queue.");
    // The exact capture failure and input descriptors must survive BeginPresent/Bind, even
    // after a later frame changes the live telemetry. This reproduced the hidden-cause defect.
    Guides::Bridge errors; errors.Enable(true);
    errors.Capture(producer.Get(), depth.Get(), motion.Get(), frame, nullptr, 2, 16, 8,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const auto missingIdentity = errors.BeginPresent();
    assert(missingIdentity.captureError.find("swapchain/identity") != std::string::npos);
    auto badMotion = texture(DXGI_FORMAT_R32_UINT);
    errors.Capture(producer.Get(), depth.Get(), badMotion.Get(), frame, device.Get(), 2, 16, 8,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    const auto unsupported = errors.BeginPresent();
    assert(!errors.Bind(missingIdentity, consumer.Get(), queue.Get(), device.Get(), 2, 16, 8, inputs));
    assert(errors.Inspect().status == missingIdentity.captureError);
    assert(!errors.Bind(unsupported, consumer.Get(), queue.Get(), device.Get(), 2, 16, 8, inputs));
    assert(errors.Inspect().status.find("unsupported motion") != std::string::npos);
    assert(errors.Inspect().status.find("format=42") != std::string::npos);
    assert(!errors.Bind(errors.BeginPresent(), consumer.Get(), queue.Get(), device.Get(), 2, 16, 8, inputs));
    assert(errors.Inspect().status.find("callback not observed") != std::string::npos);
    // Metadata-only Follow native does not require guide textures or allocate/copy them.
    Guides::Bridge metadata; metadata.Enable(true, 77);
    auto metadataFrame = frame;
    metadataFrame.RenderSubrectWidth = 13; metadataFrame.RenderSubrectHeight = 7;
    auto captureMetadata = [&](const char* reason = nullptr) {
        metadata.Capture(producer.Get(), nullptr, nullptr, metadataFrame, device.Get(), 2, 16, 8,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON, false, reason);
    };
    captureMetadata(); auto meta = metadata.BeginPresent();
    assert(!metadata.MatchMetadata(meta, queue.Get(), device.Get(), 2, 16, 8)); // not submitted
    submit(producer.Get());
    assert(!metadata.MatchMetadata(meta, other.Get(), device.Get(), 2, 16, 8));
    assert(!metadata.MatchMetadata(meta, queue.Get(), device.Get(), 1, 16, 8));
    assert(!metadata.MatchMetadata(meta, queue.Get(), device.Get(), 2, 32, 16));
    assert(metadata.MatchMetadata(meta, queue.Get(), device.Get(), 2, 16, 8));
    assert(meta.frame.RenderSubrectWidth == 13 && !meta.frame.ExposureTexture);
    assert(metadata.Inspect().captures == 0);
    metadata.Enable(true, 78); // route/resolution change with capture still enabled
    assert(!metadata.MatchMetadata(meta, queue.Get(), device.Get(), 2, 16, 8));
    assert(Safety::Drain(5000)); reset(producer.Get(), pa.Get());
    captureMetadata(); captureMetadata(); meta = metadata.BeginPresent();
    assert(!metadata.MatchMetadata(meta, queue.Get(), device.Get(), 2, 16, 8));
    Check(producer->Close()); reset(producer.Get(), pa.Get());
    metadataFrame.RenderSubrectWidth = 0; captureMetadata(); meta = metadata.BeginPresent();
    assert(meta.captureError.find("render-subrect") != std::string::npos);
    metadataFrame.RenderSubrectWidth = 17; captureMetadata(); meta = metadata.BeginPresent();
    assert(meta.captureError.find("render-subrect") != std::string::npos);
    metadataFrame.RenderSubrectWidth = 12; captureMetadata("Missing jitter"); meta = metadata.BeginPresent();
    assert(meta.captureError == "Missing jitter");
    // Full copies preserve nonzero valid guide subrects; overflow must fail before any copy.
    Guides::Bridge offsets; offsets.Enable(true);
    auto subrect = frame; subrect.RenderSubrectWidth = 4; subrect.RenderSubrectHeight = 2;
    subrect.DepthSubrectX = 3; subrect.DepthSubrectY = 1;
    subrect.MotionSubrectX = 2; subrect.MotionSubrectY = 2;
    offsets.Capture(producer.Get(), depth.Get(), motion.Get(), subrect, device.Get(), 2, 16, 8,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto offsetChoice = offsets.BeginPresent(); submit(producer.Get());
    assert(offsets.MatchMetadata(offsetChoice, queue.Get(), device.Get(), 2, 16, 8));
    assert(offsets.Bind(offsetChoice, consumer.Get(), queue.Get(), device.Get(), 2, 16, 8, inputs));
    assert(inputs.frame.DepthSubrectX == 3 && inputs.frame.MotionSubrectY == 2);
    submit(consumer.Get()); assert(Safety::Drain(5000));
    reset(producer.Get(), pa.Get()); reset(consumer.Get(), ca.Get());
    subrect.DepthSubrectX = UINT_MAX;
    offsets.Capture(producer.Get(), depth.Get(), motion.Get(), subrect, device.Get(), 2, 16, 8,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    assert(offsets.BeginPresent().captureError.find("subrect exceeds") != std::string::npos);
    subrect = frame; subrect.JitterX = NAN;
    offsets.Capture(producer.Get(), depth.Get(), motion.Get(), subrect, device.Get(), 2, 16, 8,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    assert(offsets.BeginPresent().captureError.find("non-finite") != std::string::npos);
    std::puts("PASS fresh metadata-only dimensions, same-queue/output matching, resolution generation, subrect origins/bounds and missing temporal metadata.");
    producer.Reset(); consumer.Reset();
    assert(Safety::Drain(5000));
    if (debugEnabled)
    {
        ComPtr<ID3D12InfoQueue> info; Check(device.As(&info));
        for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0; info->GetMessage(i, nullptr, &size);
            std::vector<char> bytes(size); auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            Check(info->GetMessage(i, message, &size));
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            { std::puts(message->pDescription); assert(false); }
        }
    }
    std::puts("Present Native guide tests PASS: pixels, metadata, queue ordering, stale/ambiguous refusal, bounded lifetime.");
    std::puts(debugEnabled ? "D3D12 debug validation: PASS" : "D3D12 debug layer unavailable");
}
