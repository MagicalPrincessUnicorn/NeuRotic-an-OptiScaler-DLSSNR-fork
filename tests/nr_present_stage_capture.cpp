// Production capture and completion hooks on WARP; known bytes, delayed GPU and canceled recordings.
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../OptiScaler/dlssnr/NrPresentStageCapture.h"
#include "../OptiScaler/dlssnr/NrFinalOutputCapture.h"
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

static std::filesystem::path FindScreenshot(const std::filesystem::path& root, const std::string& tag,
                                          const std::filesystem::path& exclude = {})
{
    std::filesystem::path result;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        assert(entry.is_regular_file());
        assert(entry.path().extension() == ".png"); // No sidecars, folders or leftover claims.
        if (entry.path() != exclude && entry.path().filename().string().ends_with("_" + tag + ".png"))
        {
            assert(result.empty());
            result = entry.path();
        }
    }
    assert(!result.empty());
    return result;
}

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
    // Checkbox selection never silently requests the other route or additional images.
    for (unsigned int mask = 0; mask < 8; ++mask)
    {
        assert(DlssNr::Screenshots::AvailableSelection(mask, false) == (mask & 2u));
        assert(DlssNr::Screenshots::AvailableSelection(mask, true) == (mask & 5u));
        assert(DlssNr::Screenshots::AvailableSelection(mask, false, false) == (mask & 1u));
        assert(DlssNr::Screenshots::AvailableSelection(mask, true, false) == (mask & 1u));
        assert(DlssNr::Screenshots::AvailableSelection(mask, false, true, true) == (mask & 3u));
        assert(DlssNr::Screenshots::AvailableSelection(mask, true, true, true) == (mask & 5u));
    }
    // A screenshot request records exactly one frame's selected pair and writes actual PNGs.
    std::vector<DlssNr::StageCapture::StageInput> screenshotInputs {inputs[0], inputs[2]};
    screenshotInputs[0].name = "NR-Off";
    screenshotInputs[1].name = "Present-NR-On";
    capture.request(GetTickCount64(), 0, 1, true);
    assert(capture.wantsFrame(GetTickCount64()));
    assert(capture.record(lists[0].Get(), device.Get(), screenshotInputs, 600, false, "same_evaluation true"));
    assert(capture.count() == 1 && capture.wanted() == 1);
    assert(!capture.record(lists[0].Get(), device.Get(), screenshotInputs, 601, false, "same_evaluation true"));
    Check(lists[0]->Close());
    ID3D12CommandList* submitted[] = {lists[0].Get()}; queue->ExecuteCommandLists(1, submitted);
    assert(Safety::Drain(10000));
    Check(allocators[0]->Reset()); Check(lists[0]->Reset(allocators[0].Get(), nullptr));
    capture.poll(root / "screenshots");
    assert(!capture.active() && capture.status().find("Saved:") == 0);
    const auto pngDir = root / "screenshots";
    assert(std::distance(std::filesystem::directory_iterator(pngDir), std::filesystem::directory_iterator()) == 2);
    const auto offPng = FindScreenshot(pngDir, "NROFF");
    const auto presentPng = FindScreenshot(pngDir, "NRONPRESENT");
    const auto offName = offPng.filename().string();
    const auto batchPrefix = offName.substr(0, offName.size() - std::string("_NROFF.png").size());
    assert(presentPng.filename() == batchPrefix + "_NRONPRESENT.png");
    assert(!std::filesystem::exists(pngDir / (batchPrefix + "_CAPTURE.txt")));
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    {
        ComPtr<IWICImagingFactory> imaging;
        Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imaging)));
        auto verifyPng = [&](const std::filesystem::path& path, UINT expectedWidth, UINT expectedHeight,
                             std::array<unsigned char, 3> expected)
        {
            ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> decoded;
            ComPtr<IWICFormatConverter> converter;
            Check(imaging->CreateDecoderFromFilename(path.wstring().c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnLoad, &decoder));
            Check(decoder->GetFrame(0, &decoded));
            UINT width = 0, height = 0; Check(decoded->GetSize(&width, &height));
            assert(width == expectedWidth && height == expectedHeight);
            Check(imaging->CreateFormatConverter(&converter));
            Check(converter->Initialize(decoded.Get(), GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
            std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);
            Check(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(rgba.size()), rgba.data()));
            for (size_t i = 0; i < rgba.size(); i += 4)
                assert(rgba[i] == expected[0] && rgba[i+1] == expected[1] && rgba[i+2] == expected[2] && rgba[i+3] == 255);
        };
        verifyPng(offPng, 7, 5, {72, 72, 72});
        constexpr UINT packed = 0x4a4a4a4a;
        verifyPng(presentPng, 11, 7,
            {static_cast<unsigned char>(((packed & 1023) * 255 + 511) / 1023),
             static_cast<unsigned char>((((packed >> 10) & 1023) * 255 + 511) / 1023),
             static_cast<unsigned char>((((packed >> 20) & 1023) * 255 + 511) / 1023)});
        // Padded BGRA input, engine alpha zero: colour order correct and exported PNG opaque.
        const unsigned char bgra[] {9, 30, 200, 0, 0, 0, 0, 0};
        assert(DlssNr::Screenshots::WritePng(pngDir / "bgra.png", bgra, 1, 1, 8, DXGI_FORMAT_B8G8R8A8_UNORM));
        verifyPng(pngDir / "bgra.png", 1, 1, {200, 30, 9});
        assert(!DlssNr::Screenshots::WritePng(pngDir / "bad.png", bgra, 2, 1, 4, DXGI_FORMAT_B8G8R8A8_UNORM));
        assert(!DlssNr::Screenshots::WritePng(pngDir / "bad.png", bgra, 1, 1, 8, DXGI_FORMAT_R32_FLOAT));
        const unsigned short halfPixel[] {0x3800, 0x3c00, 0, 0};
        assert(DlssNr::Screenshots::WritePng(pngDir / "half.png", reinterpret_cast<const unsigned char*>(halfPixel),
            1, 1, 8, DXGI_FORMAT_R16G16B16A16_FLOAT));
        verifyPng(pngDir / "half.png", 1, 1, {128, 255, 0});
        const float floatPixel[] {0.25f, 2.0f, -1.0f, 0.0f};
        assert(DlssNr::Screenshots::WritePng(pngDir / "float.png", reinterpret_cast<const unsigned char*>(floatPixel),
            1, 1, 16, DXGI_FORMAT_R32G32B32A32_FLOAT));
        verifyPng(pngDir / "float.png", 1, 1, {64, 255, 0});

        // A full-resolution Native pair with linear scene data, independent from
        // the model's work resolution. Retain pre/post pixels in one recording.
        std::array<ComPtr<ID3D12Resource>, 2> nativeImages;
        std::vector<DlssNr::StageCapture::StageInput> nativeInputs;
        for (unsigned int i = 0; i < 2; ++i)
        {
            D3D12_RESOURCE_DESC desc {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = 7; desc.Height = 5; desc.DepthOrArraySize = desc.MipLevels = 1;
            desc.SampleDesc.Count = 1; desc.Format = DXGI_FORMAT_R32G32B32A32_TYPELESS;
            D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&nativeImages[i])));
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout {}; UINT64 bytes = 0;
            device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, nullptr, nullptr, &bytes);
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC buffer {}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = bytes; buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> upload;
            Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));
            void* data = nullptr; Check(upload->Map(0, nullptr, &data));
            for (UINT y = 0; y < 5; ++y)
                for (UINT x = 0; x < 7; ++x)
                {
                    const float pixel[] {i ? 64.0f : 16.0f, i ? 64.0f : 16.0f, i ? 64.0f : 16.0f, 0};
                    std::memcpy(static_cast<unsigned char*>(data) + y * layout.Footprint.RowPitch + x * 16,
                                pixel, sizeof(pixel));
                }
            upload->Unmap(0, nullptr);
            D3D12_TEXTURE_COPY_LOCATION from {}, to {};
            from.pResource = upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint = layout;
            to.pResource = nativeImages[i].Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            lists[0]->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            uploads.push_back(upload);
            nativeInputs.push_back({i ? "Native-NR-On" : "NR-Off", nativeImages[i].Get(),
                                    D3D12_RESOURCE_STATE_COPY_DEST, 16.0f});
        }
        capture.request(GetTickCount64(), 0, 1, true);
        assert(capture.record(lists[0].Get(), device.Get(), nativeInputs, 650, false, "Native matched linear pair"));
        Check(lists[0]->Close()); queue->ExecuteCommandLists(1, submitted);
        assert(Safety::Drain(10000));
        capture.poll(root / "native-pair"); assert(capture.active()); // Still replayable.
        Check(allocators[0]->Reset()); Check(lists[0]->Reset(allocators[0].Get(), nullptr));
        capture.poll(root / "native-pair"); assert(!capture.active());
        verifyPng(FindScreenshot(root / "native-pair", "NROFF"), 7, 5, {219, 219, 219});
        verifyPng(FindScreenshot(root / "native-pair", "NRON"), 7, 5, {252, 252, 252});
        const UINT r11White = 960u | (960u << 11) | (480u << 22);
        assert(DlssNr::Screenshots::WritePng(pngDir / "r11.png", reinterpret_cast<const unsigned char*>(&r11White),
            1, 1, 4, DXGI_FORMAT_R11G11B10_FLOAT, 1.0f));
        verifyPng(pngDir / "r11.png", 1, 1, {219, 219, 219});
        assert(!DlssNr::Screenshots::WritePng(pngDir / "bad-white.png", bgra, 1, 1, 8,
            DXGI_FORMAT_B8G8R8A8_UNORM, 1.0f));

        // Full-output capture precedes an overlay-like write on the SAME queue.
        // Gate the queue to prove no premature freeing/saving while its GPU work is pending.
        DlssNr::Screenshots::FinalOutputSubmission finalOutput;
        assert(Safety::Record(lists[0].Get()));
        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {textures[0].Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                              inputs[0].state, D3D12_RESOURCE_STATE_PRESENT};
        lists[0]->ResourceBarrier(1, &barrier);
        Check(lists[0]->Close());
        queue->ExecuteCommandLists(1, submitted);
        assert(Safety::Drain(10000));
        Check(allocators[0]->Reset()); Check(lists[0]->Reset(allocators[0].Get(), nullptr));
        Check(queue->Wait(gate.Get(), 2));
        assert(Safety::Record(lists[0].Get()));
        capture.request(GetTickCount64(), 0, 1, true);
        assert(finalOutput.submit(queue.Get(), textures[0].Get(), capture, "Current-output", 700, "pre-overlay full output"));
        finalOutput.poll(); capture.poll(root / "full-output");
        assert(finalOutput.active() && capture.active() && !std::filesystem::exists(root / "full-output"));
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        lists[0]->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION overlayFrom {}, overlayTo {};
        overlayFrom.pResource = uploads[0].Get(); // value 16, unlike the frame's original 72
        overlayFrom.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        overlayFrom.PlacedFootprint = layouts[0];
        overlayTo.pResource = textures[0].Get();
        overlayTo.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        lists[0]->CopyTextureRegion(&overlayTo, 0, 0, 0, &overlayFrom, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        lists[0]->ResourceBarrier(1, &barrier);
        Check(lists[0]->Close()); queue->ExecuteCommandLists(1, submitted);
        Check(gate->Signal(2));
        assert(Safety::Drain(10000));
        finalOutput.poll(); capture.poll(root / "full-output");
        assert(!finalOutput.active() && !capture.active());
        const auto fullDir = root / "full-output";
        const auto firstOutput = FindScreenshot(fullDir, "NRON");
        verifyPng(firstOutput, 7, 5, {72, 72, 72});
        capture.request(GetTickCount64(), 0, 1, true);
        assert(finalOutput.submit(queue.Get(), textures[0].Get(), capture, "Current-output", 701, "next output"));
        assert(Safety::Drain(10000)); finalOutput.poll(); capture.poll(fullDir);
        const auto secondOutput = FindScreenshot(fullDir, "NRON", firstOutput);
        verifyPng(secondOutput, 7, 5, {16, 16, 16});
        verifyPng(firstOutput, 7, 5, {72, 72, 72}); // Never overwrite the previous capture.
        assert(std::distance(std::filesystem::directory_iterator(fullDir), std::filesystem::directory_iterator()) == 2);
        Check(queue->Wait(gate.Get(), 3));
        capture.request(GetTickCount64(), 0, 1, true);
        assert(finalOutput.submit(queue.Get(), textures[0].Get(), capture, "Current-output", 702, "cancelled output"));
        capture.cancel(); finalOutput.poll(); capture.poll(root / "cancelled-output");
        assert(finalOutput.active() && capture.active());
        Check(gate->Signal(3)); assert(Safety::Drain(10000));
        finalOutput.poll(); capture.poll(root / "cancelled-output");
        assert(!finalOutput.active() && !capture.active());
        assert(!std::filesystem::exists(root / "cancelled-output"));
        Check(allocators[0]->Reset()); Check(lists[0]->Reset(allocators[0].Get(), nullptr));
        assert(DlssNr::Screenshots::HalfToFloat(0x0001) == std::ldexp(1.0f, -24));
        assert(DlssNr::Screenshots::HalfToFloat(0x7bff) == 65504.0f);
        assert(DlssNr::Screenshots::HalfToFloat(0xbc00) == -1.0f);
        assert(std::isinf(DlssNr::Screenshots::HalfToFloat(0x7c00)));
        assert(std::isnan(DlssNr::Screenshots::HalfToFloat(0x7e00)));
    }
    CoUninitialize();

    // A real pair just above 512 MiB: rejected by the old raw-diagnostic budget,
    // accepted as one PNG frame under the new 2 GiB budget. Cancel before submission
    // to avoid writing huge test images; no frame contents are needed for this check.
    {
        D3D12_RESOURCE_DESC large {};
        large.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        large.Width = 8192; large.Height = 8193;
        large.DepthOrArraySize = large.MipLevels = 1; large.SampleDesc.Count = 1;
        large.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> largeImage;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &large,
            D3D12_RESOURCE_STATE_PRESENT, nullptr, IID_PPV_ARGS(&largeImage)));
        UINT64 bytes = 0; device->GetCopyableFootprints(&large, 0, 1, 0, nullptr, nullptr, nullptr, &bytes);
        assert(bytes * 2 > Capture::MaxBytes && bytes * 2 < Capture::MaxScreenshotBytes);
        std::vector<DlssNr::StageCapture::StageInput> pair {
            {"NR-Off", largeImage.Get(), D3D12_RESOURCE_STATE_PRESENT},
            {"Present-NR-On", largeImage.Get(), D3D12_RESOURCE_STATE_PRESENT}};
        capture.request(GetTickCount64(), 0);
        assert(!capture.record(lists[0].Get(), device.Get(), pair, 800, false, "large raw"));
        assert(!capture.active() && capture.status().find("512 MiB") != std::string::npos);
        capture.request(GetTickCount64(), 0, 1, true);
        assert(capture.record(lists[0].Get(), device.Get(), pair, 800, false, "large PNG"));
        assert(capture.wanted() == 1 && capture.count() == 1);
        capture.cancel();
        Check(lists[0]->Close()); Check(lists[0]->Reset(allocators[0].Get(), nullptr));
        capture.poll(root); assert(!capture.active());
        pair[0].image = nullptr;
        capture.request(GetTickCount64(), 0, 1, true);
        assert(!capture.record(lists[0].Get(), device.Get(), pair, 801, false, "missing"));
        assert(capture.status().find("missing") != std::string::npos);
        assert(capture.status().find("MiB") == std::string::npos);
    }
    ComPtr<ID3D12InfoQueue> info; Check(device.As(&info));
    for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i)
    {
        SIZE_T size = 0; Check(info->GetMessage(i, nullptr, &size));
        std::vector<unsigned char> data(size); auto* message = reinterpret_cast<D3D12_MESSAGE*>(data.data());
        Check(info->GetMessage(i, message, &size));
        if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR || message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
            std::cerr << message->pDescription << '\n';
        assert(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR && message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION);
    }
    std::cout << "PASS: matched stage pixels/formats/pitches across 8 delayed submissions; completion and replay gates; cancellation, layer/settings/reset changes; six-stage output and write failure; no debug-layer errors.\n";
    std::cout << "Evidence: " << root.string() << '\n';
    std::cout << "PASS screenshots: every selection mask, exactly one matched GPU frame, PNG decode/pixel/dimension checks, RGBA/BGRA/R10, opaque alpha and invalid-format/stride rejection.\n";
    std::cout << "PASS full output: immediate request, completion-owned private submission, pre-overlay pixels, repeat capture, real readback pair above 512 MiB and distinct missing-buffer failure.\n";
    std::cout << "PASS Native: one full-resolution linear pair, fixed exposure, typeless float and R11 PNG pixels, exact completion/recording-seal gate.\n";
    std::cout << "PASS flat naming: PNG files only, shared pair prefix, final NR tags and repeated capture preserves earlier pixels.\n";
}
