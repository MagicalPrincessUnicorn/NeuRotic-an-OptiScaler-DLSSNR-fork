#pragma once
#include "CharacterPipeline.h"
#include <dlssnr/NrGpuSafety.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <mutex>

namespace Neurotic::Semantic::Character {
// Samples the original NGX Color on its exact caller recording. The caller
// supplies the NGX NON_PIXEL_SHADER_RESOURCE contract and a full color region.
// GPU work is retained until completion AND real caller Reset/destruction.
class CharacterEarlyCaptureDx12 {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Backing {
        Ptr<ID3D12Device> device;
        Ptr<ID3D12RootSignature> root;
        Ptr<ID3D12PipelineState> pipeline;
        Ptr<ID3D12DescriptorHeap> heap;
        Ptr<ID3D12Resource> thumbnail,readback,source;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 bytes=0;
        unsigned width=0,height=0;
        // No recording ticket here: the ticket retains this backing.
    };
    struct Slot {
        std::shared_ptr<Backing> backing;
        DlssNr::GpuSafety::Ticket ticket;
        CpuFrame metadata;
        bool recorded=false,complete=false;
    };
    std::mutex mutex_;
    std::mutex recordingMutex_;
    std::array<Slot,3> slots_;
    Ptr<ID3D12Device> requestedDevice_;
    unsigned requestedWidth_=0,requestedHeight_=0;
    std::atomic<std::uint64_t> admissionGeneration_{1};
    std::atomic<bool> restorationFailed_{false};
    std::atomic<const char*> reason_{"Waiting for an NGX Color input"};
    std::shared_ptr<Backing> Prepare(ID3D12Device*,unsigned,unsigned);
    void PollLocked();
public:
    std::uint64_t AdmissionToken()const noexcept{return admissionGeneration_.load();}
    void InvalidateAdmission(){std::lock_guard lock(mutex_);++admissionGeneration_;}
    void InvalidateWorkAdmission(){std::lock_guard lock(recordingMutex_);++admissionGeneration_;}
    // A post-record restoration failure is fatal to this caller evaluation,
    // unlike optional source/state/slot rejection before recording starts.
    bool RestorationFailed()const noexcept{return restorationFailed_.load();}
    const char* Reason()const noexcept{return reason_.load();}
    bool TryRecord(ID3D12GraphicsCommandList*,ID3D12Resource*,CpuFrame metadata);
    void PrepareRequested();
    bool TryTakeNewest(CpuFrame&);
    bool RetireSources(unsigned timeoutMs);
};
}
