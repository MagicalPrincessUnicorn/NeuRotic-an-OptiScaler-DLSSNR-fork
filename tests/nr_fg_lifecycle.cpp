#include "dlssnr/DredDiagnostics.h"
#include "dlssnr/PreFg.h"
#include <dxgi1_4.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 3);
    using namespace DlssNr;
    const std::string mode = argv[1];
    const auto directory = std::filesystem::path(argv[2]) / mode;
    std::filesystem::create_directories(directory);
    SetEnvironmentVariableA("NEUROTIC_FG_LIFECYCLE", mode == "off" ? nullptr : "1");
    SetEnvironmentVariableA("NEUROTIC_DRED", "1");
    SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION", "0123456789abcdef0123456789abcdef");
    SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_TRIGGER", "nr-enable");
    SetEnvironmentVariableW(L"NEUROTIC_DIAGNOSTIC_DIRECTORY",
        mode == "missing-directory" ? L"Z:\\missing-neurotic-diagnostics\\no-directory" : directory.c_str());
    if (mode == "off")
    {
        int calls = 0;
        NR_FG_EVENT("disabled", "value={}", ++calls);
        DredDiagnostics::Configure(nullptr);
        assert(!calls && !DredDiagnostics::configured && !std::filesystem::exists(directory / "FG-LIFECYCLE.log"));
        assert(FgLifecycle::Read().generation == 0);
    }
    else if (mode == "missing-directory")
    {
        NR_FG_EVENT("unavailable", "value=1");
        assert(FgLifecycle::Current().file == INVALID_HANDLE_VALUE);
    }
    else if (mode == "dred")
    {
        DredDiagnostics::Configure(&D3D12GetDebugInterface);
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
        Microsoft::WRL::ComPtr<ID3D12Device5> device;
        assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
        assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
        assert(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
        DredDiagnostics::Device(device.Get(), S_OK);
        DredDiagnostics::Collect(device.Get(), S_OK);
        assert(!std::filesystem::exists(directory / "DRED.log"));
        device->RemoveDevice(); // Only this offline WARP device; no application or hardware reset.
        DredDiagnostics::Collect(device.Get(), device->GetDeviceRemovedReason());
        assert(std::filesystem::exists(directory / "DRED.log"));
        const auto size = std::filesystem::file_size(directory / "DRED.log");
        DredDiagnostics::Collect(device.Get(), device->GetDeviceRemovedReason());
        assert(size == std::filesystem::file_size(directory / "DRED.log"));
        std::ifstream file(directory / "DRED.log");
        const std::string contents((std::istreambuf_iterator<char>(file)), {});
        assert(contents.find("kind=report-end") != std::string::npos);
    }
    else if (mode == "dred-unavailable")
    {
        DredDiagnostics::Configure(nullptr);
        assert(!DredDiagnostics::configured);
        std::ifstream file(directory / "FG-LIFECYCLE.log");
        const std::string contents((std::istreambuf_iterator<char>(file)), {});
        assert(contents.find("configured=false") != std::string::npos);
    }
    else
    {
        FgLifecycle::Instances instances;
        assert(instances.Create(7, false) == 0 && instances.Read().generation == 0);
        const auto first = instances.Create(7, true);
        const auto claim = instances.Read();
        instances.Release(7, first, false);
        assert(instances.Read().generation == claim.generation && instances.Find(7) == first);
        instances.Release(7, first, true);
        const auto replacement = instances.Create(7, true);
        assert(replacement != first && instances.Read().generation > claim.generation);
        // A delayed completion from an earlier release cannot remove a reused handle.
        instances.Release(7, first, true);
        assert(instances.Find(7) == replacement);
        const auto other = instances.Create(8, true);
        assert(instances.Read().instance == 0 && instances.Read().active == 2);
        instances.Release(7, replacement, true);
        assert(instances.Read().instance == other);
        instances.Create(8, true); // Earlier free slot must not duplicate the still-live handle.
        assert(instances.Read().active == 1);
        PreFg::PublishProvider(true, true);
        const auto providerGeneration = PreFg::Provider().generation;
        NR_FG_EVENT("capture-start", "test=true");
        const auto op = FgLifecycle::Begin("fg-create-begin");
        FgLifecycle::Created(op, 7, 1, true, nullptr);
        FgLifecycle::Options(1, 0, 1, 1, 0, 0, 0, 0, providerGeneration);
        const auto before = FgLifecycle::Read();
        FgLifecycle::Released(2, 7, FgLifecycle::Find(7), 1, true);
        FgLifecycle::Created(3, 7, 1, true, nullptr);
        FgLifecycle::Options(4, 0, 1, 1, 0, 0, 0, 0, providerGeneration);
        assert(PreFg::Provider().generation == providerGeneration); // Diagnostics do not reset the renderer.
        assert(FgLifecycle::Read().generation != before.generation);
        // The provider reports a target of 41 while the fake queue completion stays 3.
        // This observer records the target and never waits/signals/advances completion.
        uint64_t completed = 3;
        FgLifecycle::Completion(0, 0, 3, reinterpret_cast<void*>(0x1234), 41, 2);
        assert(completed == 3);
        std::vector<std::thread> workers;
        for (unsigned i = 0; i < 4; ++i)
            workers.emplace_back([=] { for (unsigned n = 0; n < 50; ++n) {
                const auto operation = FgLifecycle::Begin("options-begin");
                const auto snapshot = FgLifecycle::Read();
                FgLifecycle::Present(snapshot, providerGeneration, n, nullptr, nullptr, false, S_OK);
                FgLifecycle::Options(operation, i, 1, 1, 0, 0, 0, 0, providerGeneration);
            } });
        for (auto& worker : workers) worker.join();
        assert(!FrameTrace::Current().capturing); // Journal works before NR enable.
        for (unsigned i = 0; i < FgLifecycle::Budget::limit + 100; ++i) NR_FG_EVENT("fill", "index={}", i);
        std::ifstream file(directory / "FG-LIFECYCLE.log");
        std::string line, last;
        uint64_t count = 0; unsigned options = 0;
        while (std::getline(file, line)) {
            ++count; last = line;
            assert(line.find(" seq=" + std::to_string(count) + " ") != std::string::npos);
            if (line.find("kind=options-end") != std::string::npos) ++options;
        }
        assert(count == FgLifecycle::Budget::limit && options >= 2);
        assert(last.find("kind=journal-ended reason=budget-exhausted") != std::string::npos);
    }
    std::cout << "PASS FG lifecycle: " << mode << '\n';
}
