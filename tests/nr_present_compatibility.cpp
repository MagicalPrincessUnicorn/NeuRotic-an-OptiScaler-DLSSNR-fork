#include "dlssnr/DlssNr_PresentCompatibility.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace DlssNr::PresentCompatibility;

static void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static Capabilities Baseline(Api api, bool tenBit = false)
{
    Capabilities value {};
    value.api = api;
    value.directQueue = true;
    value.singleSample = true;
    value.flipModel = true;
    value.sdr = true;
    value.texture2D = true;
    value.nonZeroExtent = true;
    value.sameDevice = true;
    value.rgba8 = !tenBit;
    value.rgb10 = tenBit;
    value.rgba8ShaderLoad = true;
    value.rgba8TypedStore = true;
    value.targetShaderLoad = true;
    value.targetTypedStore = true;
    value.sharedResources = true;
    value.synchronization = true;
    return value;
}

struct SimulatedFrame
{
    int modelWork = 0;
    int outputWrites = 0;
};

static void RunIfAdmitted(const Capabilities& value, SimulatedFrame& frame)
{
    if (!Admit(value).supported)
        return;
    ++frame.modelWork;
    ++frame.outputWrites;
}

int main()
{
    auto d12Rgba8 = Admit(Baseline(Api::D3D12));
    Check(d12Rgba8.supported && d12Rgba8.path == PixelPath::Rgba8Direct,
          "D3D12 RGBA8 SDR keeps the direct path");
    auto d12Rgb10 = Admit(Baseline(Api::D3D12, true));
    Check(d12Rgb10.supported && d12Rgb10.path == PixelPath::Rgb10Conversion,
          "D3D12 R10G10B10A2 SDR uses conversion");
    auto d11Rgba8 = Admit(Baseline(Api::D3D11));
    Check(d11Rgba8.supported && d11Rgba8.path == PixelPath::Rgba8Direct,
          "D3D11 RGBA8 SDR uses the shared bridge");
    auto d11Rgb10 = Admit(Baseline(Api::D3D11, true));
    Check(d11Rgb10.supported && d11Rgb10.path == PixelPath::Rgb10Conversion,
          "D3D11 10-bit SDR combines sharing and conversion");

    auto CheckRejectedUntouched = [](Capabilities value, const char* expected, const char* description)
    {
        const auto result = Admit(value);
        Check(!result.supported && std::string_view(result.reason) == expected, description);
        SimulatedFrame frame;
        RunIfAdmitted(value, frame);
        Check(frame.modelWork == 0 && frame.outputWrites == 0,
              "failed admission leaves output untouched and performs zero model work");
    };

    auto value = Baseline(Api::D3D12);
    value.sdr = false;
    CheckRejectedUntouched(value, "HDR or non-SDR color space is unsupported", "HDR fails closed");
    value = Baseline(Api::D3D12);
    value.singleSample = false;
    CheckRejectedUntouched(value, "target is not single-sample flip-model", "MSAA fails closed");
    value = Baseline(Api::D3D12);
    value.flipModel = false;
    CheckRejectedUntouched(value, "target is not single-sample flip-model", "blit-model fails closed");
    value = Baseline(Api::D3D12);
    value.texture2D = false;
    CheckRejectedUntouched(value, "target shape is unsupported", "non-texture target fails closed");
    value = Baseline(Api::D3D12);
    value.nonZeroExtent = false;
    CheckRejectedUntouched(value, "target shape is unsupported", "empty target fails closed");
    value = Baseline(Api::D3D12);
    value.directQueue = false;
    CheckRejectedUntouched(value, "a direct D3D12 queue is unavailable", "missing direct queue fails closed");
    value = Baseline(Api::D3D12);
    value.rgba8 = false;
    CheckRejectedUntouched(value, "Present target format is unsupported", "unknown format fails closed");
    value = Baseline(Api::D3D12, true);
    value.targetTypedStore = false;
    CheckRejectedUntouched(value, "10-bit conversion capabilities are unavailable",
                           "missing conversion capability fails closed");
    value = Baseline(Api::D3D12, true);
    value.targetShaderLoad = false;
    CheckRejectedUntouched(value, "10-bit conversion capabilities are unavailable",
                           "missing conversion shader-load support fails closed");
    value = Baseline(Api::D3D12);
    value.rgba8ShaderLoad = false;
    CheckRejectedUntouched(value, "RGBA8 model surface capabilities are unavailable",
                           "missing model shader-load support fails closed");
    value = Baseline(Api::D3D12);
    value.rgba8TypedStore = false;
    CheckRejectedUntouched(value, "RGBA8 model surface capabilities are unavailable",
                           "missing model UAV store support fails closed");
    value = Baseline(Api::D3D11);
    value.sharedResources = false;
    CheckRejectedUntouched(value, "D3D11 shared-resource synchronization is unavailable",
                           "missing shared resources fail closed");
    value = Baseline(Api::D3D11);
    value.synchronization = false;
    CheckRejectedUntouched(value, "D3D11 shared-resource synchronization is unavailable",
                           "missing bridge fence support fails closed");
    value = Baseline(Api::D3D11);
    value.sameDevice = false;
    CheckRejectedUntouched(value, "Present target and NR queue use different devices",
                           "device mismatch fails closed");

    std::cout << "PASS: Present capability admission, D3D12 RGBA8/R10, D3D11 shared paths, and fail-closed invariants\n";
}
