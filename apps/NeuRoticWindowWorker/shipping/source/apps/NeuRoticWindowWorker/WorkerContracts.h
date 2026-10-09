// NeuRotic standalone window worker. GPL-3.0; see ../../LICENSE.
#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>
#include "PerformanceMetrics.h"
#include "guidance/DepthPolicy.h"

namespace nrw {
namespace depth {struct DepthFrame;}
template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
struct FrameStamp {
    uint64_t session = 0, sequence = 0, timestampQpc = 0;
    uint32_t width = 0, height = 0;
    bool reset = false;
    uint64_t streamEpoch = 1, geometryEpoch = 1, configEpoch = 1;
};
struct CapturedFrame {
    FrameStamp stamp;
    ComPtr<ID3D11Texture2D> texture; // worker-owned snapshot, SDR BGRA8
    uint64_t acquiredQpc = 0; // copy completion; stamp keeps the compositor source clock
    bool pairedDepthOutput = false; // owned completed depth result may present its exact older colour
    std::shared_ptr<void> ownership; // retain pooled storage through the last consumer
};
struct WindowIdentity {
    uint64_t hwnd = 0, processCreation = 0;
    uint32_t pid = 0;
    std::wstring title, executable, windowClass;
    int32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
};
enum class GuideMode { Off, Shadow, Experimental };
struct GuidanceOptions {
    GuideMode mode = GuideMode::Off;
    std::wstring depthBundle; // explicit verified local bundle, never download
    bool depth = true, motion = true;
    depth::Settings dav2;
    std::wstring dav2Root;
};
struct GuideResult {
    FrameStamp stamp;
    uint32_t width = 0, height = 0;
    std::vector<float> depth, motionXY; // relative depth; current->previous pixel displacement
    bool depthCompleted = false, motionCompleted = false;
    bool estimated = true; // cannot be native engine metadata
    // Explicit prepared-source adapter only. Default remains relative inverse
    // depth. Raw device depth must be finite [0,1]; this does not certify cameras.
    bool rawDeviceDepth = false, rawDeviceDepthReversed = false;
    std::string depthProvider, motionProvider, reason;
};
inline bool PrepareDepthUpload(const GuideResult& guides,std::vector<float>& values,bool& inverted)
{
    if(guides.depth.empty()||!std::all_of(guides.depth.begin(),guides.depth.end(),[&](float v){
        return std::isfinite(v)&&v>=0&&(!guides.rawDeviceDepth||v<=1);}))return false;
    if(guides.rawDeviceDepth){values=guides.depth;inverted=guides.rawDeviceDepthReversed;return true;}
    const auto bounds=std::minmax_element(guides.depth.begin(),guides.depth.end());
    const double low=*bounds.first,span=double(*bounds.second)-low;
    values.clear();values.reserve(guides.depth.size());
    for(float v:guides.depth)values.push_back(span>1e-12?float((double(v)-low)/span):0.5f);
    inverted=true;return true;
}
struct NrOptions {
    std::wstring modelPath, forwarderPath;
    float transferStrength = 1.0f, colourStrength = 1.0f;
    uint32_t workWidth = 0, workHeight = 0;
    int modelStyle = 0; // provider enum: Standard=0, Natural=1, Cinematic=2
    uint32_t nrScalePercent = 100; // Native by default for existing protocol clients.
    GuideMode guides = GuideMode::Off;
    bool gpuTiming = true;
};
struct NrProcessingOptions {
    float transferStrength=1,colourStrength=1;
    uint32_t nrScalePercent=100;
    int modelStyle=0;
};
struct NrResult {
    FrameStamp stamp;
    uint32_t workWidth = 0, workHeight = 0;
    ComPtr<ID3D11Texture2D> texture; // independently owned completed BGRA8 output
    bool recorded = false, submitted = false, completed = false;
    bool estimatedGuidesUsed = false;
    uint64_t completionValue = 0; // actual private D12 completion, never capture sequence
    std::string reason;
    double milliseconds = 0;
    NrMeasurements measurements;
    std::shared_ptr<void> ownership; // retain completed output until presentation retires
};
struct CaptureStatistics {
    uint64_t arrivals=0, dequeued=0, superseded=0, presentCalls=0, foregroundHwnd=0;
    uint32_t foregroundPid=0, guiFlags=0;
    bool outputVisible=false;
    OptionalMs presentCallMs;
    std::string interactionReason="not-started";
};
struct CaptureOptions { WindowIdentity target; uint64_t session = 1; bool cursor = true; bool overlay = true; uint32_t controlHostPid = 0; };
struct SnapshotResult { std::wstring path; std::string error; };
class FrameProbe;
class CaptureHost {
public:
    CaptureHost(); ~CaptureHost();
    CaptureHost(const CaptureHost&) = delete;
    bool Start(const CaptureOptions&, std::string& reason);
    bool Next(CapturedFrame&, std::string& reason); // bounded poll; false may mean no new frame
    bool Present(const CapturedFrame&, ID3D11Texture2D* processed, float split,
                 std::string& reason, int stripes=0, SnapshotResult* snapshot=nullptr, int direction=0, std::shared_ptr<void> processedOwnership={}, FrameProbe* probe=nullptr); // original-side direction 0..7; split 0=processed, 1=original
    bool Pump(); // false on output close, Escape or source destruction
    bool Paused() const;
    HANDLE WakeHandle() const; // borrowed owner-thread handle; valid until Stop
    CaptureStatistics Statistics() const;
    bool CursorCaptured() const;
    FrameStamp ObservedIdentity() const; // current epochs only; not proof of a captured image
    uint64_t OutputEpoch() const;
    bool DisengageHotkeyAvailable() const;
    void Disengage(); // hide/release interaction before backend retirement; retain GPU resources
    void Stop();
    ID3D11Device* Device() const;
    ID3D11DeviceContext* Context() const;
    static std::vector<WindowIdentity> Enumerate();
    static bool Inspect(HWND, WindowIdentity&, std::string& reason);
    static bool Validate(const WindowIdentity&, std::string& reason);
private: struct Impl; std::unique_ptr<Impl> impl_;
#ifdef NRW_CAPTURE_TEST
    friend bool TestCaptureCompletionFailures();
#endif
};
class NrHost {
public:
    NrHost(); ~NrHost();
    NrHost(const NrHost&) = delete;
    bool Initialize(ID3D11Device*, ID3D11DeviceContext*, const NrOptions&, std::string& reason);
    bool Reconfigure(const NrProcessingOptions&,std::string& reason); // owner-thread completed-frame boundary; never changes model/device
    NrResult Process(const CapturedFrame&, const GuideResult* guides = nullptr, FrameProbe* probe=nullptr);
    void Stop();
    bool RequiresRestart() const;
private:
#ifdef NRW_NATIVE_TEST
    friend struct NrOwnedGpuFixture;
#endif
    struct Impl; std::unique_ptr<Impl> impl_; bool retained_ = false;
};
class GuidanceHost {
public:
    GuidanceHost(); ~GuidanceHost();
    GuidanceHost(const GuidanceHost&) = delete;
    bool Initialize(ID3D11Device*, ID3D11DeviceContext*, const GuidanceOptions&, std::string& reason);
    GuideResult Process(const CapturedFrame&);
    void OfferDepth(const CapturedFrame&,uint64_t sourceId);
    void ObserveDepthEpochs(const FrameStamp&,uint64_t sourceId,uint64_t configEpoch);
    bool PollDepth(depth::DepthFrame&,std::string&);
    bool DepthReady() const;
    bool DepthBusy() const;
    uint64_t DepthFresh() const;
    uint64_t DepthDropped() const;
    void Stop();
    bool RequiresRestart() const;
private: struct Impl; std::unique_ptr<Impl> impl_;
};
}
