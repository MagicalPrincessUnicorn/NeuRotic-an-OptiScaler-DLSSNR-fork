#pragma once
// NeuRotic / EspiOwnage. OBS-CLOSURE-1: descriptive CPU values, never resource rights.
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <limits>

namespace DlssNr::CandidateObserver {
enum class Kind : uint16_t { Committed, Placed, Failed, CapabilityOnly, Anomalous,
    ResultUnclassified, InterfaceUnclassified, Alternate, Coverage, Boundary, End, OwnerFact };
enum class EmitResult : uint8_t { Recorded, Disabled, Contended, BudgetRefused, QueueFull,
    Unsupported, Closing, CounterExhausted };
enum class Reason : uint32_t { None, Requested, Contended, Budget, QueueFull, Unsupported,
    CounterExhausted, Deadline, WriterFailure, SourceDetach, Nested, FileLimit };
// quota-reasons-1: ordered, value-only diagnostics; no resource/frame identity.
enum class BudgetCause : uint32_t { SessionData, SessionControl, WindowData, WindowControl,
    WindowQuotaSlots, ResourceWindow, FrameStreams, FrameOrder, FrameQuotaSlots, ResourceFrame, FrameData, Count };
constexpr uint32_t BudgetCauseCount=static_cast<uint32_t>(BudgetCause::Count);
constexpr const char* BudgetCauseNames[BudgetCauseCount]={"session_data","session_control","window_data","window_control",
    "window_quota_slots","resource_window","frame_streams","frame_order","frame_quota_slots","resource_frame","frame_data"};
enum class Retention : uint8_t { Retained, Stale, Superseded };
enum class ReadResult : uint8_t { Snapshot, Unavailable, Contended, Closed };
enum class ImportResult : uint8_t { Recorded, Unavailable, UnsupportedContract, Conflict };
// No current owner adapter is installed in OBS-CLOSURE-1. The bounded port
// refuses unsupported facts, preserving the distinction from verified evidence.
struct OwnerFact {
    uint16_t major=2,minor=0;
    bool available=false;
    uint64_t birthEpoch=0,creationSequence=0;
    char session[33]{},owner[65]{},contract[65]{},version[33]{},evidenceId[129]{},revision[33]{},scope[65]{};
};
struct Descriptor {
    uint64_t alignment=0, width=0;
    uint32_t dimension=0, height=0, format=0, layout=0, flags=0, sampleCount=0,
        sampleQuality=0, initialState=0;
    uint16_t depth=0, mips=0;
};
struct Event {
    uint16_t major=2, minor=0;
    Kind kind=Kind::Failed;
    uint16_t source=0;
    uint64_t sequence=0, epoch=0, window=0, elapsedMs=0;
    uint64_t nativeObservation=0, presentObservation=0;
    // PRIVATE integer correlation only. Never serialize these or dereference them later.
    uintptr_t resourceBits=0, deviceBits=0, heapBits=0;
    Descriptor descriptor{};
    uint64_t heapOffset=0;
    uint32_t result=0, heapType=0, cpuPage=0, memoryPool=0, creationNode=0,
        visibleNode=0, heapFlags=0, coverageMask=0;
    uint8_t iid[16]{};
    bool descriptorKnown=false, heapKnown=false, timeKnown=false;
    // Future value-only owner adapters; first-profile production creation uses unknown frame.
    bool frameKnown=false;
    uint32_t frameContract=0;
    uint64_t frameScope=0, frameGeneration=0, frameOrdinal=0;
    Reason reason=Reason::None;
};
struct CaptureStats {
    uint64_t enqueued=0, dropped=0, sequence=0, epoch=1;
    uint32_t lossMask=0, highWater=0,familyStates=0;
    bool dropExact=true, complete=true;
    Reason reason=Reason::None;
    uint64_t familyEnqueued[4]{},familyDropped[4]{};
    bool familyDropExact[4]{true,true,true,true};
    uint64_t budgetRefused[BudgetCauseCount]{};
    bool budgetRefusalExact[BudgetCauseCount]{};
};
struct Summary {
    uint64_t sequence=0, epoch=0, window=0, elapsedMs=0, token=0, predecessor=0;
    Descriptor descriptor{};
    Kind kind=Kind::Failed;
    Retention retention=Retention::Retained;
    bool timeKnown=false, descriptorKnown=false, depthSupported=false, mayAdmit=false;
    uint8_t cautions=0;
};
struct Snapshot {
    uint64_t publication=0, elapsedMs=0;
    CaptureStats coverage{};
    uint32_t count=0;
    Summary candidates[64]{};
};
constexpr bool Eligible(Kind k) noexcept { return k==Kind::Committed || k==Kind::Placed; }
constexpr bool Control(Kind k) noexcept { return k==Kind::Coverage || k==Kind::Boundary || k==Kind::End; }
constexpr Kind Classify(bool normal, uint32_t result, bool outputStorage,
                        bool objectPresent, bool resourceIid) noexcept {
    if(!normal) return Kind::Alternate;
    if(result & 0x80000000u) return Kind::Failed;
    if(!outputStorage) return Kind::CapabilityOnly;
    if(result!=0) return Kind::ResultUnclassified;
    if(!resourceIid) return Kind::InterfaceUnclassified;
    if(!objectPresent) return Kind::Anomalous;
    return Kind::Committed;
}
constexpr bool DepthSupport(const Descriptor& d) noexcept {
    return (d.flags&2)!=0 || d.format==20 || d.format==40 || d.format==45 || d.format==55;
}
static_assert(std::is_trivially_copyable_v<Event> && std::is_standard_layout_v<Event>);
static_assert(sizeof(Event)<=512 && sizeof(Summary)<=2048 && sizeof(Snapshot)<=65536);
static_assert(std::is_trivially_copyable_v<OwnerFact> && sizeof(OwnerFact)<=512);
static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free);
}
