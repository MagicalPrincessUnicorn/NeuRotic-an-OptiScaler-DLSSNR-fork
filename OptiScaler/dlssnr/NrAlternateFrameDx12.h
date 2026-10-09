#pragma once
#include "NrAlternateFrameInputs.h"
#include "NrGpuSafety.h"
#include "NativeDispatchOutcome.h"
#include <d3d12.h>
#include <memory>

namespace DlssNr::AlternateFrame
{
struct GpuRequest {
    ID3D12GraphicsCommandList* list=nullptr;
    ID3D12CommandQueue* queue=nullptr;
    ID3D12Resource* base=nullptr;
    ID3D12Resource* target=nullptr;
    ID3D12Resource* depth=nullptr;
    ID3D12Resource* motion=nullptr;
    D3D12_RESOURCE_STATES baseState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES targetState=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES depthState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES motionState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    NativeSourceView source;
};
struct GpuRecordResult {bool recorded=false,possibleEffects=false;HRESULT result=E_FAIL;std::optional<NativeDispatchOutcome> output;};
struct GpuMeasurement {
    SourceRef source;
    std::uint64_t association=0;
    bool carry=false;
    bool budgetKnown=false;
    std::array<std::uint32_t,6> pixels{};
    std::optional<double> milliseconds;
};
class PreparedFrame {
    friend class Renderer;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit PreparedFrame(std::unique_ptr<Impl>);
  public:
    ~PreparedFrame();
    PreparedFrame(const PreparedFrame&)=delete;
    PreparedFrame& operator=(const PreparedFrame&)=delete;
};
class Renderer {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    explicit Renderer(ID3D12Device*);
    ~Renderer();
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    std::unique_ptr<PreparedFrame> PrepareCapture(const GpuRequest&);
    std::unique_ptr<PreparedFrame> PrepareCarry(const GpuRequest&);
    GpuRecordResult RecordCapture(PreparedFrame&);
    GpuRecordResult RecordCarry(PreparedFrame&);
    // Output acceptance comes from the caller's current-output transaction.
    // Recording alone cannot publish an anchor as accepted output.
    bool AcceptCapture(const SourceRef&);
    const AnchorMetadata* Anchor()const noexcept;
    Reason LastReason()const noexcept;
    std::optional<GpuMeasurement> Poll();
    void Invalidate();
};
}
