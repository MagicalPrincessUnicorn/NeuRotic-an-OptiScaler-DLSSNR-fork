#pragma once
#include "NativeRecordingState.h"
#include "NativeRecordingInterfaces.h"
#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace Neurotic::D3D12
{
// A binding is evidence from one actual public outer->terminal setter call.
// External recording/snapshot ownership keeps the argument domains alive;
// nothing is stored on a COM object that could create a reference cycle.
class NativeWrapperHeapBindings
{
  public:
    struct Binding
    {
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> caller, terminal;
        std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>, 2> callerHeaps, terminalHeaps;
        UINT count = 0;
        std::uint64_t incarnation = 0;
        std::array<ID3D12DescriptorHeap*, 2> CallerArguments() const noexcept
        { return {callerHeaps[0].Get(), callerHeaps[1].Get()}; }
    };
    using Owner = std::shared_ptr<const Binding>;

  private:
    struct Registry
    {
        std::mutex mutex;
        std::unordered_map<ID3D12GraphicsCommandList*, Owner> current;
    };
    static Registry& Records()
    {
        // Hook owners are process retained. Do not release foreign objects from
        // a CRT static destructor under the loader lock. Real Close/Reset retire
        // their live recording entries during ordinary operation.
        static auto* records = new Registry;
        return *records;
    }

  public:
    class ForwardingCall
    {
        friend class NativeWrapperHeapBindings;
        inline static thread_local ForwardingCall* current = nullptr;
        ForwardingCall* prior_;
        std::shared_ptr<Binding> binding_;
        unsigned terminalCalls_ = 0;
        bool valid_ = false;

        void Terminal(NativeRecordingObserver& observer, ID3D12GraphicsCommandList* list,
                      UINT count, ID3D12DescriptorHeap* const* heaps) noexcept
        {
            if(!binding_ || list!=binding_->terminal.Get())return;
            // Count before ownership acquisition: reentrant foreign AddRef must
            // not silently replace the first dispatch with another one.
            if (++terminalCalls_ != 1 || !valid_ || count != binding_->count ||
                count > 2 || (count && !heaps)) { valid_ = false; return; }
            try
            {
                for (UINT index = 0; index < count; ++index)
                {
                    if (!heaps[index]) { valid_ = false; return; }
                    binding_->terminalHeaps[index] = heaps[index];
                }
                const auto incarnation = observer.HeapBindingEpoch(list);
                if (!incarnation || terminalCalls_ != 1)
                { valid_ = false; return; }
                binding_->incarnation = *incarnation;
            }
            catch (...) { valid_ = false; }
        }

      private:
        friend class NativeRecordingHooks;
        ForwardingCall(ID3D12GraphicsCommandList* caller, UINT count,
                       ID3D12DescriptorHeap* const* heaps) noexcept : prior_(current)
        {
            try
            {
                if (caller && count <= 2 && (!count || heaps))
                {
                    binding_ = std::make_shared<Binding>();
                    binding_->caller = caller;
                    const auto interfaces=NativeRecordingInterfaces::Resolve(caller);
                    if(FAILED(interfaces.result)||!interfaces.terminal) {
                        current=this;return;
                    }
                    binding_->terminal=interfaces.terminal;
                    binding_->count = count;
                    valid_ = true;
                    for (UINT index = 0; index < count; ++index)
                    {
                        if (!heaps[index]) { valid_ = false; break; }
                        binding_->callerHeaps[index] = heaps[index];
                    }
                }
            }
            catch (...) { valid_ = false; }
            current = this;
        }
        ForwardingCall(const ForwardingCall&) = delete;
        ForwardingCall& operator=(const ForwardingCall&) = delete;
        ~ForwardingCall() { current = prior_; }

        // Call only after the outer original returned normally, including its
        // own cache updates/callbacks. An exceptional return cannot grant proof.
        void Complete(NativeRecordingObserver& observer) noexcept
        {
            if (!valid_ || terminalCalls_ != 1 || !binding_ || !binding_->terminal) return;
            try
            {
                const auto incarnation = observer.HeapBindingEpoch(binding_->terminal.Get());
                if (!incarnation || *incarnation != binding_->incarnation) return;
                Owner retired;
                {
                    auto& records = Records();
                    std::lock_guard lock(records.mutex);
                    auto entry = records.current.try_emplace(binding_->terminal.Get()).first;
                    retired = std::move(entry->second);
                    entry->second = binding_;
                }
                // Last foreign Release occurs after the tracker lock.
            }
            catch (...) {}
        }
    };

  private:
    friend class NativeRecordingHooks;
    static void ObserveTerminal(NativeRecordingObserver& observer, ID3D12GraphicsCommandList* list,
                                UINT count, ID3D12DescriptorHeap* const* heaps) noexcept
    {
        // Multiple wrappers of the same hooked implementation can nest. Each
        // live outer call records its own original-domain arguments.
        for (auto* call = ForwardingCall::current; call; call = call->prior_)
            call->Terminal(observer, list, count, heaps);
    }
  public:
    static Owner Find(ID3D12GraphicsCommandList* caller, ID3D12GraphicsCommandList* terminal,
                      std::uint64_t incarnation, UINT count, ID3D12DescriptorHeap* const* heaps)
    {
        Owner binding;
        {
            auto& records = Records();
            std::lock_guard lock(records.mutex);
            const auto entry = records.current.find(terminal);
            if (entry != records.current.end()) binding = entry->second;
        }
        if (!binding || binding->caller.Get() != caller || binding->terminal.Get() != terminal ||
            binding->incarnation != incarnation || binding->count != count || count > 2 ||
            (count && !heaps)) return {};
        for (UINT index = 0; index < count; ++index)
            if (binding->terminalHeaps[index].Get() != heaps[index]) return {};
        return binding;
    }
    static void Retire(ID3D12GraphicsCommandList* terminal)
    {
        Owner retired;
        {
            auto& records = Records();
            std::lock_guard lock(records.mutex);
            const auto entry = records.current.find(terminal);
            if (entry != records.current.end())
            { retired = std::move(entry->second); records.current.erase(entry); }
        }
    }
    static auto RetireAll()
    {
        decltype(Records().current) retired;
        auto& records=Records();
        { std::lock_guard lock(records.mutex);retired.swap(records.current); }
        return retired;
    }
};
}
