#pragma once
#include "NativeRecordingState.h"
#include "NativeQueryInsertion.h"
#include "NativeReplayEffects.h"
#include "NativeWrapperHeapBindings.h"
#include "NativeRecordingInterfaces.h"
#include <detours/detours.h>
#include <tlhelp32.h>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <cstring>
#include <stdexcept>
#include <atomic>
#include <exception>
#include <memory>

namespace Neurotic::D3D12
{
enum class NativeRecordingHookRefusal : std::uint32_t
{
    None, InvalidList, Poisoned, OtherOwner, NativeIdentity, CommandListType,
    InterfaceQuery, InterfaceIdentity, InterfaceShape, DuplicateTarget, ModulePin,
    TransactionBegin, TransactionThread, ThreadSnapshot, ThreadEnumeration, ThreadOpen,
    Attach, Commit, CoverageAbsent, VtableMismatch, SlotMismatch, RouteBytesMismatch,
    PatchBytesMismatch, UnknownInterface, QueryIdentity, ResetFailed, Enrollment,
    ReplayLifecycle, ReplayEffect, UnsupportedEffect, StateObservationRejected, EligibilityUnavailable, WrapperHeapRoute, WrapperQueryRoute
};
struct NativeRecordingHookDiagnostic
{
    std::uint64_t queriedList = 0, queriedVtable = 0, installedVtable = 0;
    std::uint64_t installAttempts = 0, installSuccesses = 0, closeCalls = 0;
    std::uint64_t resetCalls = 0, resetSuccesses = 0, enrollmentSuccesses = 0, queryInterfaceCalls = 0;
    std::uint64_t firstRefusalSequence = 0, droppedEvents = 0;
    std::uint64_t globalInstallAttempts = 0, globalInstallSuccesses = 0, lastInstallList = 0;
    std::uint64_t queriedResetTarget = 0, installedResetTarget = 0, resetPatchTarget = 0;
    std::uint32_t flags = 0; // 1 installed, 2 coverage active, 4 retained address record, 8 historical denial at this address
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
            QueryCallScope queryScope(owner, nullptr, list);
            if constexpr (Slot == 53) {
                const auto values=std::forward_as_tuple(args...);
                queryScope.NativeEnd(std::get<0>(values),std::get<1>(values),std::get<2>(values));
            }
            if constexpr (std::is_void_v<R>)
            {
                // A rejected enrollment can still own the final temporary root
                // reference. Its Release must not Reset an epoch before this
                // original setter executes. Keep it through unwind as well.
                NativeRecordingState::RootOwner forwardingOwner;
                NativeRecordingState::PreparedOwners forwardingObjects;
                NativeRecordingState::PreparedBarriers barrierFacts;
                if constexpr (Slot == 26)
                {
                    try { const auto values=std::forward_as_tuple(args...); barrierFacts.Acquire(std::get<0>(values), std::get<1>(values)); }
                    catch (...) { barrierFacts.valid = false; }
                }
                // Foreign ownership can reenter Reset. Acquire it before
                // selecting the current recording for this actual setter.
                ForwardTrace trace(owner, list, Slot);
                // Prepare argument owners, finish pending-operation cancellation
                // and its foreign final releases, then journal the actual call.
                // QueryCallScope suppresses rearming throughout that interval.
                owner.BeforeWithRootLifetime<Slot, true>(list, forwardingOwner, forwardingObjects, args...);
                NativeRecordingObservation beforeEnd;
                if constexpr (Slot == 53) beforeEnd = owner.observer_.Observe(list);
                std::uint64_t barrierIncarnation = 0, barrierSerial = 0;
                if constexpr (Slot == 26) owner.observer_.WithCurrent(list, [&](auto& state, auto incarnation) {
                    barrierIncarnation=incarnation; barrierSerial=state.ResourceFactSerial(incarnation); return true;
                });
                original(self, args...);
                if constexpr (Slot == 26) owner.observer_.WithCurrent(list, [&](auto& state, auto incarnation) {
                    return incarnation == barrierIncarnation && state.CompleteBarriers(incarnation, barrierSerial, barrierFacts);
                });
                if constexpr (Slot == 53) {
                    const auto values=std::forward_as_tuple(args...);
                    owner.QueryEndReturned(list,beforeEnd,queryScope,std::get<0>(values),std::get<1>(values),std::get<2>(values));
                }
            }
            else
            {
                owner.BeforeQueryInsertion<Slot>(list, args...);
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
                        NativeReplayEffects::InterfaceEscape(list);
                        owner.Deny(list, true);
                    }
                }
                else if constexpr (Slot == 9)
                {
                    NativeReplayEffects::Lifecycle(list, 9);
                    owner.Record(list, [](auto& d, auto) { ++d.closeCalls; });
                    if (replayDepth_) owner.Refuse(list, NativeRecordingHookRefusal::ReplayLifecycle, result, Slot);
                    if (replayDepth_) { owner.Deny(list); replayInvalid_ = true; }
                    owner.RetireRecording(list);
                }
                else if constexpr (Slot == 10)
                {
                    NativeReplayEffects::Lifecycle(list, 10);
                    owner.Record(list, [result](auto& d, auto) { ++d.resetCalls; d.lastResetResult = result; if (SUCCEEDED(result)) ++d.resetSuccesses; });
                    if (FAILED(result)) owner.Refuse(list, NativeRecordingHookRefusal::ResetFailed, result, Slot);
                    if (replayDepth_) owner.Refuse(list, NativeRecordingHookRefusal::ReplayLifecycle, result, Slot);
                    if (replayDepth_) { owner.Deny(list); replayInvalid_ = true; owner.RetireRecording(list); return result; }
                    // Start before ANY post-Reset foreign acquisition/release.
                    // A newer genuine callback Reset/Close wins this list's
                    // enrollment token; the older completion cannot erase it.
                    const auto enrollment=owner.observer_.BeginEnrollment(list);
                    if(owner.observer_.EnrollmentCurrent(enrollment))owner.RetireForwarding(list);
                    if(owner.observer_.EnrollmentCurrent(enrollment))NativeWrapperHeapBindings::Retire(list);
                    auto** table = *reinterpret_cast<void***>(list);
                    if (SUCCEEDED(result) && owner.observer_.EnrollmentCurrent(enrollment) &&
                        owner.CheckShape(list) && owner.Matches(list, table))
                    {
                        auto values = std::forward_as_tuple(args...);
                        NativeRecordingResetDiagnostic diagnostic;
                        if (owner.observer_.ResetNative(list, std::get<1>(values), owner.InterfaceVersion(), &diagnostic, table,enrollment))
                            owner.Record(list, [](auto& d, auto) { ++d.enrollmentSuccesses; });
                        else owner.Refuse(list, NativeRecordingHookRefusal::Enrollment, diagnostic.result, Slot,
                            diagnostic.iid, diagnostic.expectedIdentity, diagnostic.observedIdentity, nullptr, nullptr, nullptr, diagnostic.stage);
                    }
                    owner.observer_.FinishEnrollment(enrollment);
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
    NativeQueryInsertion::Owner queryInsertions_;
    // Every covered forwarding call participates. The outermost destructor runs
    // after argument ownership releases and ForwardTrace have completed.
    struct QueryCallScope
    {
        inline static thread_local QueryCallScope* current = nullptr;
        NativeRecordingHooks& owner;
        QueryCallScope* prior;
        QueryCallScope* root;
        int exceptions = std::uncaught_exceptions();
        struct ForwardCompletion { bool completed=false,failed=false; };
        struct ReadyEntry {
            NativeQueryInsertion::Boundary boundary;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> caller,terminal;
            std::vector<std::shared_ptr<ForwardCompletion>> forwarding;
        };
        std::optional<ReadyEntry> firstReady;
        std::vector<ReadyEntry> moreReady;
        size_t readyListCount=0;
        std::array<std::uintptr_t,64> failedEnds{};
        size_t failedEndCount=0;
        bool nativeEnd=false;
        ID3D12QueryHeap* nativeHeap=nullptr;
        bool wrapper=false;
        std::shared_ptr<ForwardCompletion> completion;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> wrapperCaller,wrapperTerminal;
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> wrapperHeap;
        D3D12_QUERY_TYPE wrapperType{};
        UINT wrapperIndex=0;
        ID3D12GraphicsCommandList* affected;
        explicit QueryCallScope(NativeRecordingHooks& value, ID3D12GraphicsCommandList* caller = nullptr,
                                ID3D12GraphicsCommandList* affectedList = nullptr,
                                ID3D12QueryHeap* heap=nullptr,D3D12_QUERY_TYPE type={},UINT index=0) noexcept
            : owner(value), prior(current), root(prior && &prior->owner == &value ? prior->root : this), affected(affectedList)
        {
            current = this;
            if(caller) {
                wrapper=true;wrapperType=type;wrapperIndex=index;
                try {
                    completion=std::make_shared<ForwardCompletion>();
                    auto interfaces=NativeRecordingInterfaces::Resolve(caller);
                    wrapperCaller=std::move(interfaces.caller);
                    if(SUCCEEDED(interfaces.result))wrapperTerminal=std::move(interfaces.terminal);
                    auto query=DlssNr::NativeIdentity::Resolve<ID3D12QueryHeap>(heap);
                    if(SUCCEEDED(query.result))wrapperHeap=std::move(query.object);
                }catch(...) { /* Missing association cannot suppress the host call. */ }
            }
        }
        void NativeEnd(ID3D12QueryHeap* heap,D3D12_QUERY_TYPE type,UINT index) noexcept
        {
            nativeEnd=true;nativeHeap=heap;wrapperType=type;wrapperIndex=index;
            try { completion=std::make_shared<ForwardCompletion>(); } catch(...) {}
        }
        void FailedEnd(std::uintptr_t list) noexcept
        {
            auto& outer=*root;
            for(size_t i=0;i<outer.failedEndCount;++i)if(outer.failedEnds[i]==list)return;
            if(outer.failedEndCount<outer.failedEnds.size())outer.failedEnds[outer.failedEndCount++]=list;
            else if(!outer.HasReady(list)) {
                try { owner.queryInsertions_.Cancel(list,NativeQueryInsertion::Reason::Capacity); } catch(...) {}
            }
        }
        bool HasReady(std::uintptr_t list) const noexcept
        {
            return (firstReady && firstReady->boundary.list == list) ||
                std::any_of(moreReady.begin(),moreReady.end(),[&](const auto& b){return b.boundary.list==list;});
        }
        static bool InFlight(NativeRecordingHooks& owner, ID3D12GraphicsCommandList* list) noexcept
        {
            for(auto* scope=current;scope;scope=scope->prior)
                if(&scope->owner==&owner && (scope->affected==list ||
                   scope->root->HasReady(reinterpret_cast<std::uintptr_t>(list)) ||
                   std::find(scope->root->failedEnds.begin(),scope->root->failedEnds.begin()+scope->root->failedEndCount,
                             reinterpret_cast<std::uintptr_t>(list))!=scope->root->failedEnds.begin()+scope->root->failedEndCount))return true;
            return false;
        }
        void Ready(NativeQueryInsertion::Boundary boundary,ID3D12QueryHeap* heap,D3D12_QUERY_TYPE type,UINT index) noexcept
        {
            auto& outer = *root;
            try {
                auto* terminal=reinterpret_cast<ID3D12GraphicsCommandList*>(boundary.list);
                ID3D12GraphicsCommandList* caller=terminal;
                bool unknownAssociation=false;
                std::vector<std::shared_ptr<ForwardCompletion>> forwarding;
                for(auto* scope=this;scope;scope=scope->prior) {
                    if(&scope->owner!=&owner||scope->wrapperType!=type||scope->wrapperIndex!=index)continue;
                    if(scope->nativeEnd) {
                        if(scope->affected!=terminal||scope->nativeHeap!=heap)continue;
                    } else if(scope->wrapper) {
                        if(scope->wrapperTerminal&&scope->wrapperTerminal.Get()!=terminal)continue;
                        if(scope->wrapperHeap&&scope->wrapperHeap.Get()!=heap)continue;
                        if(!scope->wrapperTerminal||!scope->wrapperHeap||!scope->wrapperCaller){unknownAssociation=true;continue;}
                        caller=scope->wrapperCaller.Get();
                    } else continue;
                    // Only this exact forwarding chain contributes completion
                    // dependencies. Root scope lifetime is scheduling, not proof
                    // that an unrelated surrounding End succeeded.
                    if(!scope->completion){FailedEnd(boundary.list);return;}
                    forwarding.push_back(scope->completion);
                }
                // A proven inner chain cannot authenticate a potentially matching
                // unresolved outer. Known unrelated scopes were filtered above.
                if(unknownAssociation){FailedEnd(boundary.list);return;}
                ReadyEntry entry{boundary,caller,terminal,std::move(forwarding)};
                // Keep separate exact-boundary receipts until every enclosing
                // call has unwound; a later failed End must not replace a success.
                // Preserve the existing capacity of 64 distinct terminals;
                // several End boundaries on one terminal consume one slot.
                const bool newList=!outer.HasReady(boundary.list);
                if(newList&&outer.readyListCount==64){FailedEnd(boundary.list);return;}
                if(!outer.firstReady){outer.firstReady=std::move(entry);++outer.readyListCount;return;}
                outer.moreReady.push_back(std::move(entry));
                if(newList)++outer.readyListCount;
                return;
            } catch(...) {}
            FailedEnd(boundary.list);
        }
        ~QueryCallScope() noexcept
        {
            const bool failed=std::uncaught_exceptions()!=exceptions;
            // Retire association owners while this forwarding scope still
            // suppresses rearming. The drain observes any Release reentry.
            wrapperHeap.Reset();wrapperTerminal.Reset();wrapperCaller.Reset();
            if(completion){completion->failed=failed;completion->completed=true;}
            current = prior;
            if (affected && failed)
            {
                try {
                    if(nativeEnd)FailedEnd(reinterpret_cast<std::uintptr_t>(affected));
                    else owner.queryInsertions_.Cancel(reinterpret_cast<std::uintptr_t>(affected), NativeQueryInsertion::Reason::ForwardingFailed);
                }
                catch (...) {}
            }
            if (root != this) return;
            std::array<std::uintptr_t,64> finished{};size_t finishedCount=0;
            const auto successful=[](const ReadyEntry& key) {
                return std::all_of(key.forwarding.begin(),key.forwarding.end(),
                    [](const auto& receipt){return receipt->completed&&!receipt->failed;});
            };
            const auto finish=[&](const auto& first) noexcept {
                const auto list=first.boundary.list;
                if(std::find(finished.begin(),finished.begin()+finishedCount,list)!=finished.begin()+finishedCount)return;
                finished[finishedCount++]=list;
                try
                {
                    const ReadyEntry* selected=successful(first)?&first:nullptr;
                    if(!selected)for(const auto& other:moreReady)
                        if(other.boundary.list==list&&successful(other)){selected=&other;break;}
                    if(selected)owner.DrainQueryInsertion(selected->boundary,selected->caller.Get());
                    else owner.queryInsertions_.Cancel(list,NativeQueryInsertion::Reason::ForwardingFailed);
                }
                catch (...) {
                    // Refuse the affected insertion even when foreign caller
                    // resolution throws; never leave its ready action armed.
                    try { owner.queryInsertions_.Cancel(list,NativeQueryInsertion::Reason::ForwardingFailed); }
                    catch (...) {}
                }
            };
            if(firstReady)finish(*firstReady);
            for(const auto& key:moreReady)finish(key);
            for(size_t i=0;i<failedEndCount;++i)
                if(std::find(finished.begin(),finished.begin()+finishedCount,failedEnds[i])==finished.begin()+finishedCount)
                    try { owner.queryInsertions_.Cancel(failedEnds[i],NativeQueryInsertion::Reason::ForwardingFailed); } catch(...) {}
        }
    };
    static NativeQueryInsertion::Boundary QueryBoundary(const NativeRecordingObservation& observed) noexcept
    { return {reinterpret_cast<std::uintptr_t>(observed.nativeList), observed.incarnation, observed.hookGeneration, observed.workOrdinal}; }
    template<unsigned Slot, class... A> void BeforeQueryInsertion(ID3D12GraphicsCommandList* list, A... args)
    {
        if constexpr (Slot == 48)
        {
            const auto values = std::forward_as_tuple(args...);
            queryInsertions_.RtvClear(QueryBoundary(observer_.Observe(list)), std::get<0>(values).ptr);
            return;
        }
        using Reason = NativeQueryInsertion::Reason;
        Reason reason = Reason::None;
        if constexpr (Slot == 9) reason = Reason::Closed;
        else if constexpr (Slot == 10) reason = Reason::Reset;
        else if constexpr ((Slot >= 12 && Slot <= 19) || (Slot >= 47 && Slot <= 51) ||
                           Slot == 54 || Slot == 59 || Slot == 66 || Slot == 76) reason = Reason::DataWork;
        else if constexpr (Slot == 52) reason = Reason::QueryReopened;
        else if constexpr (Slot == 26)
        {
            const auto values = std::forward_as_tuple(args...);
            const auto count = std::get<0>(values); const auto* barriers = std::get<1>(values);
            if (count && !barriers) reason = Reason::UnknownEffect;
            else for (UINT i = 0; i < count; ++i)
                if (barriers[i].Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING) reason = Reason::Aliased;
                else if (barriers[i].Flags != D3D12_RESOURCE_BARRIER_FLAG_NONE ||
                    (barriers[i].Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && barriers[i].Type != D3D12_RESOURCE_BARRIER_TYPE_UAV)) reason = Reason::UnknownEffect;
        }
        else if constexpr (Slot == 0 || (Slot >= 20 && Slot <= 25) ||
                           (Slot >= 28 && Slot <= 46) || Slot == 53 || (Slot >= 56 && Slot <= 58) ||
                           Slot == 62 || Slot == 75 || Slot == 77 || Slot == 78 || Slot == 82) {}
        else reason = Reason::UnknownEffect;
        if (reason != Reason::None) queryInsertions_.Cancel(reinterpret_cast<std::uintptr_t>(list), reason, Slot);
    }
    void QueryEndReturned(ID3D12GraphicsCommandList* list, const NativeRecordingObservation& before, QueryCallScope& scope,
        ID3D12QueryHeap* heap,D3D12_QUERY_TYPE type,UINT index)
    {
        if (!before.active || !before.completeCoverage) return;
        NativeStateCaptureDiagnostic diagnostic;
        const auto after = observer_.Observe(list, &diagnostic);
        if (!after.active || !after.completeCoverage || after.incarnation != before.incarnation ||
            after.hookGeneration != before.hookGeneration || diagnostic.openQueryTotal) return;
        const auto boundary = QueryBoundary(after);
        queryInsertions_.EndReturned(boundary);
        scope.Ready(boundary,heap,type,index);
    }
    void DrainQueryInsertion(const NativeQueryInsertion::Boundary& completed, ID3D12GraphicsCommandList* executionCaller)
    {
        auto* list = reinterpret_cast<ID3D12GraphicsCommandList*>(completed.list);
        bool associated=false;
        {
            const auto interfaces=NativeRecordingInterfaces::Resolve(executionCaller);
            associated=SUCCEEDED(interfaces.result)&&interfaces.terminal.Get()==list;
        } // Retire temporary COM owners before selecting the current epoch.
        if(!associated){queryInsertions_.Cancel(completed.list,NativeQueryInsertion::Reason::ForwardingFailed);return;}
        NativeStateCaptureDiagnostic diagnostic;
        const auto observed = observer_.Observe(list, &diagnostic);
        if (!observed.active || !observed.completeCoverage || diagnostic.openQueryTotal ||
            observed.incarnation != completed.incarnation || observed.hookGeneration != completed.hookGeneration)
        { queryInsertions_.Cancel(completed.list, NativeQueryInsertion::Reason::CoverageLost); return; }
        auto boundary = QueryBoundary(observed);
        boundary.executionCaller = reinterpret_cast<std::uintptr_t>(executionCaller);
        if (queryInsertions_.Drain(boundary) == NativeQueryInsertion::Outcome::Failed)
            observer_.WithCurrent(list, [&](auto& state, auto incarnation) {
                if (incarnation == completed.incarnation) state.Taint(incarnation, "Native.QueryInsertion.RecordFailed");
                return false;
            });
    }
    struct ForwardRoute
    {
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> caller, callee;
        unsigned slot;
        std::uint64_t generation;
    };
    std::atomic<std::uint64_t> forwardingGeneration_ = 1;
    std::mutex forwardingMutex_;
    std::vector<std::shared_ptr<const ForwardRoute>> forwarding_;
    struct ForwardTrace
    {
        inline static thread_local ForwardTrace* current = nullptr;
        ForwardTrace* prior;
        NativeRecordingHooks& owner;
        ID3D12GraphicsCommandList* list;
        unsigned slot;
        std::uint64_t generation;
        std::shared_ptr<const ForwardRoute> lifetime;
        ForwardTrace(NativeRecordingHooks& o, ID3D12GraphicsCommandList* l, unsigned s) noexcept
            : prior(current), owner(o), list(l), slot(s), generation(o.forwardingGeneration_.load())
        {
            try
            {
                if (!replayDepth_ && prior && &prior->owner == &owner && prior->slot == slot && prior->list != list && prior->generation == generation && owner.coverage_.load())
                {
                    // Prepare owners outside the route lock; hold the chosen
                    // pair through the original call even if Reset retires it.
                    auto candidate = std::make_shared<const ForwardRoute>(ForwardRoute{prior->list,list,slot,generation});
                    {
                        std::lock_guard lock(owner.forwardingMutex_);
                        if (generation == owner.forwardingGeneration_.load())
                        {
                            const auto found = std::find_if(owner.forwarding_.begin(),owner.forwarding_.end(),[&](const auto& route){
                                return route->caller.Get()==prior->list && route->callee.Get()==list && route->slot==slot && route->generation==generation;
                            });
                            if (found == owner.forwarding_.end()) { owner.forwarding_.push_back(candidate); lifetime=candidate; }
                            else lifetime=*found;
                        }
                    }
                }
            }
            catch (...) { /* Missing forwarding proof cannot drop a host call. */ }
            current = this;
        }
        ~ForwardTrace() { current = prior; }
    };
    inline static thread_local unsigned replayDepth_ = 0;
    inline static thread_local bool replayInvalid_ = false;
    inline static thread_local unsigned replaySlot_ = 0;
    inline static thread_local ID3D12GraphicsCommandList* replayList_ = nullptr;
    inline static thread_local std::array<std::uint32_t,3> replayDepthBiasBits_{};
    template<unsigned Slot, class F, class... A> static void Replay(F original, A... args)
    {
        if (!replayDepth_)
        {
            replayInvalid_ = false; replaySlot_ = Slot;
            replayList_ = reinterpret_cast<ID3D12GraphicsCommandList*>(std::get<0>(std::forward_as_tuple(args...)));
            if constexpr(Slot==82){
                const auto values=std::forward_as_tuple(args...);
                replayDepthBiasBits_={std::bit_cast<std::uint32_t>(std::get<1>(values)),
                    std::bit_cast<std::uint32_t>(std::get<2>(values)),std::bit_cast<std::uint32_t>(std::get<3>(values))};
            }
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
    // Only failed private-data persistence needs a process-retained fallback.
    // Its owning reference prevents raw-address reuse from inheriting denial.
    mutable std::unordered_map<ID3D12GraphicsCommandList*, Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> deniedFallback_;
    std::atomic<bool> eligibilityMemoryFailure_ = false;
    inline static constexpr GUID escapedAliasGuid_ =
        {0x31460c7d,0x9b38,0x4c68,{0x9d,0x5a,0x31,0x72,0x71,0x81,0xd5,0x63}};
    using WrapperHeapFn = void (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,ID3D12DescriptorHeap* const*);
    WrapperHeapFn wrapperHeapOriginal_ = nullptr;
    InstalledMethod wrapperHeapMethod_{};
    std::atomic<bool> wrapperHeapInstalled_ = false;
    bool wrapperHeapPoisoned_ = false;
    using WrapperQueryFn = void (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12QueryHeap*,D3D12_QUERY_TYPE,UINT);
    WrapperQueryFn wrapperQueryOriginal_ = nullptr;
    InstalledMethod wrapperQueryMethod_{};
    std::atomic<bool> wrapperQueryInstalled_ = false;
    bool wrapperQueryPoisoned_ = false;
    static void STDMETHODCALLTYPE WrapperQueryCall(ID3D12GraphicsCommandList* list, ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type, UINT index)
    {
        auto& owner = *instance_;
        CallbackLease lease(owner);
        QueryCallScope scope(owner,list,nullptr,heap,type,index);
        owner.wrapperQueryOriginal_(list, heap, type, index);
    }
    static void STDMETHODCALLTYPE WrapperHeapCall(ID3D12GraphicsCommandList* list, UINT count, ID3D12DescriptorHeap* const* heaps)
    {
        auto& owner = *instance_;
        CallbackLease lease(owner);
        if (!owner.wrapperHeapInstalled_.load() || !owner.coverage_.load())
        { owner.wrapperHeapOriginal_(list,count,heaps); return; }
        NativeWrapperHeapBindings::ForwardingCall forwarding(list,count,heaps);
        owner.wrapperHeapOriginal_(list,count,heaps);
        forwarding.Complete(owner.observer_);
    }
    bool Eligible(ID3D12GraphicsCommandList* list) const
    {
        if (eligibilityMemoryFailure_.load()) return Refuse(list, NativeRecordingHookRefusal::EligibilityUnavailable, E_OUTOFMEMORY);
        {
            std::lock_guard lock(deniedMutex_);
            if (deniedFallback_.contains(list)) return false;
        }
        std::uint32_t denied = 0;UINT bytes = sizeof(denied);
        const auto result = list->GetPrivateData(escapedAliasGuid_, &bytes, &denied);
        if (result == DXGI_ERROR_NOT_FOUND) return true;
        if (FAILED(result) || bytes != sizeof(denied) || denied != 1)
            return Refuse(list, NativeRecordingHookRefusal::EligibilityUnavailable, FAILED(result) ? result : E_UNEXPECTED);
        return Refuse(list, NativeRecordingHookRefusal::UnknownInterface, E_ACCESSDENIED, 0);
    }
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
        if (!list || !Eligible(list)) return false;
        const auto type = list->GetType(); // discovery before observer/route locks
        if (type != D3D12_COMMAND_LIST_TYPE_DIRECT)
            return Refuse(list, NativeRecordingHookRefusal::CommandListType, S_OK, 8, GUID{}, D3D12_COMMAND_LIST_TYPE_DIRECT, type);
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
    // recording identity. No COM calls here: the observer invokes this under its lock.
    bool Matches(ID3D12GraphicsCommandList* list, void** expectedTable) const
    {
        const auto coverage = coverage_.load();
        if (!list) return false;
        if (!coverage) return Refuse(list, NativeRecordingHookRefusal::CoverageAbsent);
        if (eligibilityMemoryFailure_.load()) return false;
        { std::lock_guard lock(deniedMutex_); if (deniedFallback_.contains(list)) return false; }
        bool match = true;
        const auto table = *reinterpret_cast<void***>(list);
        if (!expectedTable || table != expectedTable) match = Refuse(list, NativeRecordingHookRefusal::VtableMismatch, S_OK,
            UINT32_MAX, GUID{}, reinterpret_cast<std::uint64_t>(expectedTable), reinterpret_cast<std::uint64_t>(table));
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
        return match;
    }
    void ClearForwarding(std::vector<std::shared_ptr<const ForwardRoute>>& retired)
    {
        std::lock_guard lock(forwardingMutex_);
        forwarding_.swap(retired);
    }
    void RetireForwarding(ID3D12GraphicsCommandList* list) noexcept
    {
        std::vector<std::shared_ptr<const ForwardRoute>> retired;
        try
        {
            std::lock_guard lock(forwardingMutex_);
            retired.reserve(forwarding_.size());
            for (auto& route : forwarding_)
                if (route->caller.Get()==list || route->callee.Get()==list) retired.push_back(std::move(route));
            std::erase_if(forwarding_,[](const auto& route){return !route;});
        }
        catch (...) { /* Keep lifetime owners if cleanup allocation failed. */ }
        // Retired COM references are released with no route lock held.
    }
    void Deny(ID3D12GraphicsCommandList* list, bool escapedAlias = false)
    {
        // A successful mutable terminal alias escape during discovery also
        // invalidates pending enrollment before any marker/owner callbacks.
        observer_.InvalidatePendingEffect(list);
        if (escapedAlias)
        {
            Record(list, [](auto& diagnostic, auto) { diagnostic.flags |= 8; });
            const std::uint32_t denied = 1;
            if (FAILED(list->SetPrivateData(escapedAliasGuid_, sizeof(denied), &denied)))
            {
                Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> retained = list;
                try
                {
                    std::lock_guard lock(deniedMutex_);
                    auto entry = deniedFallback_.try_emplace(list).first;
                    if (!entry->second) entry->second = std::move(retained);
                }
                catch (...)
                {
                    // Actual memory failure prevents retaining the affected
                    // lifetime safely. Do not invent alias retirement/recovery.
                    eligibilityMemoryFailure_.store(true);
                }
            }
        }
        if (replayDepth_) replayInvalid_ = true;
        // Original setters execute outside observer/state locks. A failed
        // legacy replay must also retire its touched recording authority.
        observer_.WithCurrent(list, [](auto& state, auto incarnation) { state.Taint(incarnation, "HookRouteDenied"); return false; });
    }
    void RetireRecording(ID3D12GraphicsCommandList* list)
    {
        const auto enrollment=observer_.BeginEnrollment(list);
        if(observer_.EnrollmentCurrent(enrollment))RetireForwarding(list);
        if(observer_.EnrollmentCurrent(enrollment))NativeWrapperHeapBindings::Retire(list);
        observer_.FinishEnrollment(enrollment);
    }
    // Observer-only test adapter; it does not dispatch a native original.
    template<unsigned Slot, class... A> void Before(ID3D12GraphicsCommandList* list, A... args)
    {
        NativeRecordingState::RootOwner forwardingOwner;
        NativeRecordingState::PreparedOwners forwardingObjects;
        BeforeWithRootLifetime<Slot>(list, forwardingOwner, forwardingObjects, args...);
    }
    template<unsigned Slot, bool CancelPending = false, class... A> void BeforeWithRootLifetime(ID3D12GraphicsCommandList* list,
        NativeRecordingState::RootOwner& forwardingOwner,
        NativeRecordingState::PreparedOwners& forwardingObjects,A... args)
    {
        if constexpr(Slot<56||Slot>58)observer_.InvalidatePendingEffect(list);
        bool ownershipPrepared = true;
        try {
            const auto arguments=std::forward_as_tuple(args...);
            if constexpr(Slot==25||Slot==75||Slot==52)forwardingObjects.Acquire(std::get<0>(arguments));
            else if constexpr(Slot==28) {
                const auto count=std::get<0>(arguments);auto* heaps=std::get<1>(arguments);
                if(count<=2&&(!count||heaps))for(UINT i=0;i<count;++i)forwardingObjects.Acquire(heaps[i]);
            }
        }catch(...) { ownershipPrepared = false; }
        if constexpr (CancelPending)
        {
            // Cancellation and the final retired-operation Release may Reset
            // this list. Complete both before choosing the journal epoch, but
            // keep every prepared argument owner alive through the original.
            BeforeQueryInsertion<Slot>(list, args...);
            if constexpr(Slot<56||Slot>58)observer_.InvalidatePendingEffect(list);
        }
        if (!ownershipPrepared)
        {
            observer_.WithCurrent(list,[](auto& state,auto incarnation){
                state.Taint(incarnation,"Native.State.ObjectOwnershipUnavailable");return false;
            },Slot);
            return;
        }
        // Heap argument proof samples the current epoch only after argument
        // acquisition and cancellation releases may have reentered real Reset.
        if constexpr (Slot == 28)
            NativeWrapperHeapBindings::ObserveTerminal(observer_, list, std::get<0>(std::forward_as_tuple(args...)), std::get<1>(std::forward_as_tuple(args...)));
        if constexpr (Slot < 56 || Slot > 58)
        {
            if (NativeReplayEffects::Contains(list))
            {
                if (NativeReplayEffects::Observe<Slot>(list, args...)) return;
                Refuse(list, NativeRecordingHookRefusal::ReplayEffect, S_OK, Slot);
                // Preserve every unmatched actual host call and observation.
            }
        }
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
                    return route->caller.Get() == replayList_ && route->callee.Get() == list && route->slot == Slot && route->generation == forwardingGeneration_.load();
                });
            }
            bool sameArguments=true;
            if constexpr(Slot==82){
                const auto values=std::forward_as_tuple(args...);
                sameArguments=replaySlot_==82&&replayDepthBiasBits_==std::array<std::uint32_t,3>{
                    std::bit_cast<std::uint32_t>(std::get<0>(values)),std::bit_cast<std::uint32_t>(std::get<1>(values)),
                    std::bit_cast<std::uint32_t>(std::get<2>(values))};
            }
            if (!sameIdentity || !sameSetter || !sameArguments) { Refuse(list, NativeRecordingHookRefusal::ReplayEffect, S_OK, Slot); Deny(list); replayInvalid_ = true; }

            return;
        }
        auto values = std::forward_as_tuple(args...);
        std::shared_ptr<const NativeIndirectEffects> indirectEffects;
        if constexpr (Slot == 59)
            indirectEffects = NativeIndirectSignatures::Read(std::get<0>(values),list);
        bool wasActive = false, unsupported = false;
        const auto observe = [&](auto&& action) {
            if constexpr (Slot == 29 || Slot == 30)
                return observer_.WithRootLayout(list, std::get<0>(values), action, Slot, &forwardingOwner);
            else return observer_.WithCurrent(list, action, Slot);
        };
        const bool accepted = observe([&]([[maybe_unused]] auto& state, [[maybe_unused]] auto incarnation) {
            wasActive = state.Activity(list, incarnation).has_value();
            if(!state.AdoptObjectOwners(incarnation,forwardingObjects))return false;
            if constexpr (Slot == 25) return state.ObservePipelinePrepared(incarnation, std::get<0>(values));
            else if constexpr (Slot == 75) return state.ObserveStateObjectPrepared(incarnation, std::get<0>(values));
            else if constexpr (Slot == 82) return state.ObserveDepthBias(incarnation,std::get<0>(values),std::get<1>(values),std::get<2>(values));
            else if constexpr (Slot == 66) return state.ObserveWriteBufferImmediate(incarnation);
            else if constexpr (Slot == 23) return state.ObserveBlendFactor(incarnation, std::get<0>(values));
            else if constexpr (Slot == 26) return state.ObserveBarriers(incarnation, std::get<0>(values), std::get<1>(values));
            else if constexpr (Slot == 80) return state.ObserveEnhancedBarriers(incarnation, std::get<0>(values), std::get<1>(values));
            else if constexpr (Slot == 28) return state.ObserveDescriptorHeapsPrepared(incarnation, std::get<0>(values), std::get<1>(values));
            else if constexpr (Slot == 29) return state.ObserveComputeRootSignaturePrepared(incarnation, std::get<0>(values));
            else if constexpr (Slot == 30) return state.ObserveGraphicsRootSignaturePrepared(incarnation, std::get<0>(values));
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
            else if constexpr ((Slot >= 12 && Slot <= 19) || (Slot >= 47 && Slot <= 50) || Slot == 76)
                return state.ObserveWork(incarnation);
            else if constexpr (Slot == 51)
                return state.ObserveDiscard(incarnation, std::get<0>(values), std::get<1>(values) != nullptr);
            else if constexpr (Slot == 52 || Slot == 53)
            {
                return state.ObserveQueryPrepared(incarnation,std::get<0>(values),std::get<1>(values),std::get<2>(values),Slot == 52);
            }
            else if constexpr (Slot == 54)
                return state.ObserveQueryResolve(incarnation,std::get<0>(values),std::get<1>(values),std::get<2>(values),std::get<3>(values));
            else if constexpr (Slot == 59) return state.ObserveIndirect(incarnation,indirectEffects.get(),
                reinterpret_cast<std::uint64_t>(std::get<0>(values)));
            else if constexpr ((Slot >= 20 && Slot <= 24) || (Slot >= 43 && Slot <= 46) || Slot == 62 || Slot == 77 || Slot == 78)
            {
                // Diagnostic scalars only: never dereference view/descriptor arrays
                // or query a resource. These facts grant no restoration authority.
                std::uint64_t a = 0, b = 0, c = 0;
                if constexpr (Slot == 20 || Slot == 24) a = std::get<0>(values);
                else if constexpr (Slot == 21 || Slot == 22 || Slot == 77)
                { a = std::get<0>(values); b = std::get<1>(values) ? 1 : 0; }
                else if constexpr (Slot == 23 || Slot == 43) a = std::get<0>(values) ? 1 : 0;
                else if constexpr (Slot == 44 || Slot == 45)
                { a = std::get<0>(values); b = std::get<1>(values); c = std::get<2>(values) ? 1 : 0; }
                else if constexpr (Slot == 46)
                { a = std::get<0>(values); b = std::get<2>(values) ? 1 : 0;
                  c = (std::get<1>(values) ? 1u : 0u) | (std::get<3>(values) ? 2u : 0u); }
                else if constexpr (Slot == 62)
                { a = std::bit_cast<std::uint32_t>(std::get<0>(values)); b = std::bit_cast<std::uint32_t>(std::get<1>(values)); }
                else if constexpr (Slot == 78) a = reinterpret_cast<std::uint64_t>(std::get<0>(values));
                return state.ObserveUnrestorableGraphics(incarnation, Slot, a, b, c);
            }
            else if constexpr (Slot >= 56 && Slot <= 58) return true; // Diagnostic markers only.
            else
            { unsupported = true; state.Taint(incarnation, "UnsupportedEffect", Slot); return false; } // Bundles, predication, other versioned effects.
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
        // Enhanced layouts cannot be inferred as legacy states. Discard also
        // ends any pending insertion's content provenance, independently above.
        if constexpr (Slot == 80 || Slot == 51)
            observer_.WithCurrent(list, [](auto& state, auto incarnation) { state.ForgetResourceFacts(incarnation); return true; });
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
        return result;
    }
    bool Install(ID3D12GraphicsCommandList* list)
    {
        std::vector<std::shared_ptr<const ForwardRoute>> retiredForwarding;
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
            if (coverage_.load())
            {
                // Discovery may call foreign interfaces. Bind any witnessed
                // loss to the entry/discovery which existed before those calls.
                const auto history = observer_.CaptureRouteHistory(list);
                auto** table = *reinterpret_cast<void***>(list);
                if (history.commandTable && table != history.commandTable)
                {
                    Refuse(list,NativeRecordingHookRefusal::VtableMismatch,S_OK,UINT32_MAX,GUID{},
                        reinterpret_cast<std::uint64_t>(history.commandTable),reinterpret_cast<std::uint64_t>(table));
                    observer_.InvalidateRouteHistory(history);
                    return false;
                }
                if (!CheckShape(list) || !Matches(list, history.commandTable ? history.commandTable : table))
                {
                    observer_.InvalidateRouteHistory(history);
                    return false;
                }
                return installed();
            }
            auto** table = *reinterpret_cast<void***>(list);
            ++forwardingGeneration_;
            ClearForwarding(retiredForwarding);
            coverage_.store(retainedCoverage_);
            if (!CheckShape(list) || !Matches(list, table)) { coverage_.store({}); return false; }
            observer_.SetHookCoverage(true, [this](const auto& identity) {
                return identity.type == D3D12_COMMAND_LIST_TYPE_DIRECT && identity.interfaceVersion == InterfaceVersion() && Matches(identity.nativeList.Get(), identity.commandTable);
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
        if (!Eligible(list)) return false;
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
            return identity.type == D3D12_COMMAND_LIST_TYPE_DIRECT && identity.interfaceVersion == InterfaceVersion() && Matches(identity.nativeList.Get(), identity.commandTable);
        });
        return installed();
    }
    bool WrapperHeapRouteValid(ID3D12GraphicsCommandList* caller) const
    {
        if (!caller || !wrapperHeapInstalled_.load()) return false;
        const auto table = *reinterpret_cast<void***>(caller);
        return table[28] == wrapperHeapMethod_.target &&
            std::memcmp(wrapperHeapMethod_.target,wrapperHeapMethod_.routePatch.data(),wrapperHeapMethod_.routePatch.size()) == 0 &&
            std::memcmp(wrapperHeapMethod_.patchTarget,wrapperHeapMethod_.patch.data(),wrapperHeapMethod_.patch.size()) == 0;
    }
    bool InstallWrapperHeapHook(ID3D12GraphicsCommandList* caller)
    {
        std::lock_guard lock(installationMutex_);
        if (!caller || !installed_ || !coverage_.load() || wrapperHeapPoisoned_) return false;
        if (wrapperHeapInstalled_.load()) return WrapperHeapRouteValid(caller);
        auto* target = (*reinterpret_cast<void***>(caller))[28];
        const auto nativeHeap = std::find_if(methods_.begin(),methods_.end(),[](const auto& method){return method.slot==28;});
        if (nativeHeap == methods_.end() || target == nativeHeap->target)
            return Refuse(caller, NativeRecordingHookRefusal::WrapperHeapRoute, E_NOTIMPL, 28);
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&WrapperHeapCall),&module)) return false;
        wrapperHeapOriginal_ = reinterpret_cast<WrapperHeapFn>(target);
        InstalledMethod method{28,reinterpret_cast<PVOID*>(&wrapperHeapOriginal_),reinterpret_cast<PVOID>(&WrapperHeapCall),target};
        std::vector<HANDLE> threads;
        if (!BeginTransaction(caller,threads,wrapperHeapPoisoned_)) { CloseThreads(threads); return false; }
        auto result = DetourAttachEx(method.original,method.detour,nullptr,&method.patchTarget,nullptr);
        if (result == NO_ERROR && method.patchTarget) {
            result = DetourTransactionCommit();
            if(result!=NO_ERROR)wrapperHeapPoisoned_=true;
        }
        else { if (DetourTransactionAbort() != NO_ERROR) wrapperHeapPoisoned_ = true; if(result==NO_ERROR)result=ERROR_INVALID_DATA; }
        CloseThreads(threads);
        if (result != NO_ERROR) return Refuse(caller,NativeRecordingHookRefusal::WrapperHeapRoute,result,28);
        std::memcpy(method.patch.data(),method.patchTarget,method.patch.size());
        std::memcpy(method.routePatch.data(),method.target,method.routePatch.size());
        wrapperHeapMethod_ = method;
        wrapperHeapInstalled_.store(true);
        return true;
    }
    bool WrapperQueryRouteValid(ID3D12GraphicsCommandList* caller) const
    {
        if (!caller || !wrapperQueryInstalled_.load()) return false;
        const auto table = *reinterpret_cast<void***>(caller);
        return table[53] == wrapperQueryMethod_.target &&
            std::memcmp(wrapperQueryMethod_.target,wrapperQueryMethod_.routePatch.data(),wrapperQueryMethod_.routePatch.size()) == 0 &&
            std::memcmp(wrapperQueryMethod_.patchTarget,wrapperQueryMethod_.patch.data(),wrapperQueryMethod_.patch.size()) == 0;
    }
    bool InstallWrapperQueryHook(ID3D12GraphicsCommandList* caller)
    {
        std::lock_guard lock(installationMutex_);
        if (!caller || !installed_ || !coverage_.load() || wrapperQueryPoisoned_) return false;
        if (wrapperQueryInstalled_.load()) return WrapperQueryRouteValid(caller);
        auto* target = (*reinterpret_cast<void***>(caller))[53];
        const auto nativeQuery = std::find_if(methods_.begin(),methods_.end(),[](const auto& method){return method.slot==53;});
        if (nativeQuery == methods_.end() || target == nativeQuery->target)
            return Refuse(caller, NativeRecordingHookRefusal::WrapperQueryRoute, E_NOTIMPL, 53);
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&WrapperQueryCall),&module)) return false;
        wrapperQueryOriginal_ = reinterpret_cast<WrapperQueryFn>(target);
        InstalledMethod method{53,reinterpret_cast<PVOID*>(&wrapperQueryOriginal_),reinterpret_cast<PVOID>(&WrapperQueryCall),target};
        std::vector<HANDLE> threads;
        if (!BeginTransaction(caller,threads,wrapperQueryPoisoned_)) { CloseThreads(threads); return false; }
        auto result = DetourAttachEx(method.original,method.detour,nullptr,&method.patchTarget,nullptr);
        if (result == NO_ERROR && method.patchTarget) {
            result = DetourTransactionCommit();
            if(result!=NO_ERROR)wrapperQueryPoisoned_=true;
        }
        else { if (DetourTransactionAbort() != NO_ERROR) wrapperQueryPoisoned_ = true; if(result==NO_ERROR)result=ERROR_INVALID_DATA; }
        CloseThreads(threads);
        if (result != NO_ERROR) return Refuse(caller,NativeRecordingHookRefusal::WrapperQueryRoute,result,53);
        std::memcpy(method.patch.data(),method.patchTarget,method.patch.size());
        std::memcpy(method.routePatch.data(),method.target,method.routePatch.size());
        wrapperQueryMethod_ = method;
        wrapperQueryInstalled_.store(true);
        return true;
    }
    void Revoke()
    {
        std::vector<std::shared_ptr<const ForwardRoute>> retiredForwarding;
        decltype(NativeWrapperHeapBindings::RetireAll()) retiredHeaps;
        {
        std::lock_guard lock(installationMutex_);
        coverage_.store({});
        ++forwardingGeneration_;
        ClearForwarding(retiredForwarding);
        retiredHeaps=NativeWrapperHeapBindings::RetireAll();
        observer_.SetHookCoverage(false);
        }
        queryInsertions_.CancelAll(NativeQueryInsertion::Reason::CoverageLost);
        // Deliberately retain callbacks, original trampolines, and their pinned
        // module until process exit. No thread-creation exclusion is available.
    }
    bool DeferPostQuery(ID3D12GraphicsCommandList* nativeList,
                        const NativeQueryInsertion::Boundary& producer,
                        std::shared_ptr<NativeQueryInsertion::Operation> operation,
                        ID3D12GraphicsCommandList* caller = nullptr)
    {
        if (!nativeList || !operation || producer.list!=reinterpret_cast<std::uintptr_t>(nativeList) ||
            QueryCallScope::InFlight(*this,nativeList)) return false;
        if (caller && caller != nativeList)
        {
            if (!InstallWrapperQueryHook(caller)) return false;
            // This CPU owner dies after recording/cancellation. GPU retirement
            // must retain only the renderer's external resources, never this object.
            struct Wrapped final : NativeQueryInsertion::Operation
            {
                NativeRecordingHooks& hooks;
                Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> caller;
                std::shared_ptr<NativeQueryInsertion::Operation> operation;
                Wrapped(NativeRecordingHooks& h, ID3D12GraphicsCommandList* c,
                        std::shared_ptr<NativeQueryInsertion::Operation> o)
                    : hooks(h), caller(c), operation(std::move(o)) {}
                NativeQueryInsertion::Outcome Record(const NativeQueryInsertion::Boundary& boundary) noexcept override
                {
                    // The actual intercepted outer instance may differ from
                    // the NGX parameter wrapper. Its hook target is checked and
                    // the End event already identifies this native recording.
                    auto* actualCaller=reinterpret_cast<ID3D12GraphicsCommandList*>(boundary.executionCaller);
                    if (!actualCaller || (boundary.executionCaller!=boundary.list&&!hooks.WrapperQueryRouteValid(actualCaller)))
                    { operation->Cancel(NativeQueryInsertion::Reason::ForwardingFailed); return NativeQueryInsertion::Outcome::Skipped; }
                    return operation->Record(boundary);
                }
                void Cancel(NativeQueryInsertion::Reason reason) noexcept override { operation->Cancel(reason); }
                void CancelAt(NativeQueryInsertion::Reason reason,std::uint32_t slot) noexcept override { operation->CancelAt(reason,slot); }
                bool AllowsRtvClear(const NativeQueryInsertion::Boundary& boundary, std::uintptr_t descriptor) noexcept override
                { return operation->AllowsRtvClear(boundary, descriptor); }
            };
            operation = std::make_shared<Wrapped>(*this, caller, std::move(operation));
        }
        NativeStateCaptureDiagnostic diagnostic;
        const auto observed = observer_.Observe(nativeList, &diagnostic);
        if (!observed.active || !observed.completeCoverage || !diagnostic.openQueryTotal ||
            QueryCallScope::InFlight(*this,nativeList) || observed.incarnation!=producer.incarnation ||
            observed.hookGeneration!=producer.hookGeneration || observed.workOrdinal!=producer.workOrdinal) return false;
        return queryInsertions_.Arm(producer, std::move(operation));
    }
    NativeQueryInsertion::Outcome RecordOwnedBarrier(NativeQueryInsertion::Boundary& cursor,
        std::uint64_t& serial,const D3D12_RESOURCE_BARRIER& barrier,
        bool (*guard)(void*) noexcept = nullptr,void* context = nullptr,
        ID3D12GraphicsCommandList* executionCaller=nullptr,const D3D12_RESOURCE_BARRIER* callerBarrier=nullptr)
    {
        using Outcome=NativeQueryInsertion::Outcome;
        const auto command=barrier;
        using H=Hook<26,decltype(&ID3D12GraphicsCommandList::ResourceBarrier)>;
        auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(cursor.list);
        if(!list||!serial||!H::original || QueryCallScope::InFlight(*this,list) || command.Flags!=D3D12_RESOURCE_BARRIER_FLAG_NONE ||
           (command.Type!=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION&&command.Type!=D3D12_RESOURCE_BARRIER_TYPE_UAV))return Outcome::Skipped;
        if(command.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
           (!command.Transition.pResource || command.Transition.StateBefore==command.Transition.StateAfter))return Outcome::Skipped;
        const bool wrapped=executionCaller&&executionCaller!=list;
        if(wrapped&&!callerBarrier)return Outcome::Skipped;
        const auto outerCommand=callerBarrier?*callerBarrier:command;
        if(outerCommand.Type!=command.Type||outerCommand.Flags!=command.Flags)return Outcome::Skipped;
        ID3D12Resource* terminalResource=nullptr;
        ID3D12Resource* outerResource=nullptr;
        if(command.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
            if(outerCommand.Transition.Subresource!=command.Transition.Subresource||
               outerCommand.Transition.StateBefore!=command.Transition.StateBefore||
               outerCommand.Transition.StateAfter!=command.Transition.StateAfter)return Outcome::Skipped;
            terminalResource=command.Transition.pResource;outerResource=outerCommand.Transition.pResource;
        } else {terminalResource=command.UAV.pResource;outerResource=outerCommand.UAV.pResource;}
        if(!wrapped&&outerResource!=terminalResource)return Outcome::Skipped;
        CallbackLease lease(*this);
        QueryCallScope call(*this,nullptr,list);
        NativeRecordingState::PreparedBarriers prepared;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> heldCaller;
        Microsoft::WRL::ComPtr<ID3D12Resource> heldOuterResource,heldTerminalResource;
        auto completed=cursor;std::uint64_t completedSerial=serial;
        bool journalEntered=false;
        const auto sameEpoch=[&]{
            std::uint64_t incarnation=0,work=0,generation=0;
            return observer_.ReadCurrentEpoch(list,incarnation,work,generation)&&
                incarnation==cursor.incarnation&&generation==cursor.hookGeneration;
        };
        const auto fail=[&]{
            // Never taint a genuinely newer recording created by reentry.
            observer_.WithCurrent(list,[&](auto& state,auto incarnation){
                if(incarnation==cursor.incarnation)state.Taint(incarnation,"Native.OwnedBarrier.Reentered",26);
                return false;
            });
            serial=0;return Outcome::Failed;
        };
        try {
            // All foreign ownership, then the nonretained source/selection/
            // LocalAction guard. No foreign call remains before journal check.
            prepared.Acquire(1,&command);
            if(wrapped) {
                {
                    auto interfaces=NativeRecordingInterfaces::Resolve(executionCaller);
                    if(FAILED(interfaces.result)||interfaces.terminal.Get()!=list)return Outcome::Skipped;
                    heldCaller=std::move(interfaces.caller);
                }
                heldOuterResource=outerResource;heldTerminalResource=terminalResource;
                if(bool(outerResource)!=bool(terminalResource))return Outcome::Skipped;
                if(outerResource) {
                    const auto resolved=DlssNr::NativeIdentity::Resolve<ID3D12Resource>(outerResource);
                    if(FAILED(resolved.result)||resolved.object.Get()!=terminalResource)return Outcome::Skipped;
                }
            }
            if(!prepared.valid || (guard&&!guard(context)) || !sameEpoch())return Outcome::Skipped;
            observer_.InvalidatePendingEffect(list);
            const bool entered=observer_.WithCurrent(list,[&](auto& state,auto incarnation){
                if(incarnation!=cursor.incarnation || !state.ResourceFactsCurrent(incarnation,cursor.workOrdinal,serial))return false;
                journalEntered=true;
                return state.ObserveBarriers(incarnation,1,&command)&&state.ObserveWork(incarnation)&&
                    state.ReadResourceFactCursor(incarnation,completed.workOrdinal,completedSerial);
            });
            if(!entered)return journalEntered?fail():Outcome::Skipped;
            if(wrapped)
                NativeReplayEffects::Invoke<26>(list,[&]{heldCaller->ResourceBarrier(1,&outerCommand);},UINT{1},&command);
            else
                NativeReplayEffects::InvokeOriginal<26>(list,[&]{H::original(list,1,&command);},UINT{1},&command);
            if(!sameEpoch())return fail();
            const bool published=observer_.WithCurrent(list,[&](auto& state,auto incarnation){
                return incarnation==cursor.incarnation&&
                    state.ResourceFactsCurrent(incarnation,completed.workOrdinal,completedSerial)&&
                    state.CompleteBarriers(incarnation,completedSerial,prepared);
            });
            if(!published)return fail();
            // Temporary Release can reenter too. Retire it before final proof.
            prepared.owners.clear();
            heldOuterResource.Reset();heldTerminalResource.Reset();heldCaller.Reset();
            // Eligibility loss cannot erase an authenticated cleanup cursor.
            // Further NR work needs a new eligibility check by the caller.
            if(!sameEpoch())return fail();
            const bool current=observer_.WithCurrent(list,[&](auto& state,auto incarnation){
                return incarnation==cursor.incarnation&&
                    state.ResourceFactsCurrent(incarnation,completed.workOrdinal,completedSerial);
            });
            if(!current)return fail();
            cursor=completed;serial=completedSerial;return Outcome::Recorded;
        } catch(...) { return journalEntered?fail():Outcome::Skipped; }
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
        // Check actual installed interface coverage, not device/model identity.
        if (count_ > 75 && Hook<75, decltype(&ID3D12GraphicsCommandList10::SetPipelineState1)>::original)
            result.stateObject = [this](ID3D12GraphicsCommandList* list, ID3D12StateObject* object) {
                CallbackLease lease(*const_cast<NativeRecordingHooks*>(this));
                if (!installed_ || !coverage_.load() || count_ <= 75) throw std::runtime_error("retired state-object setter");
                // Install/Reset qualified the versioned interface at this exact
                // native base pointer and restore revalidates its covered route.
                Replay<75>(Hook<75, decltype(&ID3D12GraphicsCommandList10::SetPipelineState1)>::original,
                    reinterpret_cast<ID3D12GraphicsCommandList4*>(list), object);
            };
        if(count_>82&&Hook<82,decltype(&ID3D12GraphicsCommandList10::RSSetDepthBias)>::original)
            result.depthBias=[this](ID3D12GraphicsCommandList* list,float bias,float clamp,float slope){
                CallbackLease lease(*const_cast<NativeRecordingHooks*>(this));
                if(!installed_||!coverage_.load()||count_<=82)throw std::runtime_error("retired depth-bias setter");
                Replay<82>(Hook<82,decltype(&ID3D12GraphicsCommandList10::RSSetDepthBias)>::original,
                    reinterpret_cast<ID3D12GraphicsCommandList9*>(list),bias,clamp,slope);
            };
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
        result.blendFactor = [this](auto... args) { CallbackLease lease(*const_cast<NativeRecordingHooks*>(this)); if (!installed_ || !coverage_.load()) throw std::runtime_error("retired original setter"); Replay<23>(Hook<23, decltype(&ID3D12GraphicsCommandList10::OMSetBlendFactor)>::original, args...); };
        return result;
    }
};
}

