#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <functional>
struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12Fence;

namespace DlssNr::NativeGuides {
enum class Outcome { Unavailable, Preparing, Delivered, Unsafe };
// Completed CPU-owned capture. Identity belongs to the acquisition owner, not
// to an inferred engine camera. Motion is generated from real capture pairs.
struct Capture {
    std::uint64_t adapterLuid=0,session=0,generation=0,capture=0;
    unsigned width=0,height=0;
    bool reset=true,depthInverted=false;
    std::span<const std::uint8_t> bgra;
    std::span<const float> depth;
    double deltaMs=16.667; // Measured capture spacing; first frame uses explicit policy.
};
Outcome Process(const Capture&,std::vector<std::uint8_t>& bgraOutput,std::string& reason);
// Exact submitted shared raw packet. The GPU waits for inputFence/value before
// touching pixels; the producer need not be CPU-complete. Buffers enter/leave COMMON.
// The Vulkan owner retains buffers/fences until this call and copyback complete.
struct ExportedGuides {
    std::uint64_t capture=0,previous=0,stream=0,generation=0;
    bool depthInverted=false,reset=true;
};
struct SharedCapture {
    ID3D12Resource *color=nullptr,*depth=nullptr,*output=nullptr;
    ID3D12Fence *inputFence=nullptr,*outputFence=nullptr;
    std::uint64_t value=0;
    unsigned depthBits=0;
    bool bgra=true;
    ID3D12Resource* generated=nullptr;
    bool* generatedReady=nullptr;
    // Native depth may differ from presentation size. Both zero means legacy
    // matching size; otherwise both dimensions describe the raw depth buffer.
    unsigned depthWidth=0,depthHeight=0;
    ID3D12Resource *guideDepth=nullptr,*guideMotion=nullptr;
    // Called after the actual export fence completes. The caller must finish
    // all guide reads before returning; Unsafe retains this processor owner.
    std::function<Outcome(const ExportedGuides&,std::string&)> consumeGuides;
};
// Borrowed device; serialized native owner remains alive for process lifetime.
ID3D12Device* SharedDevice(std::uint64_t adapterLuid,std::string& reason);
Outcome ProcessShared(const Capture&,const SharedCapture&,std::string& reason);
bool CanSwitchInput(std::string& reason);
}
