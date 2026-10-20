#pragma once
#include <d3d12.h>
#include "NativeReplayFailure.h"
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace Neurotic::D3D12
{
// Validate effects observed at the actual terminal hook while dispatching a
// public outer setter. Pointer equality alone cannot validate array arguments.
class NativeReplayEffects
{
    struct Witness
    {
        unsigned slot = 0, rawSlot = 0;
        std::vector<std::uint64_t> values;
        bool valid = false;
    };
    static bool Copy(void* destination, const void* source, size_t bytes) noexcept
    {
        if (!bytes) return true;
        if (!source) return false;
        __try { std::memcpy(destination, source, bytes); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    template<class T> static std::uint64_t Address(T* object) noexcept
    { return reinterpret_cast<std::uintptr_t>(object); }
    template<unsigned Slot, class... A> static Witness Read(A... arguments)
    {
        Witness result;
        result.rawSlot = Slot;
        result.slot = Slot == 33 ? 35 : Slot == 34 ? 36 : Slot;
        const auto values = std::forward_as_tuple(arguments...);
        if constexpr (Slot == 25 || Slot == 29 || Slot == 30 || Slot == 75)
            result.values = {Address(std::get<0>(values))};
        else if constexpr (Slot == 23)
        {
            // NULL has the documented default {1,1,1,1} effect. Preserve every
            // non-null float's raw bits, including signed zero and NaN payloads.
            std::array<std::uint32_t, 4> bits{0x3f800000,0x3f800000,0x3f800000,0x3f800000};
            if (std::get<0>(values) && !Copy(bits.data(), std::get<0>(values), sizeof(bits))) return result;
            for (auto bit : bits) result.values.push_back(bit);
        }
        else if constexpr (Slot == 82)
            result.values={std::bit_cast<std::uint32_t>(std::get<0>(values)),
                std::bit_cast<std::uint32_t>(std::get<1>(values)),std::bit_cast<std::uint32_t>(std::get<2>(values))};
        else if constexpr (Slot == 28)
        {
            const UINT count = std::get<0>(values);
            if (count > 2) return result; // D3D12 binds at most two heap types.
            std::array<ID3D12DescriptorHeap*, 2> heaps{};
            if (!Copy(heaps.data(), std::get<1>(values), count * sizeof(heaps[0]))) return result;
            result.values.push_back(count);
            for (UINT index = 0; index < count; ++index) result.values.push_back(Address(heaps[index]));
        }
        else if constexpr (Slot == 31 || Slot == 32)
            result.values = {std::get<0>(values), std::get<1>(values).ptr};
        else if constexpr (Slot == 33 || Slot == 34)
            result.values = {std::get<0>(values), 1, std::get<2>(values), std::get<1>(values)};
        else if constexpr (Slot == 35 || Slot == 36)
        {
            const UINT count = std::get<1>(values);
            if (count > 64) return result; // Actual root-signature DWORD budget.
            std::array<UINT,64> data{};
            if (!Copy(data.data(), std::get<2>(values), count * sizeof(UINT))) return result;
            result.values = {std::get<0>(values), count, std::get<3>(values)};
            for (UINT index = 0; index < count; ++index) result.values.push_back(data[index]);
        }
        else if constexpr (Slot >= 37 && Slot <= 42)
            result.values = {std::get<0>(values), std::get<1>(values)};
        else if constexpr (Slot == 26)
        {
            const UINT count = std::get<0>(values);
            // Restore emits one inverse transition per call. A different count
            // cannot match that expected effect; normal host calls stay intact.
            if (count > 1) return result;
            result.values.push_back(count);
            if (count)
            {
                D3D12_RESOURCE_BARRIER barrier{};
                if (!Copy(&barrier, std::get<1>(values), sizeof(barrier))) return result;
                result.values.push_back(barrier.Type); result.values.push_back(barrier.Flags);
                if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
                {
                    result.values.push_back(Address(barrier.Transition.pResource));
                    result.values.push_back(barrier.Transition.Subresource);
                    result.values.push_back(barrier.Transition.StateBefore);
                    result.values.push_back(barrier.Transition.StateAfter);
                }
                else if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING)
                {
                    result.values.push_back(Address(barrier.Aliasing.pResourceBefore));
                    result.values.push_back(Address(barrier.Aliasing.pResourceAfter));
                }
                else if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
                    result.values.push_back(Address(barrier.UAV.pResource));
                else return result;
            }
        }
        else return result;
        result.valid = true;
        return result;
    }
    struct Frame
    {
        inline static thread_local Frame* current = nullptr;
        Frame* prior;
        ID3D12GraphicsCommandList* list;
        Witness expected;
        std::vector<bool> constants;
        unsigned calls = 0;
        unsigned failedSlot = UINT32_MAX;
        bool invalid = false;
        const char* failureReason = nullptr;
        Frame(ID3D12GraphicsCommandList* object, Witness effect)
            : prior(current), list(object), expected(std::move(effect))
        {
            if (!expected.valid) throw std::runtime_error("native replay arguments unavailable");
            if (expected.slot == 35 || expected.slot == 36) constants.resize(expected.values[1]);
            current = this;
        }
        ~Frame() { current = prior; }
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;
        bool Reject(unsigned slot) noexcept
        {
            invalid = true;
            if (failedSlot == UINT32_MAX) failedSlot = slot;
            return false;
        }
        bool Observe(const Witness& effect)
        {
            // After a lifecycle/extra-effect failure, actual setters belong to
            // the current genuine epoch and must be journaled normally.
            if (invalid) return false;
            ++calls;
            if (!effect.valid || effect.slot != expected.slot) return Reject(effect.rawSlot);
            if (expected.slot != 35 && expected.slot != 36)
            {
                if (calls != 1 || effect.values != expected.values) return Reject(effect.rawSlot);
                return true;
            }
            // A legal plural setter may forward as several single-value calls.
            // Accept the exact expected range once, never duplicate/extra data.
            const auto count = effect.values[1], offset = effect.values[2];
            const auto begin = expected.values[2], total = expected.values[1];
            if (effect.values[0] != expected.values[0] || offset < begin ||
                count > total || offset - begin > total - count || (!count && total))
                return Reject(effect.rawSlot);
            if (!total) return calls == 1 || Reject(effect.rawSlot);
            for (size_t index = 0; index < count; ++index)
            {
                const size_t target = static_cast<size_t>(offset - begin) + index;
                if (constants[target] || effect.values[3 + index] != expected.values[3 + target])
                    return Reject(effect.rawSlot);
            }
            for (size_t index = 0; index < count; ++index) constants[static_cast<size_t>(offset - begin) + index] = true;
            return true;
        }
        bool Complete() const
        {
            if (invalid || !calls) return false;
            for (bool written : constants) if (!written) return false;
            return true;
        }
    };
  public:
    // Only a matching real terminal effect may be excluded from journaling.
    // Independent command lists still follow their ordinary observation path.
    template<unsigned Slot, class... A> static bool Observe(ID3D12GraphicsCommandList* list, A... arguments) noexcept
    {
        auto* frame = Frame::current;
        if (!frame || frame->list != list) return false;
        try { return frame->Observe(Read<Slot>(arguments...)); }
        catch (...) { frame->invalid = true; return false; }
    }
    static bool Contains(ID3D12GraphicsCommandList* list) noexcept
    { return Frame::current && Frame::current->list == list; }
    static void Lifecycle(ID3D12GraphicsCommandList* list, unsigned slot) noexcept
    {
        for(auto* frame=Frame::current;frame;frame=frame->prior)
            if(frame->list==list) {
                frame->Reject(slot);
                frame->failureReason="Native.Restore.ReplayLifecycle";
            }
    }
    static void InterfaceEscape(ID3D12GraphicsCommandList* list) noexcept
    {
        for(auto* frame=Frame::current;frame;frame=frame->prior)
            if(frame->list==list) {
                frame->Reject(0);
                frame->failureReason="Native.Restore.TerminalInterfaceEscape";
            }
    }
    template<unsigned Slot, class Action, class... A>
    static void Invoke(ID3D12GraphicsCommandList* terminal, Action&& dispatch, A... arguments)
    {
        Frame frame(terminal, Read<Slot>(arguments...));
        dispatch();
        if (!frame.Complete()) throw NativeReplayFailure{
            frame.failureReason ? frame.failureReason : frame.calls ? "Native.Restore.TerminalEffectChanged" : "Native.Restore.TerminalEffectMissing",
            frame.failedSlot != UINT32_MAX ? frame.failedSlot : Slot};
    }
    // The caller invokes a verified original trampoline and journals that exact
    // command itself. A direct original need not reenter a hook. If it does,
    // only the same complete terminal effect may be excluded a second time.
    template<unsigned Slot, class Action, class... A>
    static void InvokeOriginal(ID3D12GraphicsCommandList* terminal, Action&& dispatch, A... arguments)
    {
        Frame frame(terminal,Read<Slot>(arguments...));
        dispatch();
        if(frame.invalid || (frame.calls && !frame.Complete()))throw NativeReplayFailure{
            frame.failureReason ? frame.failureReason : "Native.OwnedCommand.TerminalEffectChanged",
            frame.failedSlot!=UINT32_MAX ? frame.failedSlot : Slot};
    }
};
}
