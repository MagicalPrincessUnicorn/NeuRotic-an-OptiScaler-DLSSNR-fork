#pragma once

#include "DlssNr_Common.h"
#include <dlssnr/NrGpuSafety.h>
#include <shaders/Shader_Dx12Utils.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <memory>
#include <new>

namespace DlssNr
{
// Serialized by the host's lifecycle/NR locks. Destruction is allowed only before use
// or after the existing shutdown drain. There is deliberately no live shrink operation.
class CompositionPool
{
  public:
    static constexpr unsigned int InitialCapacity = 48;
    static constexpr unsigned int GrowthChunk = 32;
    // Experimental ~16 MiB committed-upload budget plus descriptor/object overhead.
    // This counts slots, independently of the tracker's 256-recording bound.
    static constexpr unsigned int HardCap = 256;
    static constexpr unsigned int AdmissionSlots = 8;
    static constexpr unsigned int SlotsPerAdditionalPass = 4;
    static constexpr unsigned int MaxPasses = 10;
    static constexpr unsigned int MaxAdmissionSlots =
        AdmissionSlots + SlotsPerAdditionalPass * (MaxPasses - 1);
    static constexpr unsigned int RequiredSlots(unsigned int passes)
    {
        return AdmissionSlots + SlotsPerAdditionalPass * (std::clamp(passes, 1u, MaxPasses) - 1);
    }
    static constexpr unsigned int TwoLayerAdmissionSlots = AdmissionSlots + SlotsPerAdditionalPass;
    static constexpr unsigned int SrvCount = 5;
    static constexpr unsigned int UavCount = 2;

    struct Slot
    {
        FrameDescriptorHeap heap;
        Microsoft::WRL::ComPtr<ID3D12Resource> constants;
        GpuSafety::Ticket owner;
        Slot() = default;
        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;
        Slot(Slot&&) = delete;
        Slot& operator=(Slot&&) = delete;
    };

    enum class Admission { Accepted, Tracking, Capacity, Allocation, Device };
    static const char* AdmissionName(Admission value)
    {
        switch (value)
        {
        case Admission::Accepted: return "accepted";
        case Admission::Tracking: return "tracking";
        case Admission::Capacity: return "hard-cap";
        case Admission::Allocation: return "allocation";
        case Admission::Device: return "device";
        }
        return "unknown";
    }
    struct Counters
    {
        unsigned long long checks = 0, accepted = 0, trackingRejects = 0;
        unsigned long long capacityRejects = 0, allocationRejects = 0, deviceRejects = 0;
        unsigned long long allocationAttempts = 0, growthEvents = 0, allocationFailures = 0;
        unsigned long long rejectRun = 0, maxRejectRun = 0, consumed = 0;
        unsigned int highWater = 0;
        double growthMs = 0, worstGrowthMs = 0;
    };

    static bool CreateSlot(ID3D12Device* device, Slot& slot)
    {
        auto properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(DlssNrConstants));
        return SUCCEEDED(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc,
                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&slot.constants))) &&
               slot.heap.Initialize(device, SrvCount, UavCount, 1);
    }

    // Factory injection exercises the same transaction with real D3D12 resources in tests.
    template<class Factory = decltype(&CreateSlot)>
    bool Initialize(ID3D12Device* device, Factory create = &CreateSlot)
    {
        return _capacity != 0 || Grow(device, create);
    }

    template<class Factory = decltype(&CreateSlot)>
    Admission Begin(ID3D12Device* device, const GpuSafety::Ticket& ticket, Factory create = &CreateSlot,
                    unsigned int required = AdmissionSlots)
    {
        _remaining = 0;
        _reservation.reset();
        ++_counts.checks;
        if (!device || FAILED(device->GetDeviceRemovedReason())) return Reject(Admission::Device);
        if (!ticket) return Reject(Admission::Tracking);
        if (required < AdmissionSlots || required > MaxAdmissionSlots ||
            (required - AdmissionSlots) % SlotsPerAdditionalPass != 0)
            return Reject(Admission::Capacity);
        const auto state = GpuSafety::InspectSlots(&ticket, 1);
        if (state.registryFailed || state.failed) return Reject(Admission::Device);

        unsigned int free = CollectReusable();
        _counts.highWater = std::max(_counts.highWater, _capacity - free);
        if (free < required)
        {
            if (_capacity == HardCap) return Reject(Admission::Capacity);
            if (!Grow(device, create))
                return Reject(FAILED(device->GetDeviceRemovedReason()) ? Admission::Device : Admission::Allocation);
            free = CollectReusable();
        }
        if (free < required) return Reject(Admission::Capacity);
        _reservation = ticket;
        _remaining = required;
        _nextReserved = 0;
        _usedAtAdmission = _capacity - free;
        ++_counts.accepted;
        _counts.rejectRun = 0;
        return Admission::Accepted;
    }

    bool ReservedFor(const GpuSafety::Ticket& ticket) const
    {
        return ticket && _reservation == ticket && _remaining != 0;
    }

    Slot* Consume(const GpuSafety::Ticket& ticket)
    {
        if (!ReservedFor(ticket)) return nullptr;
        const auto state = GpuSafety::InspectSlots(&ticket, 1);
        if (state.registryFailed || state.failed) return nullptr;
        Slot* slot = _slots[_reserved[_nextReserved]].get();
        // Reset/seal permission is authoritative; a diagnostic snapshot is never used here.
        if (!GpuSafety::Reusable(slot->owner)) return nullptr;
        slot->owner = ticket;
        ++_nextReserved;
        --_remaining;
        ++_counts.consumed;
        _counts.highWater = std::max(_counts.highWater, _usedAtAdmission + _nextReserved);
        return slot;
    }

    unsigned int Capacity() const { return _capacity; }
    const Counters& Stats() const { return _counts; }
    GpuSafety::SlotSnapshot Snapshot() const
    {
        std::array<GpuSafety::Ticket, HardCap> owners;
        for (unsigned int i = 0; i < _capacity; ++i) owners[i] = _slots[i]->owner;
        return GpuSafety::InspectSlots(owners.data(), _capacity);
    }

  private:
    std::array<std::unique_ptr<Slot>, HardCap> _slots;
    std::array<unsigned int, MaxAdmissionSlots> _reserved {};
    unsigned int _capacity = 0, _remaining = 0, _nextReserved = 0, _usedAtAdmission = 0;
    GpuSafety::Ticket _reservation;
    Counters _counts;

    unsigned int CollectReusable()
    {
        unsigned int free = 0;
        for (unsigned int i = 0; i < _capacity; ++i)
            if (GpuSafety::Reusable(_slots[i]->owner))
            {
                if (free < MaxAdmissionSlots) _reserved[free] = i;
                ++free;
            }
        return free;
    }

    Admission Reject(Admission reason)
    {
        if (reason == Admission::Tracking) ++_counts.trackingRejects;
        if (reason == Admission::Capacity) ++_counts.capacityRejects;
        if (reason == Admission::Allocation) ++_counts.allocationRejects;
        if (reason == Admission::Device) ++_counts.deviceRejects;
        ++_counts.rejectRun;
        _counts.maxRejectRun = std::max(_counts.maxRejectRun, _counts.rejectRun);
        return reason;
    }

    template<class Factory> bool Grow(ID3D12Device* device, Factory create)
    {
        if (!device || _capacity == HardCap) return false;
        const auto start = std::chrono::steady_clock::now();
        ++_counts.allocationAttempts;
        const unsigned int count = _capacity == 0 ? InitialCapacity : std::min(GrowthChunk, HardCap - _capacity);
        std::array<std::unique_ptr<Slot>, InitialCapacity> staged;
        bool complete = true;
        try
        {
            for (unsigned int i = 0; i < count; ++i)
            {
                staged[i] = std::make_unique<Slot>();
                if (!create(device, *staged[i])) { complete = false; break; }
            }
        }
        catch (const std::bad_alloc&) { complete = false; }
        complete = complete && SUCCEEDED(device->GetDeviceRemovedReason());
        if (complete)
        {
            // No allocation or throwing operation after publication begins.
            for (unsigned int i = 0; i < count; ++i) _slots[_capacity + i] = std::move(staged[i]);
            _capacity += count;
            ++_counts.growthEvents;
        }
        else ++_counts.allocationFailures;
        // Include rollback destruction in the measured growth cost.
        for (auto& slot : staged) slot.reset();
        _counts.growthMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        _counts.worstGrowthMs = std::max(_counts.worstGrowthMs, _counts.growthMs);
        return complete;
    }
};
}
