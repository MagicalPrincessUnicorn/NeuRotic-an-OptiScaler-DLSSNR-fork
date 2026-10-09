#pragma once
#include "CharacterPipeline.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <mutex>
#include <atomic>
namespace Neurotic::Semantic::Character {
struct CaptureDiagnostics {bool available=false;unsigned format=0,flags=0,pending=0;HRESULT lastError=S_OK;const char* operation="none";};
// This scoped owner receives the backbuffer only from the existing pre-overlay
// swapchain queue. It owns every capture recording and never permits replay.
class CharacterCaptureDx12 {
    using MicrosoftPtr=Microsoft::WRL::ComPtr<ID3D12Device>;
    struct Slot {
        CaptureSlotState state;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        Microsoft::WRL::ComPtr<ID3D12Resource> thumbnail,readback,source;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 completion=0,bytes=0;
        CpuFrame metadata;
    };
    std::mutex mutex_;
    std::mutex submissionMutex_;
#ifdef CHARACTER_CAPTURE_TEST
    void(*preparationTestHook_)()=nullptr;
#endif
    std::atomic<std::uint64_t> admissionGeneration_{1};
    std::array<Slot,3> slots_;
    MicrosoftPtr device_;
    MicrosoftPtr requestedDevice_;
    unsigned requestedWidth_=0,requestedHeight_=0;
    HRESULT lastError_=S_OK;unsigned sourceFormat_=0,sourceFlags_=0;
    const char* errorOperation_="none";
    bool Failed(HRESULT result,const char* operation) noexcept {
        if(SUCCEEDED(result))return false;
        lastError_=result;errorOperation_=operation;return true;
    }
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    bool Initialize(ID3D12Device*);
    bool Prepare(Slot&,ID3D12Device*,unsigned,unsigned,DXGI_FORMAT);
    bool SubmitLocked(ID3D12CommandQueue*,ID3D12Resource*,CpuFrame);
public:
    CaptureDiagnostics TryDiagnostics();
    // Capture token is acquired BEFORE reading source metadata. Resize invalidation
    // serializes with submission, so an already-observed source cannot submit later.
    std::uint64_t AdmissionToken() const noexcept{return admissionGeneration_.load();}
    void InvalidateAdmission(){std::lock_guard lock(mutex_);++admissionGeneration_;}
    // Native evaluation excludes only command submission, never allocations,
    // readback mapping/copying or GPU retirement owned by the control thread.
    void InvalidateWorkAdmission(){std::lock_guard lock(submissionMutex_);++admissionGeneration_;}
#ifdef CHARACTER_CAPTURE_TEST
    void SetPreparationTestHook(void(*hook)()){preparationTestHook_=hook;}
#endif
    // Nonblocking admission. All resource allocation belongs to PrepareRequested.
    bool TrySubmit(ID3D12CommandQueue*,ID3D12Resource*,CpuFrame metadata);
    bool TrySubmitSwapchain(ID3D12CommandQueue*,IDXGISwapChain3*,CpuFrame metadata);
    // Native control owner only: all graphics allocations and PSO preparation.
    void PrepareRequested();
    // Control owner only. Map/row copy occurs after actual fence completion.
    bool TryTakeNewest(CpuFrame&);
    // Control owner/resize only: releases completed source references, retains pending.
    void PollRetirement();
    bool RetireSources(unsigned timeoutMs);
};
}
