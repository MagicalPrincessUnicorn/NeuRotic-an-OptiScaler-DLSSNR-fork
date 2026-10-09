#pragma once
#include "FlowMath.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <string>

namespace Neurotic::PreparedFlow
{
using Microsoft::WRL::ComPtr;
struct Completion
{
    ComPtr<ID3D12Fence> fence;
    std::uint64_t value = 0;
    bool Ready() const { return fence && Completed(fence->GetCompletedValue(), value); }
};
struct Options
{
    std::uint32_t width = 0, height = 0;
    Backend requested = Backend::Software;
    Encoding encoding = Encoding::Srgb;
    float minLuminance = 0, maxLuminance = 1000;
    float appearanceThreshold = 0.15f;
    float consistencyThreshold = 1.5f;
    std::uint32_t nvidiaCostThreshold = 128;
};
struct Input
{
    // An owned, immutable whole image. Keep any upstream resource rights in
    // ownership until producer and this session's completion have retired.
    ComPtr<ID3D12Resource> color;
    std::shared_ptr<void> ownership;
    Completion producer;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Pair pair;
    bool reset = false;
};
struct Output
{
    ComPtr<ID3D12Resource> motion; // Full-resolution RG16_FLOAT, current -> previous, pixels.
    ComPtr<ID3D12Resource> distrust; // Full-resolution R8_UNORM, 1 = distrust.
    Completion producer;
    Pair pair;
    Backend selected = Backend::Software;
    std::uint32_t sourceGrid = 8;
    bool explicitReset = false;
    bool sceneCut = false;
    bool warmup = false;
    // Keeps all backend/runtime aliases alive. Submit refuses until the caller
    // returns the output and all reader completion points actually finish.
    std::shared_ptr<void> ownership;
};
// Rejected means no new flow work was recorded or submitted. Unsafe retains
// the owner after a poisoned session or potentially issued work.
enum class SubmitDisposition { Rejected, Submitted, Unsafe };
class Session
{
    struct Impl;
    std::shared_ptr<Impl> impl_;
public:
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session();
    bool Initialize(ID3D12Device* device, const Options& options, std::string& reason);
    // Records and executes on a private queue, which waits the actual producer
    // fence. An asynchronous call return is never publication of ready output.
    bool Submit(const Input& input, std::string& reason);
    SubmitDisposition SubmitChecked(const Input& input, std::string& reason);
    bool Poll(Output& output, std::string& reason);
    bool Wait(Output& output, std::string& reason, unsigned timeoutMs=5000);
    // Register each actual consuming queue's post-use signal. CPU cancellation,
    // timeout, or dropping Output cannot substitute for completion. All copied
    // output leases must be destroyed before the next Submit/Close.
    bool AddReader(const Completion& reader, std::string& reason);
    bool ReturnOutput(std::string& reason);
    Backend Selected() const;
    bool Close(std::string& reason);
};
}
