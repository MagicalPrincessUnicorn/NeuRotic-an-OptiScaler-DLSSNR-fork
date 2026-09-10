// Production capture and completion hooks on WARP; known bytes, delayed GPU and canceled recordings.
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../OptiScaler/dlssnr/NrPresentStageCapture.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>

using Microsoft::WRL::ComPtr;
namespace Safety = DlssNr::GpuSafety;
using Capture = DlssNr::StageCapture::PresentStages;
static void Check(HRESULT value) { assert(SUCCEEDED(value)); }

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path root = std::filesystem::path(argv[1]) / std::to_string(GetTickCount64());
    ComPtr<ID3D12Debug> debug;
    Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDesc {};
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> gate;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
    Check(queue->Wait(gate.Get(), 1));
    std::array<ComPtr<ID3D12Resource>, 3> textures;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 3> layouts {};
    std::array<UINT64, 3> sizes {};
    const std::array<DXGI_FORMAT, 3> formats {DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM};
    const std::array<const char*, 3> names {"layer1_input", "layer1_raw", "final_composed"};
    std::vector<DlssNr::StageCapture::StageInput> inputs;
    for (unsigned int stage = 0; stage < textures.size(); ++stage)
    {
        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 7 + stage * 2; desc.Height = 5 + stage;
        desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.SampleDesc.Count = 1; desc.Format = formats[stage];
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&textures[stage])));
        device->GetCopyableFootprints(&desc, 0, 1, 0, &layouts[stage], nullptr, nullptr, &sizes[stage]);
        inputs.push_back({names[stage], textures[stage].Get(), stage == 0 ?
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS});
    }
    std::array<ComPtr<ID3D12CommandAllocator>, Capture::MaxFrames> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>, Capture::MaxFrames> lists;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    Capture capture;
    capture.request(GetTickCount64());
    assert(!capture.wantsFrame(GetTickCount64()));
    capture.cancel(); capture.poll(root);
    assert(!capture.active() && !std::filesystem::exists(root));
    capture.request(GetTickCount64(), 0);
    for (unsigned int frame = 0; frame < Capture::MaxFrames; ++frame)
    {
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[frame])));
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[frame].Get(), nullptr,
                                       IID_PPV_ARGS(&lists[frame])));
        for (unsigned int stage = 0; stage < textures.size(); ++stage)
        {
            D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC buffer {}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = sizes[stage]; buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> upload;
            Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));
            void* data = nullptr; const D3D12_RANGE empty {0, 0};
            Check(upload->Map(0, &empty, &data));
            memset(data, 16 + frame * 8 + stage, static_cast<size_t>(sizes[stage]));
            upload->Unmap(0, nullptr);
            D3D12_RESOURCE_BARRIER barrier {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition = {textures[stage].Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                  inputs[stage].state, D3D12_RESOURCE_STATE_COPY_DEST};
            if (frame) lists[frame]->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION from {}, to {};
            from.pResource = upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint = layouts[stage];
            to.pResource = textures[stage].Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            lists[frame]->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            lists[frame]->ResourceBarrier(1, &barrier);
            uploads.push_back(upload);
        }
        assert(capture.record(lists[frame].Get(), device.Get(), inputs, 100 + frame, false, "fixture"));
        Check(lists[frame]->Close());
        ID3D12CommandList* submitted[] = {lists[frame].Get()};
        queue->ExecuteCommandLists(1, submitted);
    }
    assert(capture.count() == 8 && capture.wanted() == 8);
    assert(!capture.record(lists[0].Get(), device.Get(), inputs, 999, false, "fixture"));
    capture.poll(root);
    assert(capture.active() && !std::filesystem::exists(root));
    Check(gate->Signal(1));
    assert(Safety::Drain(10000));
    capture.poll(root);
    assert(capture.active() && !std::filesystem::exists(root)); // complete, still replayable
    for (unsigned int i = 0; i < lists.size(); ++i)
    {
        Check(allocators[i]->Reset());
        Check(lists[i]->Reset(allocators[i].Get(), nullptr));
    }
    capture.poll(root);
    assert(!capture.active() && capture.status().find("Saved:") == 0);
    auto directory = std::filesystem::directory_iterator(root)->path();
    assert(std::filesystem::exists(directory / "manifest.txt"));
    for (unsigned int frame = 0; frame < 8; ++frame)
        for (unsigned int stage = 0; stage < 3; ++stage)
        {
            std::ifstream file(directory / (std::string(names[stage]) + "_" + std::to_string(frame) + ".raw"), std::ios::binary);
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
            assert(bytes.size() == sizes[stage]);
            const auto desc = textures[stage]->GetDesc();
            const auto pixelBytes = stage == 1 ? 8u : 4u;
            for (unsigned int y = 0; y < desc.Height; ++y)
                for (UINT64 x = 0; x < desc.Width * pixelBytes; ++x)
                    assert(bytes[y * layouts[stage].Footprint.RowPitch + x] == 16 + frame * 8 + stage);
        }
    // An unsubmitted recording must not produce a success manifest.
    capture.request(GetTickCount64(), 0);
    assert(capture.record(lists[0].Get(), device.Get(), inputs, 200, false, "fixture"));
    capture.cancel(); capture.poll(root);
    assert(capture.active());
    Check(lists[0]->Close()); Check(lists[0]->Reset(allocators[0].Get(), nullptr));
    capture.poll(root); assert(!capture.active() && capture.status().find("interrupted") != std::string::npos);
    // A shape/layer-count change cannot combine unrelated images into the same batch.
    capture.request(GetTickCount64(), 0);
    assert(capture.record(lists[0].Get(), device.Get(), inputs, 300, false, "fixture"));
    auto changed = inputs; changed.pop_back();
    assert(!capture.record(lists[0].Get(), device.Get(), changed, 301, false, "fixture"));
    Check(lists[0]->Close()); Check(lists[0]->Reset(allocators[0].Get(), nullptr));
    capture.poll(root); assert(!capture.active());
    // Same shape, changed settings or history reset: also abort without freeing pending copies.
    for (bool reset : {false, true})
    {
        capture.request(GetTickCount64(), 0);
        assert(capture.record(lists[0].Get(), device.Get(), inputs, 400, false, "fixture"));
        assert(!capture.record(lists[0].Get(), device.Get(), inputs, 401, reset, reset ? "fixture" : "changed"));
        Check(lists[0]->Close()); Check(lists[0]->Reset(allocators[0].Get(), nullptr));
        capture.poll(root); assert(!capture.active());
    }
    // Two-layer six-stage batch, including repeated reads of one texture in different stages.
    auto six = inputs;
    six.push_back({"layer1_composed", textures[2].Get(), inputs[2].state});
    six.push_back({"layer2_input", textures[0].Get(), inputs[0].state});
    six.push_back({"layer2_raw", textures[1].Get(), inputs[1].state});
    for (bool blocked : {false, true})
    {
        capture.request(GetTickCount64(), 0);
        for (UINT64 i = 0; i < 8; ++i)
            assert(capture.record(lists[0].Get(), device.Get(), six, 500 + i, false, "six-stage fixture"));
        Check(lists[0]->Close());
        ID3D12CommandList* submitted[] = {lists[0].Get()}; queue->ExecuteCommandLists(1, submitted);
        assert(Safety::Drain(10000));
        Check(allocators[0]->Reset()); Check(lists[0]->Reset(allocators[0].Get(), nullptr));
        const auto destination = root / (blocked ? "blocked-file" : "six-stage");
        if (blocked) { std::ofstream file(destination); file << "existing file"; }
        capture.poll(destination);
        assert(!capture.active());
        if (blocked) assert(capture.status().find("could not be saved") != std::string::npos);
        else
        {
            assert(capture.status().find("Saved:") == 0);
            const auto output = std::filesystem::directory_iterator(destination)->path();
            assert(std::distance(std::filesystem::directory_iterator(output), std::filesystem::directory_iterator()) == 49);
            std::ifstream raw(output / "layer2_raw_7.raw", std::ios::binary);
            assert(raw.get() == 16 + 7 * 8 + 1);
        }
    }
    ComPtr<ID3D12InfoQueue> info; Check(device.As(&info));
    for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i)
    {
        SIZE_T size = 0; Check(info->GetMessage(i, nullptr, &size));
        std::vector<unsigned char> data(size); auto* message = reinterpret_cast<D3D12_MESSAGE*>(data.data());
        Check(info->GetMessage(i, message, &size));
        assert(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR && message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION);
    }
    std::cout << "PASS: matched stage pixels/formats/pitches across 8 delayed submissions; completion and replay gates; cancellation, layer/settings/reset changes; six-stage output and write failure; no debug-layer errors.\n";
    std::cout << "Evidence: " << root.string() << '\n';
}
