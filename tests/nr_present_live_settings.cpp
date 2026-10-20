#include "NrConfigFixture.h"
#include "../OptiScaler/dlssnr/StartupReadiness.h"
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)
#ifndef READINESS_COMPARISON
#define READINESS_COMPARISON SamePresentReadinessConfiguration
#endif

int main()
{
    using namespace DlssNr::PreFg;
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Fence> fence;
    CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
    CHECK(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    CHECK(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    TestConfig config;
    config.state.SetEnabled(config.DlssNrEnabled, true);
    const auto initial = config.GetDlssNrConfigSnapshot();
    ReadinessIdentity key {};
    key.configuration = 1; key.model = 1; key.invalidation = 1;
    StartupReadiness ready;
    auto wait = std::make_shared<DlssNr::GpuSafety::ExternalWaitStatus>();
    CHECK(ready.Submit(key, fence.Get(), 1, 1, wait));

    // Slider movement must neither discard a pending proof nor invent completion.
    for (unsigned i = 0; i < 128; ++i)
    {
        config.DlssNrColourStrength = float(i) / 32.0f;
        const auto captured = config.GetDlssNrConfigSnapshot();
        CHECK(captured.READINESS_COMPARISON(initial));
        CHECK(!ready.Poll(key, device.Get(), i + 2));
        CHECK(ready.Pending());
    }
    ready.Presented(1, true);
    CHECK(SUCCEEDED(fence->Signal(1)));
    CHECK(!ready.Poll(key, device.Get(), 130)); // actual consumer wait still required
    wait->bound = wait->applied = true;
    CHECK(ready.Poll(key, device.Get(), 131));

    // Exercise both production comparison boundaries: before recording and after
    // submission, with another menu change between immutable snapshots.
    auto previous = initial;
    for (unsigned i = 0; i < 1000; ++i)
    {
        config.DlssNrColourStrength = float(i % 128) / 32.0f;
        config.DlssNrTransferStrength = float(i % 64) / 32.0f;
        config.DlssNrMaxRatio = 1.0f + float(i % 16);
        config.DlssNrApplyModel = (i % 2) != 0;
        const auto captured = config.GetDlssNrConfigSnapshot();
        CHECK(captured.READINESS_COMPARISON(previous));
        CHECK(ready.Poll(key, device.Get(), i + 132));
        config.DlssNrColourStrength = float((i + 1) % 128) / 32.0f;
        const auto current = config.GetDlssNrConfigSnapshot();
        CHECK(captured.READINESS_COMPARISON(current));
        CHECK(!captured.SameConfiguration(current)); // exact snapshots remain exact
        CHECK(captured.DlssNrColourStrength.value_or_default() == float(i % 128) / 32.0f);
        previous = captured;
    }

    const auto structural = [&](auto change) {
        auto changed = initial; change(changed);
        CHECK(!initial.READINESS_COMPARISON(changed));
    };
    structural([](auto& s) { s.DlssNrRoute = 1u; });
    structural([](auto& s) { s.DlssNrPresentInputPolicy = 0u; });
    structural([](auto& s) { s.DlssNrEnhancedResolution = 1u; });
    structural([](auto& s) { s.DlssNrWorkingScale = 0.5f; });
    structural([](auto& s) { s.DlssNrOverrideFgGuardrails = true; });
    structural([](auto& s) { s.DlssNrOverrideHdrGuardrails = true; });
    structural([](auto& s) { s.DlssNrMultipassEnabled = true; });
    structural([](auto& s) { s.DlssNrPreset = 1u; });
    structural([](auto& s) { s.DlssNrIntensity = 0.5f; });
    structural([](auto& s) { s.DlssNrPasses = 3u; });
    structural([](auto& s) { s.DlssNrSecondLayerWorkingScale = 0.5f; });
    structural([](auto& s) { s.DlssNrSecondLayerIntensity = 0.5f; });
    structural([](auto& s) { s.DlssNrExtraLayers[7].preset = 1u; });
    structural([](auto& s) { s.DlssNrExtraLayers[7].workingScale = 0.5f; });
    // All passes' pure composition edits keep the same resource/model certificate.
    auto composed = initial;
    composed.DlssNrSecondLayerColourStrength = 0.5f;
    composed.DlssNrSecondLayerTransferStrength = 0.3f;
    composed.DlssNrSecondLayerMaxRatio = 4.0f;
    composed.DlssNrSecondLayerApplyModel = false;
    for (auto& layer : composed.DlssNrExtraLayers)
    {
        layer.colourStrength = 0.5f;
        layer.transferStrength = 0.3f;
        layer.maxRatio = 4.0f;
        layer.applyModel = false;
    }
    CHECK(initial.READINESS_COMPARISON(composed));
    CHECK(!initial.SameConfiguration(composed));
    structural([](auto& s) { s.DlssNrLocalTone = 0.5f; });
    structural([](auto& s) { s.DlssNrNativeProtocol = true; });
    config.state.SetEnabled(config.DlssNrEnabled, false);
    CHECK(!initial.READINESS_COMPARISON(config.GetDlssNrConfigSnapshot()));
    config.state.SetEnabled(config.DlssNrEnabled, true);
    CHECK(!initial.READINESS_COMPARISON(config.GetDlssNrConfigSnapshot()));
    auto changedKey = key; ++changedKey.model;
    CHECK(!ready.Poll(changedKey, device.Get(), 2000));
    CHECK(!ready.Ready()); // real model replacement still revokes readiness
    CHECK(ready.Submit(key, fence.Get(), 1, 2001, wait));
    ready.Presented(2001, true);
    CHECK(ready.Poll(key, device.Get(), 2002));
    wait->Fail();
    CHECK(!ready.Poll(key, device.Get(), 2003));
    std::puts("PASS: 128 pending and 1000 ready live edits; immutable values, exact config equality, structural/model/consumer-failure invalidation retained (WARP; no provider)");
}
