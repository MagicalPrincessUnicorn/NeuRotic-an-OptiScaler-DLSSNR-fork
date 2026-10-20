// Actual completion path on a removed software device; no game, model or hardware reset.
#include "dlssnr/DredDiagnostics.h"
#undef NR_FRAME_TRACE
#define NR_GPU_SAFETY_TEST
#define NR_GPU_SAFETY_DIAGNOSTICS_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include <dxgi1_4.h>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    namespace Safety = DlssNr::GpuSafety;
    const bool enabled = std::string(argv[1]) == "on";
    const auto directory = std::filesystem::path(argv[2]);
    std::filesystem::create_directories(directory);
    const auto report = directory / "DRED.log";
    if (std::filesystem::exists(report)) return 3;
    SetEnvironmentVariableA("NEUROTIC_FG_LIFECYCLE", enabled ? "1" : nullptr);
    SetEnvironmentVariableA("NEUROTIC_DRED", "1");
    SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION", "0123456789abcdef0123456789abcdef");
    SetEnvironmentVariableW(L"NEUROTIC_DIAGNOSTIC_DIRECTORY", directory.c_str());
    DlssNr::DredDiagnostics::Configure(&D3D12GetDebugInterface);
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
    Microsoft::WRL::ComPtr<ID3D12Device5> device;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
        FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return 4;
    auto timeline = std::make_shared<Safety::Timeline>();
    if (FAILED(device->CreateFence(1, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&timeline->fence)))) return 5;
    auto ticket = std::make_shared<Safety::Recording>();
    ticket->sealed = true;
    ticket->points.push_back({timeline, 1});
    if (!Safety::Completed(ticket) || std::filesystem::exists(report)) return 6;
    device->RemoveDevice();
    for (int i = 0; i < 100 && timeline->fence->GetCompletedValue() != UINT64_MAX; ++i) Sleep(5);
    if (timeline->fence->GetCompletedValue() != UINT64_MAX) return 7;
    if (Safety::Completed(ticket) || !Safety::State().failed) return 8;
    if (std::filesystem::exists(report) != enabled)
    { std::cerr << "FAIL: fence-first loss did not preserve expected diagnostic report\n"; return 9; }
    if (enabled)
    {
        std::ifstream input(report);
        const std::string contents((std::istreambuf_iterator<char>(input)), {});
        if (contents.find("kind=report-end") == std::string::npos) return 10;
        const auto size = std::filesystem::file_size(report);
        if (Safety::Completed(ticket) || std::filesystem::file_size(report) != size) return 11;
    }
    std::cout << "PASS: healthy fence, first loss, fail-closed completion and "
              << (enabled ? "one-shot DRED report" : "disabled diagnostics") << '\n';
    return 0;
}
