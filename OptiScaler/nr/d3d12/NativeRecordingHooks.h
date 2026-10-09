#pragma once
#include "NativeRecordingState.h"
#include <detours/detours.h>
#include <tlhelp32.h>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <cstring>
#include <stdexcept>
#include <atomic>

namespace Neurotic::D3D12
{
enum class NativeRecordingHookRefusal : std::uint32_t
{
    None, InvalidList, Poisoned, OtherOwner, NativeIdentity, CommandListType,
    InterfaceQuery, InterfaceIdentity, InterfaceShape, DuplicateTarget, ModulePin,
    TransactionBegin, TransactionThread, ThreadSnapshot, ThreadEnumeration, ThreadOpen,
    Attach, Commit, CoverageAbsent, VtableMismatch, SlotMismatch, RouteBytesMismatch,
    PatchBytesMismatch, UnknownInterface, QueryIdentity, ResetFailed, Enrollment,
    ReplayLifecycle, ReplayEffect, UnsupportedEffect, StateObservationRejected
};
struct NativeRecordingHookDiagnostic
{
    std::uint64_t queriedList = 0, queriedVtable = 0, installedVtable = 0;
    std::uint64_t installAttempts = 0, installSuccesses = 0, closeCalls = 0;
    std::uint64_t resetCalls = 0, resetSuccesses = 0, enrollmentSuccesses = 0, queryInterfaceCalls = 0;
    std::uint64_t firstRefusalSequence = 0, droppedEvents = 0;
    std::uint64_t globalInstallAttempts = 0, globalInstallSuccesses = 0, lastInstallList = 0;
    std::uint64_t queriedResetTarget = 0, installedResetTarget = 0, resetPatchTarget = 0;
    std::uint32_t flags = 0; // 1 installed, 2 coverage active, 4 retained address record, 8 denied
    NativeRecordingHookRefusal firstRefusal = NativeRecordingHookRefusal::None;
    std::uint32_t failedSlot = UINT32_MAX, enrollmentStage = 0;
    std::int32_t firstResult = S_OK, lastResetResult = S_OK;
    GUID failedIid{};
    std::uint64_t expectedIdentity = 0, observedIdentity = 0, patchTarget = 0;
    std::array<unsigned char, 16> expectedBytes{}, observedBytes{};
};
// One installation adapter, feeding the existing recording owner. This exact
// code runs in the product and the provider-free WARP integration test.
class NativeRecordingHooks
{
#ifdef NATIVE_RECORDING_HOOK_TEST_ACCESS
    friend struct NativeRecordingHooksTestAccess;
    std::function<void()> beforeTransactionForTest_, afterSuspendForTest_;
    bool failAttachForTest_ = false;
#endif
    template<unsigned Slot, class Signature> struct Hook;
    template<unsigned Slot, class C, class R, class... A>
    struct Hook<Slot, R (STDMETHODCALLTYPE C::*)(A...)>
    {
        using Fn = R (STDMETHODCALLTYPE*)(C*, A...);
        inline static Fn original = nullptr;
        static R STDMETHODCALLTYPE Call(C* self, A... args)
        {
            auto* list = reinterpret_cast<ID3D12GraphicsCommandList*>(self);
            auto& owner = *instance_;
            CallbackLease lease(owner);
            if constexpr (std::is_void_v<R>)
            {
                owner.Before<Slot>(list, args...);
                ForwardTrace trace(owner, list, Slot);
                original(self, args...);
            }
            else
            {
                const auto result = original(self, args...);
                if constexpr (Slot == 0)
                {
                    auto values = std::forward_as_tuple(args...);
                    owner.Record(list, [](auto& d, auto) { ++d.queryInterfaceCalls; });
                    if (SUCCEEDED(result) && (!KnownIid(std::get<0>(values)) ||
                        !std::get<1>(values) ||
                        (std::get<0>(values) != __uuidof(IUnknown) &&
                         std::get<0>(values) != __uuidof(ID3D12Object) &&
                         std::get<0>(values) != __uuidof(ID3D12DeviceChild) &&
                         *std::get<1>(values) != list)))
                    {
                        owner.Refuse(list, !KnownIid(std::get<0>(values)) ? NativeRecordingHookRefusal::UnknownInterface : NativeRecordingHookRefusal::QueryIdentity,
                            result, Slot, std::get<0>(values), reinterpret_cast<std::uint64_t>(list),
                            std::get<1>(values) ? reinterpret_cast<std::uint64_t>(*std::get<1>(values)) : 0);
                        owner.Deny(list);
                    }
                }
                else if constexpr (Slot == 9)
                {
                    owner.Record(list, [](auto& d, auto) { ++d.closeCalls; });
                    if (replayDepth_) owner.Refuse(list, NativeRecordingHookRefusal::ReplayLifecycle, result, Slot);
                    if (replayDepth_) { owner.Deny(list); replayInvalid_ = true; }
                    else { owner.observer_.Close(list); owner.RetireForwarding(list); }
                }
                else if constexpr (Slot == 10)
                {
                    owner.Record(list, [result](auto& d, auto) { ++d.resetCalls; d.lastResetResult = result; if (SUCCEEDED(result)) ++d.resetSuccesses; });
                    if (FAILED(result)) owner.Refuse(list, NativeRecordingHookRefusal::ResetFailed, result, Slot);
                    if (replayDepth_) owner.Refuse(list, NativeRecordingHookRefusal::ReplayLifecycle, result, Slot);
                    if (replayDepth_) { owner.Deny(list); replayInvalid_ = true; return result; }
                    owner.observer_.Close(list);
                    owner.RetireForwarding(list);
                    auto** table = *reinterpret_cast<void***>(list);
                    if (SUCCEEDED(result) && owner.CheckShape(list) && owner.Matches(list, table))
                    {
                        auto values = std::forward_as_tuple(args...);
                        NativeRecordingResetDiagnostic diagnostic;
                        if (owner.observer_.ResetNative(list, std::get<1>(values), owner.InterfaceVersion(), &diagnostic, table))
                            owner.Record(list, [](auto& d, auto) { ++d.enrollmentSuccesses; });
                        else owner.Refuse(list, NativeRecordingHookRefusal::Enrollment, diagnostic.result, Slot,
                            diagnostic.iid, diagnostic.expectedIdentity, diagnostic.observedIdentity, nullptr, nullptr, nullptr, diagnostic.stage);
                    }
                }
                return result;
            }
        }
    };
    struct InstalledMethod
    {
        unsigned slot;
        PVOID* original;
        PVOID detour;
        PVOID target;
        PVOID patchTarget = nullptr;
        std::array<unsigned char, 16> patch{}, routePatch{};
    };
    inline static NativeRecordingHooks* instance_ = nullptr;
    std::atomic<unsigned> callbacks_ = 0;
    struct CallbackLease
    {
        NativeRecordingHooks& owner;
        explicit CallbackLease(NativeRecordingHooks& value) : owner(value) { ++owner.callbacks_; }
        ~CallbackLease() { --owner.callbacks_; }
    };
    struct ForwardRoute
    {
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> caller, callee;
        unsigned slot;
        std::uint64_t generation;
    };
    std::atomic<std::uint64_t> forwardingGeneration_ = 1;
    std::mutex forwardingMutex_;
    std::vector<ForwardRoute> forwarding_;
    struct ForwardTrace
    {
        inline static thread_local ForwardTrace* current = nullptr;
        ForwardTrace* prior;
        NativeRecordingHooks& owner;
        ID3D12GraphicsCommandList* list;
        unsigned slot;
        std::uint64_t generation;
        ForwardTrace(NativeRecordingHooks& o, ID3D12GraphicsCommandList* l, unsigned s)
            : prior(current), owner(o), list(l), slot(s), generation(o.forwardingGeneration_.load())
        {
            if (!replayDepth_ && prior && &prior->owner == &owner && prior->slot == slot && prior->list != list && prior->generation == generation && owner.coverage_.load())
            {
                std::lock_guard lock(owner.forwardingMutex_);
                if (generation != owner.forwardingGeneration_.load()) { current = this; return; }
                const auto found = std::find_if(owner.forwarding_.begin(), owner.forwarding_.end(), [&](const auto& route) {
                    return route.caller.Get() == prior->list && route.callee.Get() == list && route.slot == slot && route.generation == generation;
                });
                if (found == owner.forwarding_.end()) owner.forwarding_.push_back({prior->list, list, slot, generation});
            }
            current = this;
        }
        ~ForwardTrace() { current = prior; }
    };
    inline static thread_local unsigned replayDepth_ = 0;
    inline static thread_local bool replayInvalid_ = false;
    inline static thread_local unsigned replaySlot_ = 0;
    inline static thread_local ID3D12GraphicsCommandList* replayList_ = nullptr;
    template<unsigned Slot, class F, class... A> static void Replay(F original, A... args)
    {
        if (!replayDepth_)
        {
            replayInvalid_ = false; replaySlot_ = Slot;
            replayList_ = reinterpret_cast<ID3D12GraphicsCommandList*>(std::get<0>(std::forward_as_tuple(args...)));
        }
        ++replayDepth_;
        try { original(args...); }
        catch (...) { --replayDepth_; throw; }
        --replayDepth_;
        if (replayInvalid_) throw std::runtime_error("untracked effect during native state replay");
    }
    NativeRecordingObserver& observer_;
    std::recursive_mutex installationMutex_;
    mutable std::mutex deniedMutex_;
    mutable std::unordered_set<ID3D12GraphicsCommandList*> denied_;
    std::vector<InstalledMethod> methods_;
    void** vtable_ = nullptr;
    unsigned count_ = 0;
    struct Coverage
    {
        void** vtable;
        std::vector<InstalledMethod> methods;
    };
    std::atomic<std::shared_ptr<const Coverage>> coverage_;
    std::shared_ptr<const Coverage> retainedCoverage_;
    std::atomic<bool> installed_ = false;
    bool poisoned_ = false;

    // Bounded historical ADDRESS records, not COM lifetime or recording identity.
    // Never evict the first refusal. Saturation is reported explicitly. Querying
    // does not allocate a record, call COM, touch the observer, or grant coverage.
    mutable std::mutex diagnosticsMutex_;
    mutable std::array<NativeRecordingHookDiagnostic, 64> diagnostics_{};
    mutable std::uint64_t diagnosticSequence_ = 0, droppedEvents_ = 0;
    std::uint64_t globalInstallAttempts_ = 0, globalInstallSuccesses_ = 0, lastInstallList_ = 0;
    std::atomic<std::uint64_t> installedVtable_ = 0, installedResetTarget_ = 0, resetPatchTarget_ = 0;
    template<class F> void Record(ID3D12GraphicsCommandList* list, F&& update) const
    {
        if (!list) return;
        std::lock_guard lock(diagnosticsMutex_);
        const auto key = reinterpret_cast<std::uint64_t>(list);
        auto found = std::find_if(diagnostics_.begin(), diagnostics_.end(), [key](const auto& d) { return d.queriedList == key; });
        if (found == diagnostics_.end())
        {
            found = std::find_if(diagnostics_.begin(), diagnostics_.end(), [](const auto& d) { return !d.queriedList; });
            if (found == diagnostics_.end()) { ++droppedEvents_; return; }
            found->queriedList = key;
        }
        update(*found, ++diagnosticSequence_);
    }
    bool Refuse(ID3D12GraphicsCommandList* list, NativeRecordingHookRefusal reason, LONG result = S_OK,
                unsigned slot = UINT32_MAX, const GUID& iid = GUID{}, std::uint64_t expected = 0,
                std::uint64_t observed = 0, const InstalledMethod* method = nullptr,
                const unsigned char* expectedBytes = nullptr, const void* observedBytes = nullptr,
                unsigned enrollmentStage = 0) const
    {
        Record(list, [&](auto& d, auto sequence) {
            if (d.firstRefusal != NativeRecordingHookRefusal::None) return;
            d.firstRefusal = reason; d.firstResult = result; d.failedSlot = slot; d.failedIid = iid;
            d.firstRefusalSequence = sequence; d.expectedIdentity = expected; d.observedIdentity = observed;
            d.enrollmentStage = enrollmentStage;
            if (method) d.patchTarget = reinterpret_cast<std::uint64_t>(method->patchTarget);
            if (expectedBytes) std::memcpy(d.expectedBytes.data(), expectedBytes, d.expectedBytes.size());
            if (observedBytes) std::memcpy(d.observedBytes.data(), observedBytes, d.observedBytes.size());
        });
        return false;
    }

    static bool KnownIid(REFIID iid)
    {
        return iid == __uuidof(IUnknown) || iid == __uuidof(ID3D12Object) ||
            iid == __uuidof(ID3D12DeviceChild) || iid == __uuidof(ID3D12CommandList) ||
            iid == __uuidof(ID3D12GraphicsCommandList) ||
            iid == __uuidof(ID3D12GraphicsCommandList1) ||
            iid == __uuidof(ID3D12GraphicsCommandList2) ||
            iid == __uuidof(ID3D12GraphicsCommandList3) ||
            iid == __uuidof(ID3D12GraphicsCommandList4) ||
            iid == __uuidof(ID3D12GraphicsCommandList5) ||
            iid == __uuidof(ID3D12GraphicsCommandList6) ||
            iid == __uuidof(ID3D12GraphicsCommandList7) ||
            iid == __uuidof(ID3D12GraphicsCommandList8) ||
            iid == __uuidof(ID3D12GraphicsCommandList9) ||
            iid == __uuidof(ID3D12GraphicsCommandList10);
    }

    template<class T> bool Interface(ID3D12GraphicsCommandList* list, unsigned end, unsigned& count, void** expectedTable = nullptr) const
    {
        Microsoft::WRL::ComPtr<T> extended;
        const auto hr = list->QueryInterface(IID_PPV_ARGS(&extended));
        if (hr == E_NOINTERFACE) return true;
        if (FAILED(hr)) return Refuse(list, NativeRecordingHookRefusal::InterfaceQuery, hr, 0, __uuidof(T));
        if (reinterpret_cast<void*>(extended.Get()) != list)
            return Refuse(list, NativeRecordingHookRefusal::InterfaceIdentity, hr, 0, __uuidof(T),
                reinterpret_cast<std::uint64_t>(list), reinterpret_cast<std::uint64_t>(extended.Get()));
        if (!expectedTable) expectedTable = vtable_;
        if (*reinterpret_cast<void***>(extended.Get()) != expectedTable)
            return Refuse(list, NativeRecordingHookRefusal::VtableMismatch, hr, 0, __uuidof(T),
                reinterpret_cast<std::uint64_t>(expectedTable), reinterpret_cast<std::uint64_t>(*reinterpret_cast<void***>(extended.Get())));
        count = end;
        return true;
    }
    unsigned InterfaceVersion() const
    {
        constexpr unsigned ends[] = {60,66,67,68,77,79,80,81,82,84,86};
        for (unsigned i = 0; i < 11; ++i) if (ends[i] == count_) return i;
        return 0;
    }
    bool CheckShape(ID3D12GraphicsCommandList* list) const
    {
        unsigned count = 60;
        if (!list) return false;
        auto** table = *reinterpret_cast<void***>(list);
        if (!Interface<ID3D12GraphicsCommandList1>(list, 66, count, table)) return false;
        if (!Interface<ID3D12GraphicsCommandList2>(list, 67, count, table)) return false;
        if (!Interface<ID3D12GraphicsCommandList3>(list, 68, count, table)) return false;
        if (!Interface<ID3D12GraphicsCommandList4>(list, 77, count, table)) return false;
        if (!Interface<ID3D12GraphicsCommandList5>(list, 79, count, table)) return false;
        if (!Interface<ID3D12GraphicsCommandList6>(list, 80, count, table)) return false;
        if (!Interface<ID3D12GraphicsCommandList7>(list, 81, count, table)) return false;
        if (!Interface<ID3D12GraphicsCommandList8>(list, 82, count, table)) return false;
        if (!Interface<ID3D12GraphicsCommandList9>(list, 84, count, table)) return false;
        if (!Interface<ID3D12GraphicsCommandList10>(list, 86, count, table)) return false;
        if (*reinterpret_cast<void***>(list) != table) return Refuse(list, NativeRecordingHookRefusal::VtableMismatch,
            S_OK, UINT32_MAX, GUID{}, reinterpret_cast<std::uint64_t>(table), reinterpret_cast<std::uint64_t>(*reinterpret_cast<void***>(list)));
        return count == count_ || Refuse(list, NativeRecordingHookRefusal::InterfaceShape, S_OK, UINT32_MAX, GUID{}, count_, count);
    }
    // expectedTable belongs to a shape-qualified candidate or the COM-retained
    // recording identity. No QI here: the observer invokes this under its lock.
    bool Matches(ID3D12GraphicsCommandList* list, void** expectedTable) const
    {
        const auto coverage = coverage_.load();
        if (!list) return false;
        if (!coverage) return Refuse(list, NativeRecordingHookRefusal::CoverageAbsent);
        std::lock_guard lock(deniedMutex_);
        if (denied_.contains(list)) return false;
        bool match = true;
        const auto table = *reinterpret_cast<void***>(list);
        if (!expectedTable || table != expectedTable) match = Refuse(list, NativeRecordingHookRefusal::VtableMismatch, S_OK,
            UINT32_MAX, GUID{}, reinterpret_cast<std::uint64_t>(expectedTable), reinterpret_cast<std::uint64_t>(table));
        if (match && list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
            match = Refuse(list, NativeRecordingHookRefusal::CommandListType, S_OK, 8, GUID{}, D3D12_COMMAND_LIST_TYPE_DIRECT, list->GetType());
        for (const auto& method : coverage->methods)
        {
            if (!match) break;
            if (table[method.slot] != method.target)
                match = Refuse(list, NativeRecordingHookRefusal::SlotMismatch, S_OK, method.slot, GUID{},
                    reinterpret_cast<std::uint64_t>(method.target), reinterpret_cast<std::uint64_t>(table[method.slot]), &method);
            else if (std::memcmp(method.target, method.routePatch.data(), method.routePatch.size()) != 0)
                match = Refuse(list, NativeRecordingHookRefusal::RouteBytesMismatch, S_OK, method.slot, GUID{},
                    reinterpret_cast<std::uint64_t>(method.target), reinterpret_cast<std::uint64_t>(method.target), &method, method.routePatch.data(), method.target);
            else if (std::memcmp(method.patchTarget, method.patch.data(), method.patch.size()) != 0)
                match = Refuse(list, NativeRecordingHookRefusal::PatchBytesMismatch, S_OK, method.slot, GUID{},
                    reinterpret_cast<std::uint64_t>(method.patchTarget), reinterpret_cast<std::uint64_t>(method.patchTarget), &method, method.patch.data(), method.patchTarget);
        }
        if (!match) { denied_.insert(list); return false; }
        return true;
    }
    void RetireForwarding(ID3D12GraphicsCommandList* list)
    {
        std::lock_guard lock(forwardingMutex_);
        std::erase_if(forwarding_, [list](const auto& route) { return route.caller.Get() == list || route.callee.Get() == list; });
    }
    void Deny(ID3D12GraphicsCommandList* list)
    {
        { std::lock_guard lock(deniedMutex_); denied_.insert(list); }
        if (replayDepth_) { replayInvalid_ = true; return; }
        observer_.WithCurrent(list, [](auto& state, auto incarnation) { state.Taint(incarnation); return false; });
    }
    template<unsigned Slot, class... A> void Before(ID3D12GraphicsCommandList* list, A... args)
    {
        if (replayDepth_)
        {
            // Debug/runtime forwarding may reenter a covered setter during an
            // original replay. Any work or unsupported effect fails the replay.
            const bool sameSetter = Slot == replaySlot_ ||
                ((Slot == 33 || Slot == 35) && (replaySlot_ == 33 || replaySlot_ == 35)) ||
                ((Slot == 34 || Slot == 36) && (replaySlot_ == 34 || replaySlot_ == 36));
            bool sameIdentity = list == replayList_;
            if (!sameIdentity && sameSetter)
            {
                // The forwarding relationship was observed during an actual
                // outer setter call, and both native objects are retained.
                std::lock_guard lock(forwardingMutex_);
                sameIdentity = std::any_of(forwarding_.begin(), forwarding_.end(), [&](const auto& route) {
                    return route.caller.Get() == replayList_ && route.callee.Get() == list && route.slot == Slot && route.generation == forwardingGeneration_.load();
                });
            }
            if (!sameIdentity || !sameSetter) { Refuse(list, NativeRecordingHookRefusal::ReplayEffect, S_OK, Slot); Deny(list); replayInvalid_ = true; }

            return;
        }
        auto values = std::forward_as_tuple(args...);
        bool wasActive = false, unsupported = false;
        const bool accepted = observer_.WithCurrent(list, [&]([[maybe_unused]] auto& state, [[maybe_unused]] auto incarnation) {
            wasActive = state.Activity(list, incarnation).has_value();
            if constexpr (Slot == 25) return state.ObservePipeline(incarnation, std::get<0>(values));
            else if constexpr (Slot == 26) return state.ObserveBarriers(incarnation, std::get<0>(values), std::get<1>(values));
            else if constexpr (Slot == 28) return state.ObserveDescriptorHeaps(incarnation, std::get<0>(values), std::get<1>(values));
            else if constexpr (Slot == 29) return state.ObserveComputeRootSignature(incarnation, std::get<0>(values));
            else if constexpr (Slot == 30) return state.ObserveGraphicsRootSignature(incarnation, std::get<0>(values));
            else if constexpr (Slot == 31) return state.ObserveComputeTable(incarnation, std::get<0>(values), std::get<1>(values));
            else if constexpr (Slot == 32) return state.ObserveGraphicsTable(incarnation, std::get<0>(values), std::get<1>(values));
            else if constexpr (Slot == 33) return state.ObserveComputeConstants(incarnation, std::get<0>(values), 1, &std::get<1>(values), std::get<2>(values));
            else if constexpr (Slot == 34) return state.ObserveGraphicsConstants(incarnation, std::get<0>(values), 1, &std::get<1>(values), std::get<2>(values));
            else if constexpr (Slot == 35) return state.ObserveComputeConstants(incarnation, std::get<0>(values), std::get<1>(values), std::get<2>(values), std::get<3>(values));
            else if constexpr (Slot == 36) return state.ObserveGraphicsConstants(incarnation, std::get<0>(values), std::get<1>(values), std::get<2>(values), std::get<3>(values));
            else if constexpr (Slot == 37 || Slot == 39 || Slot == 41)
                return state.ObserveComputeDescriptor(incarnation, std::get<0>(values), Slot == 37 ? RootKind::CBV : Slot == 39 ? RootKind::SRV : RootKind::UAV, std::get<1>(values));
            else if constexpr (Slot == 38 || Slot == 40 || Slot == 42)
                return state.ObserveGraphicsDescriptor(incarnation, std::get<0>(values), Slot == 38 ? RootKind::CBV : Slot == 40 ? RootKind::SRV : RootKind::UAV, std::get<1>(values));
            else if constexpr ((Slot >= 12 && Slot <= 19) || (Slot >= 47 && Slot <= 50))
                return state.ObserveWork(incarnation);
            else if constexpr (Slot == 52 || Slot == 53)
            {
                return state.ObserveQuery(incarnation,std::get<0>(values),std::get<1>(values),std::get<2>(values),Slot == 52);
            }
            else if constexpr (Slot == 54)
                return state.ObserveQueryResolve(incarnation,std::get<0>(values),std::get<1>(values),std::get<2>(values),std::get<3>(values));
            else if constexpr ((Slot >= 20 && Slot <= 24) || (Slot >= 43 && Slot <= 46))
                return state.ObserveUnrestorableGraphics(incarnation);
            else if constexpr (Slot >= 56 && Slot <= 58) return true; // Diagnostic markers only.
            else
            { unsupported = true; state.Taint(incarnation); return false; } // Bundles, indirect, discard, predication, versioned effects.
        });
        // WithCurrent also returns false without an entry. Record only an
        // action on a previously active, untainted recording, after the
        // observer/state locks retire. Existing loss cannot identify this slot.
        if (wasActive && !accepted)
        {
            std::uint64_t observed = 0;
            if constexpr (Slot == 25 || Slot == 29 || Slot == 30)
                observed = reinterpret_cast<std::uint64_t>(std::get<0>(values));
            Refuse(list, unsupported ? NativeRecordingHookRefusal::UnsupportedEffect :
                NativeRecordingHookRefusal::StateObservationRejected, S_OK, Slot, GUID{}, 0, observed);
        }
        if constexpr (Slot == 26) if (std::get<0>(values)) observer_.Work(list);
    }
    template<unsigned Slot, class Signature> void Add()
    {
        if (Slot >= count_) return;
        using H = Hook<Slot, Signature>;
        H::original = reinterpret_cast<typename H::Fn>(vtable_[Slot]);
        methods_.push_back({Slot, reinterpret_cast<PVOID*>(&H::original), reinterpret_cast<PVOID>(&H::Call), vtable_[Slot]});
    }
    bool BeginTransaction(ID3D12GraphicsCommandList* list, std::vector<HANDLE>& threads, bool& poisoned)
    {
        auto result = DetourTransactionBegin();
        if (result != NO_ERROR) return Refuse(list, NativeRecordingHookRefusal::TransactionBegin, result);
        // DetourUpdateThread suspends threads. Do not acquire any application
        // mutex until abort/commit has resumed them: one may own that mutex.
        NativeRecordingHookRefusal failure = NativeRecordingHookRefusal::None;
        LONG failureResult = NO_ERROR;
        const auto failed = [&](NativeRecordingHookRefusal reason, LONG code) {
            if (failure == NativeRecordingHookRefusal::None) { failure = reason; failureResult = code; }
            return false;
        };
        result = DetourUpdateThread(GetCurrentThread());
        bool ok = result == NO_ERROR;
        if (!ok) failed(NativeRecordingHookRefusal::TransactionThread, result);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) ok = failed(NativeRecordingHookRefusal::ThreadSnapshot, GetLastError());
        else
        {
            THREADENTRY32 entry{sizeof(entry)};
            if (!Thread32First(snapshot, &entry)) ok = failed(NativeRecordingHookRefusal::ThreadEnumeration, GetLastError());
            else do
            {
                if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) continue;
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
                if (!thread) { ok = failed(NativeRecordingHookRefusal::ThreadOpen, GetLastError()); break; }
                threads.push_back(thread);
                result = DetourUpdateThread(thread);
                if (result != NO_ERROR) { ok = failed(NativeRecordingHookRefusal::TransactionThread, result); break; }
            } while (Thread32Next(snapshot, &entry));
            CloseHandle(snapshot);
        }
        if (!ok && DetourTransactionAbort() != NO_ERROR) poisoned = true;
        if (!ok) Refuse(list, failure, failureResult);
        return ok;
    }
    static void CloseThreads(std::vector<HANDLE>& threads) { for (auto thread : threads) CloseHandle(thread); }
  public:
    explicit NativeRecordingHooks(NativeRecordingObserver& observer) : observer_(observer) {}
    ~NativeRecordingHooks() = delete; // Installed callbacks and their owner are process-retained.
    NativeRecordingHooks(const NativeRecordingHooks&) = delete;
    NativeRecordingHooks& operator=(const NativeRecordingHooks&) = delete;
    NativeRecordingHookDiagnostic Diagnostic(ID3D12GraphicsCommandList* list) const
    {
        NativeRecordingHookDiagnostic result;
        {
            std::lock_guard lock(diagnosticsMutex_);
            const auto key = reinterpret_cast<std::uint64_t>(list);
            if (key) for (const auto& record : diagnostics_) if (record.queriedList == key)
            { result = record; result.flags |= 4; break; }
            result.queriedList = key;
            result.globalInstallAttempts = globalInstallAttempts_;
            result.globalInstallSuccesses = globalInstallSuccesses_;
            result.lastInstallList = lastInstallList_;
            result.droppedEvents = droppedEvents_;
        }
        // The caller supplies a live list, exactly as for the observation query.
        result.queriedVtable = list ? reinterpret_cast<std::uint64_t>(*reinterpret_cast<void***>(list)) : 0;
        result.queriedResetTarget = list ? reinterpret_cast<std::uint64_t>((*reinterpret_cast<void***>(list))[10]) : 0;
        result.installedVtable = installedVtable_.load();
        result.installedResetTarget = installedResetTarget_.load();
        result.resetPatchTarget = resetPatchTarget_.load();
        if (installed_.load()) result.flags |= 1;
        if (coverage_.load()) result.flags |= 2;
        { std::lock_guard lock(deniedMutex_); if (denied_.contains(list)) result.flags |= 8; }
        return result;
    }
    bool Install(ID3D12GraphicsCommandList* list)
    {
        std::lock_guard lock(installationMutex_);
        { std::lock_guard diagnosticLock(diagnosticsMutex_); ++globalInstallAttempts_; lastInstallList_ = reinterpret_cast<std::uint64_t>(list); }
        Record(list, [](auto& d, auto) { ++d.installAttempts; });
        const auto installed = [&] {
            { std::lock_guard diagnosticLock(diagnosticsMutex_); ++globalInstallSuccesses_; }
            Record(list, [](auto& d, auto) { ++d.installSuccesses; });
            return true;
        };
        if (installed_)
        {
            if (!list) return false;
            auto** table = *reinterpret_cast<void***>(list);
            if (coverage_.load()) return CheckShape(list) && Matches(list, table) && installed();
            ++forwardingGeneration_;
            { std::lock_guard routes(forwardingMutex_); forwarding_.clear(); }
            coverage_.store(retainedCoverage_);
            if (!CheckShape(list) || !Matches(list, table)) { coverage_.store({}); return false; }
            observer_.SetHookCoverage(true, [this](const auto& identity) {
                return identity.interfaceVersion == InterfaceVersion() && Matches(identity.nativeList.Get(), identity.commandTable);
            });
            return installed();
        }
        observer_.SetHookCoverage(false);
        if (!list) return false;
        if (poisoned_) return Refuse(list, NativeRecordingHookRefusal::Poisoned);
        if (instance_ && instance_ != this && instance_->installed_) return Refuse(list, NativeRecordingHookRefusal::OtherOwner);
        auto native = DlssNr::NativeIdentity::Resolve<ID3D12GraphicsCommandList>(list);
        if (!native.object || native.object.Get() != list) return Refuse(list, NativeRecordingHookRefusal::NativeIdentity,
            native.result, 0, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<std::uint64_t>(list), reinterpret_cast<std::uint64_t>(native.object.Get()));
        if (list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT) return Refuse(list, NativeRecordingHookRefusal::CommandListType,
            S_OK, 8, GUID{}, D3D12_COMMAND_LIST_TYPE_DIRECT, list->GetType());
        vtable_ = *reinterpret_cast<void***>(list);
        count_ = 60;
        if (!Interface<ID3D12GraphicsCommandList1>(list, 66, count_)) return false;
        if (!Interface<ID3D12GraphicsCommandList2>(list, 67, count_)) return false;
        if (!Interface<ID3D12GraphicsCommandList3>(list, 68, count_)) return false;
        if (!Interface<ID3D12GraphicsCommandList4>(list, 77, count_)) return false;
        if (!Interface<ID3D12GraphicsCommandList5>(list, 79, count_)) return false;
        if (!Interface<ID3D12GraphicsCommandList6>(list, 80, count_)) return false;
        if (!Interface<ID3D12GraphicsCommandList7>(list, 81, count_)) return false;
        if (!Interface<ID3D12GraphicsCommandList8>(list, 82, count_)) return false;
        if (!Interface<ID3D12GraphicsCommandList9>(list, 84, count_)) return false;
        if (!Interface<ID3D12GraphicsCommandList10>(list, 86, count_)) return false;
        methods_.clear();
        Add<0, HRESULT (STDMETHODCALLTYPE IUnknown::*)(REFIID, void**)>();
        Add<9, decltype(&ID3D12GraphicsCommandList10::Close)>();
        Add<10, decltype(&ID3D12GraphicsCommandList10::Reset)>();
        Add<11, decltype(&ID3D12GraphicsCommandList10::ClearState)>();
        Add<12, decltype(&ID3D12GraphicsCommandList10::DrawInstanced)>();
        Add<13, decltype(&ID3D12GraphicsCommandList10::DrawIndexedInstanced)>();
        Add<14, decltype(&ID3D12GraphicsCommandList10::Dispatch)>();
        Add<15, decltype(&ID3D12GraphicsCommandList10::CopyBufferRegion)>();
        Add<16, decltype(&ID3D12GraphicsCommandList10::CopyTextureRegion)>();
        Add<17, decltype(&ID3D12GraphicsCommandList10::CopyResource)>();
        Add<18, decltype(&ID3D12GraphicsCommandList10::CopyTiles)>();
        Add<19, decltype(&ID3D12GraphicsCommandList10::ResolveSubresource)>();
        Add<20, decltype(&ID3D12GraphicsCommandList10::IASetPrimitiveTopology)>();
        Add<21, decltype(&ID3D12GraphicsCommandList10::RSSetViewports)>();
        Add<22, decltype(&ID3D12GraphicsCommandList10::RSSetScissorRects)>();
        Add<23, decltype(&ID3D12GraphicsCommandList10::OMSetBlendFactor)>();
        Add<24, decltype(&ID3D12GraphicsCommandList10::OMSetStencilRef)>();
        Add<25, decltype(&ID3D12GraphicsCommandList10::SetPipelineState)>();
        Add<26, decltype(&ID3D12GraphicsCommandList10::ResourceBarrier)>();
        Add<27, decltype(&ID3D12GraphicsCommandList10::ExecuteBundle)>();
        Add<28, decltype(&ID3D12GraphicsCommandList10::SetDescriptorHeaps)>();
        Add<29, decltype(&ID3D12GraphicsCommandList10::SetComputeRootSignature)>();
        Add<30, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRootSignature)>();
        Add<31, decltype(&ID3D12GraphicsCommandList10::SetComputeRootDescriptorTable)>();
        Add<32, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRootDescriptorTable)>();
        Add<33, decltype(&ID3D12GraphicsCommandList10::SetComputeRoot32BitConstant)>();
        Add<34, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRoot32BitConstant)>();
        Add<35, decltype(&ID3D12GraphicsCommandList10::SetComputeRoot32BitConstants)>();
        Add<36, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRoot32BitConstants)>();
        Add<37, decltype(&ID3D12GraphicsCommandList10::SetComputeRootConstantBufferView)>();
        Add<38, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRootConstantBufferView)>();
        Add<39, decltype(&ID3D12GraphicsCommandList10::SetComputeRootShaderResourceView)>();
        Add<40, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRootShaderResourceView)>();
        Add<41, decltype(&ID3D12GraphicsCommandList10::SetComputeRootUnorderedAccessView)>();
        Add<42, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRootUnorderedAccessView)>();
        Add<43, decltype(&ID3D12GraphicsCommandList10::IASetIndexBuffer)>();
        Add<44, decltype(&ID3D12GraphicsCommandList10::IASetVertexBuffers)>();
        Add<45, decltype(&ID3D12GraphicsCommandList10::SOSetTargets)>();
        Add<46, decltype(&ID3D12GraphicsCommandList10::OMSetRenderTargets)>();
        Add<47, decltype(&ID3D12GraphicsCommandList10::ClearDepthStencilView)>();
        Add<48, decltype(&ID3D12GraphicsCommandList10::ClearRenderTargetView)>();
        Add<49, decltype(&ID3D12GraphicsCommandList10::ClearUnorderedAccessViewUint)>();
        Add<50, decltype(&ID3D12GraphicsCommandList10::ClearUnorderedAccessViewFloat)>();
        Add<51, decltype(&ID3D12GraphicsCommandList10::DiscardResource)>();
        Add<52, decltype(&ID3D12GraphicsCommandList10::BeginQuery)>();
        Add<53, decltype(&ID3D12GraphicsCommandList10::EndQuery)>();
        Add<54, decltype(&ID3D12GraphicsCommandList10::ResolveQueryData)>();
        Add<55, decltype(&ID3D12GraphicsCommandList10::SetPredication)>();
        Add<56, decltype(&ID3D12GraphicsCommandList10::SetMarker)>();
        Add<57, decltype(&ID3D12GraphicsCommandList10::BeginEvent)>();
        Add<58, decltype(&ID3D12GraphicsCommandList10::EndEvent)>();
        Add<59, decltype(&ID3D12GraphicsCommandList10::ExecuteIndirect)>();
        Add<60, decltype(&ID3D12GraphicsCommandList10::AtomicCopyBufferUINT)>();
        Add<61, decltype(&ID3D12GraphicsCommandList10::AtomicCopyBufferUINT64)>();
        Add<62, decltype(&ID3D12GraphicsCommandList10::OMSetDepthBounds)>();
        Add<63, decltype(&ID3D12GraphicsCommandList10::SetSamplePositions)>();
        Add<64, decltype(&ID3D12GraphicsCommandList10::ResolveSubresourceRegion)>();
        Add<65, decltype(&ID3D12GraphicsCommandList10::SetViewInstanceMask)>();
        Add<66, decltype(&ID3D12GraphicsCommandList10::WriteBufferImmediate)>();
        Add<67, decltype(&ID3D12GraphicsCommandList10::SetProtectedResourceSession)>();
        Add<68, decltype(&ID3D12GraphicsCommandList10::BeginRenderPass)>();
        Add<69, decltype(&ID3D12GraphicsCommandList10::EndRenderPass)>();
        Add<70, decltype(&ID3D12GraphicsCommandList10::InitializeMetaCommand)>();
        Add<71, decltype(&ID3D12GraphicsCommandList10::ExecuteMetaCommand)>();
        Add<72, decltype(&ID3D12GraphicsCommandList10::BuildRaytracingAccelerationStructure)>();
        Add<73, decltype(&ID3D12GraphicsCommandList10::EmitRaytracingAccelerationStructurePostbuildInfo)>();
        Add<74, decltype(&ID3D12GraphicsCommandList10::CopyRaytracingAccelerationStructure)>();
        Add<75, decltype(&ID3D12GraphicsCommandList10::SetPipelineState1)>();
        Add<76, decltype(&ID3D12GraphicsCommandList10::DispatchRays)>();
        Add<77, decltype(&ID3D12GraphicsCommandList10::RSSetShadingRate)>();
        Add<78, decltype(&ID3D12GraphicsCommandList10::RSSetShadingRateImage)>();
        Add<79, decltype(&ID3D12GraphicsCommandList10::DispatchMesh)>();
        Add<80, decltype(&ID3D12GraphicsCommandList10::Barrier)>();
        Add<81, decltype(&ID3D12GraphicsCommandList10::OMSetFrontAndBackStencilRef)>();
        Add<82, decltype(&ID3D12GraphicsCommandList10::RSSetDepthBias)>();
        Add<83, decltype(&ID3D12GraphicsCommandList10::IASetIndexBufferStripCutValue)>();
        Add<84, decltype(&ID3D12GraphicsCommandList10::SetProgram)>();
        Add<85, decltype(&ID3D12GraphicsCommandList10::DispatchGraph)>();
        // A shared target with different ABI signatures cannot be safely detoured twice.
        std::unordered_set<PVOID> targets;
        for (const auto& method : methods_)
            if (!method.target || !targets.insert(method.target).second) return Refuse(list, NativeRecordingHookRefusal::DuplicateTarget,
                S_OK, method.slot, GUID{}, 0, reinterpret_cast<std::uint64_t>(method.target));
        // Pin the module containing these callbacks before exposing any detour.
        // Live-process thread enumeration cannot prove that no new thread can
        // enter a trampoline, so revocation never frees executable forwarding paths.
        HMODULE callbackModule = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(methods_.front().detour), &callbackModule)) return Refuse(list, NativeRecordingHookRefusal::ModulePin, GetLastError());
        instance_ = this;
#ifdef NATIVE_RECORDING_HOOK_TEST_ACCESS
        if (beforeTransactionForTest_) beforeTransactionForTest_();
#endif
        std::vector<HANDLE> threads;
        if (!BeginTransaction(list, threads, poisoned_)) { CloseThreads(threads); return false; }
#ifdef NATIVE_RECORDING_HOOK_TEST_ACCESS
        if (afterSuspendForTest_) afterSuspendForTest_();
#endif
        bool ok = true;
        const InstalledMethod* failedMethod = nullptr;
        LONG attachResult = NO_ERROR;
        for (auto& method : methods_)
        {
#ifdef NATIVE_RECORDING_HOOK_TEST_ACCESS
            PVOID invalidOriginal = nullptr;
            const auto result = DetourAttachEx(failAttachForTest_ ? &invalidOriginal : method.original, method.detour, nullptr, &method.patchTarget, nullptr);
#else
            const auto result = DetourAttachEx(method.original, method.detour, nullptr, &method.patchTarget, nullptr);
#endif
            if (result != NO_ERROR || !method.patchTarget)
            { ok = false; failedMethod = &method; attachResult = result; break; }
        }
        if (ok)
        {
            const auto result = DetourTransactionCommit();
            ok = result == NO_ERROR;
            if (!ok) Refuse(list, NativeRecordingHookRefusal::Commit, result);
        }
        else if (DetourTransactionAbort() != NO_ERROR) poisoned_ = true;
        CloseThreads(threads);
        if (failedMethod) Refuse(list, NativeRecordingHookRefusal::Attach, attachResult, failedMethod->slot, GUID{},
            reinterpret_cast<std::uint64_t>(failedMethod->target), 0, failedMethod);
        if (!ok) return false;
        for (auto& method : methods_)
        {
            std::memcpy(method.patch.data(), method.patchTarget, method.patch.size());
            std::memcpy(method.routePatch.data(), method.target, method.routePatch.size());
            if (method.slot == 10)
            {
                installedResetTarget_.store(reinterpret_cast<std::uint64_t>(method.target));
                resetPatchTarget_.store(reinterpret_cast<std::uint64_t>(method.patchTarget));
            }
        }
        installedVtable_.store(reinterpret_cast<std::uint64_t>(vtable_));
        installed_ = true;
        retainedCoverage_ = std::make_shared<const Coverage>(Coverage{vtable_, methods_});
        coverage_.store(retainedCoverage_);
        observer_.SetHookCoverage(true, [this](const auto& identity) {
            return identity.interfaceVersion == InterfaceVersion() && Matches(identity.nativeList.Get(), identity.commandTable);
        });
        return installed();
    }
    void Revoke()
    {
        std::lock_guard lock(installationMutex_);
        coverage_.store({});
        ++forwardingGeneration_;
        { std::lock_guard routes(forwardingMutex_); forwarding_.clear(); }
        observer_.SetHookCoverage(false);
        // Deliberately retain callbacks, original trampolines, and their pinned
        // module until process exit. No thread-creation exclusion is available.
    }
    // The blob is the actual successful root creation input. Sampler-only
    // overrides do not alter its parameter layout. No guessed layouts are added.
    bool RegisterRootSignature(ID3D12RootSignature* signature, const void* blob, SIZE_T bytes,
                              decltype(&D3D12CreateVersionedRootSignatureDeserializer) deserialize)
    {
        if (!signature || !blob || !bytes || !deserialize) return false;
        Microsoft::WRL::ComPtr<ID3D12VersionedRootSignatureDeserializer> decoded;
        if (FAILED(deserialize(blob, bytes, IID_PPV_ARGS(&decoded)))) return false;
        const auto* desc = decoded->GetUnconvertedRootSignatureDesc();
        if (!desc) return false;
        std::vector<RootParameter> layout;
        const auto read = [&](const auto& root) {
            if (root.NumParameters > 64 || (root.NumParameters && !root.pParameters)) return false;
            for (UINT i = 0; i < root.NumParameters; ++i)
            {
                const auto& parameter = root.pParameters[i];
                switch (parameter.ParameterType)
                {
                case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: layout.push_back({RootKind::Table}); break;
                case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS: layout.push_back({RootKind::Constants, parameter.Constants.Num32BitValues}); break;
                case D3D12_ROOT_PARAMETER_TYPE_CBV: layout.push_back({RootKind::CBV}); break;
                case D3D12_ROOT_PARAMETER_TYPE_SRV: layout.push_back({RootKind::SRV}); break;
                case D3D12_ROOT_PARAMETER_TYPE_UAV: layout.push_back({RootKind::UAV}); break;
                default: return false;
                }
            }
            return true;
        };
        bool valid = false;
        switch (desc->Version)
        {
        case D3D_ROOT_SIGNATURE_VERSION_1_0: valid = read(desc->Desc_1_0); break;
        case D3D_ROOT_SIGNATURE_VERSION_1_1: valid = read(desc->Desc_1_1); break;
        case D3D_ROOT_SIGNATURE_VERSION_1_2: valid = read(desc->Desc_1_2); break;
        default: return false;
        }
        return valid && observer_.RegisterLayout(signature, std::move(layout));
    }
    NativeRecordingState::OriginalSetters Originals() const
    {
        NativeRecordingState::OriginalSetters result;
        if (!installed_ || poisoned_ || !coverage_.load()) return result;
        result.heaps = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<28>(Hook<28, decltype(&ID3D12GraphicsCommandList10::SetDescriptorHeaps)>::original, args...); };
        result.pipeline = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<25>(Hook<25, decltype(&ID3D12GraphicsCommandList10::SetPipelineState)>::original, args...); };
        result.computeSignature = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<29>(Hook<29, decltype(&ID3D12GraphicsCommandList10::SetComputeRootSignature)>::original, args...); };
        result.graphicsSignature = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<30>(Hook<30, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRootSignature)>::original, args...); };
        result.computeTable = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<31>(Hook<31, decltype(&ID3D12GraphicsCommandList10::SetComputeRootDescriptorTable)>::original, args...); };
        result.graphicsTable = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<32>(Hook<32, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRootDescriptorTable)>::original, args...); };
        result.computeConstants = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<35>(Hook<35, decltype(&ID3D12GraphicsCommandList10::SetComputeRoot32BitConstants)>::original, args...); };
        result.graphicsConstants = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<36>(Hook<36, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRoot32BitConstants)>::original, args...); };
        result.computeCbv = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<37>(Hook<37, decltype(&ID3D12GraphicsCommandList10::SetComputeRootConstantBufferView)>::original, args...); };
        result.graphicsCbv = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<38>(Hook<38, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRootConstantBufferView)>::original, args...); };
        result.computeSrv = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<39>(Hook<39, decltype(&ID3D12GraphicsCommandList10::SetComputeRootShaderResourceView)>::original, args...); };
        result.graphicsSrv = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<40>(Hook<40, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRootShaderResourceView)>::original, args...); };
        result.computeUav = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<41>(Hook<41, decltype(&ID3D12GraphicsCommandList10::SetComputeRootUnorderedAccessView)>::original, args...); };
        result.graphicsUav = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<42>(Hook<42, decltype(&ID3D12GraphicsCommandList10::SetGraphicsRootUnorderedAccessView)>::original, args...); };
        result.barriers = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<26>(Hook<26, decltype(&ID3D12GraphicsCommandList10::ResourceBarrier)>::original, args...); };
        return result;
    }
};
}

