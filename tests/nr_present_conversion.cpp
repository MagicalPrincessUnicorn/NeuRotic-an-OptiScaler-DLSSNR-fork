// Real FT_Dx12/Shader_Dx12/heaps/compiled shader; application services only are stubbed.
#include "pch.h"
#include <shaders/format_transfer/FT_Dx12.h>
#include <d3d12sdklayers.h>
#include <cassert>
#include <cmath>
#include <iostream>

GpuTime_Dx12::GpuTime_Dx12(ID3D12Device*, bool) {}
GpuTime_Dx12::~GpuTime_Dx12() = default;
void GpuTime_Dx12::Start(ID3D12GraphicsCommandList*) {}
void GpuTime_Dx12::End(ID3D12GraphicsCommandList*) {}
ID3DBlob* CompileShader(const char*, const char*, const char*) { return nullptr; }
using Microsoft::WRL::ComPtr;
static void Check(HRESULT result) { assert(SUCCEEDED(result)); }

int main()
{
    ComPtr<ID3D12Debug> debug;
    Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))); debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> warp; Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    ComPtr<ID3D12Device> device; Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC queueDesc {};
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
    for (UINT size : {64u, 80u})
    {
        const auto texture = [&](DXGI_FORMAT format, D3D12_RESOURCE_STATES state) {
            ComPtr<ID3D12Resource> resource;
            auto desc = CD3DX12_RESOURCE_DESC::Tex2D(format, size, size, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
            Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource)));
            return resource;
        };
        auto input = texture(DXGI_FORMAT_R10G10B10A2_UNORM, D3D12_RESOURCE_STATE_COPY_DEST);
        auto output = texture(DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto reverseOutput = texture(DXGI_FORMAT_R10G10B10A2_UNORM, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto other = texture(DXGI_FORMAT_R10G10B10A2_UNORM, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {}; UINT64 bytes = 0;
        const auto desc = input->GetDesc();
        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        const auto buffer = [&](D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
            ComPtr<ID3D12Resource> resource;
            const CD3DX12_HEAP_PROPERTIES heap(type); const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
            Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, state, nullptr, IID_PPV_ARGS(&resource)));
            return resource;
        };
        auto upload = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        unsigned char* mapped = nullptr; const D3D12_RANGE noRead {0, 0};
        Check(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)));
        const auto channel = [](UINT x, UINT y, UINT shift) { return ((x * 17 + y * 11 + shift) % 1024); };
        for (UINT y = 0; y < size; ++y)
            for (UINT x = 0; x < size; ++x)
                reinterpret_cast<uint32_t*>(mapped + footprint.Offset + y * footprint.Footprint.RowPitch)[x] =
                    channel(x, y, 0) | (channel(x, y, 173) << 10) | (channel(x, y, 431) << 20) | (3u << 30);
        upload->Unmap(0, nullptr);
        FT_Dx12 conversion("Present conversion fixture", device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
        FT_Dx12 reverse("Present reverse fixture", device.Get(), DXGI_FORMAT_R10G10B10A2_UNORM);
        assert(conversion.Ready() && reverse.Ready());
        assert(!conversion.BindImmutableDescriptors(nullptr, output.Get()));
        assert(conversion.BindImmutableDescriptors(input.Get(), output.Get()));
        assert(reverse.BindImmutableDescriptors(output.Get(), reverseOutput.Get()));
        assert(!conversion.BindImmutableDescriptors(other.Get(), output.Get()));
        ComPtr<ID3D12Fence> gate, complete;
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&complete)));
        std::array<ComPtr<ID3D12CommandAllocator>, 8> allocators;
        std::array<ComPtr<ID3D12GraphicsCommandList>, 8> lists;
        std::array<ComPtr<ID3D12Resource>, 8> readbacks;
        Check(queue->Wait(gate.Get(), 1)); // Fixture backlog only; no new wait in production.
        for (size_t i = 0; i < lists.size(); ++i)
        {
            Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])));
            Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i].Get(), nullptr, IID_PPV_ARGS(&lists[i])));
            const auto transition = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
                const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
                lists[i]->ResourceBarrier(1, &barrier);
            };
            if (i == 0)
            {
                const CD3DX12_TEXTURE_COPY_LOCATION source(upload.Get(), footprint), destination(input.Get(), 0);
                lists[i]->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
                transition(input.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            assert(conversion.BindImmutableDescriptors(input.Get(), output.Get()));
            assert(!conversion.Dispatch(lists[i].Get(), other.Get(), output.Get()));
            assert(!conversion.Dispatch(lists[i].Get(), input.Get(), reverseOutput.Get()));
            assert(conversion.Dispatch(lists[i].Get(), input.Get(), output.Get()));
            transition(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            assert(reverse.Dispatch(lists[i].Get(), output.Get(), reverseOutput.Get()));
            transition(reverseOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            readbacks[i] = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
            const CD3DX12_TEXTURE_COPY_LOCATION source(reverseOutput.Get(), 0), destination(readbacks[i].Get(), footprint);
            lists[i]->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            transition(reverseOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            transition(output.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Check(lists[i]->Close());
            ID3D12CommandList* submitted[] = {lists[i].Get()}; queue->ExecuteCommandLists(1, submitted);
        }
        Check(queue->Signal(complete.Get(), 1)); assert(complete->GetCompletedValue() == 0);
        Check(gate->Signal(1));
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); assert(event);
        Check(complete->SetEventOnCompletion(1, event));
        assert(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0); CloseHandle(event);
        Check(device->GetDeviceRemovedReason());
        for (const auto& readback : readbacks)
        {
            const D3D12_RANGE range {0, static_cast<SIZE_T>(bytes)};
            Check(readback->Map(0, &range, reinterpret_cast<void**>(&mapped)));
            for (UINT y = 0; y < size; ++y)
                for (UINT x = 0; x < size; ++x)
                {
                    const uint32_t pixel = reinterpret_cast<uint32_t*>(mapped + footprint.Offset + y * footprint.Footprint.RowPitch)[x];
                    assert((pixel >> 30) == 3);
                    unsigned int shift = 0;
                    for (UINT offset : {0u, 173u, 431u})
                    {
                        const int expected = static_cast<int>(std::round(std::round(channel(x, y, offset) * 255.0 / 1023.0) * 1023.0 / 255.0));
                        assert(std::abs(static_cast<int>((pixel >> shift) & 1023u) - expected) <= 1);
                        shift += 10;
                    }
                }
            readback->Unmap(0, &noRead);
        }
        std::cout << "PASS: " << size << "x" << size << ", eight delayed conversions, quantified R10/8-bit round trip, rejected rebinds, drained generation\n";
    }
    ComPtr<ID3D12InfoQueue> info; Check(device.As(&info));
    for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i)
    {
        SIZE_T bytes = 0; Check(info->GetMessage(i, nullptr, &bytes)); std::vector<unsigned char> data(bytes);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(data.data()); Check(info->GetMessage(i, message, &bytes));
        if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR || message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
        { std::cerr << message->pDescription << '\n'; return 1; }
    }
    std::cout << "PASS: D3D12 debug layer reported no errors or corruption\n";
}
