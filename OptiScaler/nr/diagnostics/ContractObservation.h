#pragma once
// NR-DIAG-001 candidate; not applied, compiled or executed.
// Fixed in-process values only. Producers never format, log, wait, query GPU resources or retain native handles.

#include "../contracts/C11_Diagnostics.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace Neurotic::Diagnostics::M0
{
namespace C = Neurotic::Contracts;

enum class Mode : std::uint8_t
{
    Off,
    Shadow
};

enum class PublishResult : std::uint8_t
{
    Disabled,
    Accepted,
    QueueFull,
    Contended,
    ValidationRejected,
    UnsupportedReceipt,
    Saturated
};

enum class ExportDisposition : std::uint8_t
{
    Exported,
    ConsumerRejected,
    SerializationFailed,
    TransportUnavailable
};

inline Mode ParseMode(std::string_view text) noexcept
{
    return text == "Shadow" || text == "shadow" ? Mode::Shadow : Mode::Off;
}

inline bool Assign(C::Symbol& destination, std::string_view value) noexcept
{
    return destination.Assign(value);
}

inline C::RecordKey Key(std::string_view nameSpace, std::string_view issuer, std::uint64_t value) noexcept
{
    C::RecordKey result;
    Assign(result.nameSpace, nameSpace);
    Assign(result.issuer, issuer);
    result.value = value;
    return result;
}

inline C::EvidenceRef Evidence(const C::RecordKey& record) noexcept
{
    C::EvidenceRef evidence;
    evidence.record = record;
    return evidence;
}

inline C::UnknownFact Unknown(C::UnknownReason reason, std::string_view ownerCode = {}) noexcept
{
    C::UnknownFact unknown;
    unknown.reason.category = C::ReasonCategory::CoverageRestriction;
    unknown.reason.code = reason;
    if (!ownerCode.empty())
    {
        C::Symbol code;
        if (code.Assign(ownerCode))
            unknown.reason.ownerCode = code;
    }
    return unknown;
}

struct ObservedField
{
    C::Symbol field {};
    C::ScalarValue value {false};
    bool operator==(const ObservedField&) const = default;
};

struct SourceSnapshot
{
    static constexpr std::size_t FieldCapacity = 8;

    C::OwnerDomain owner = C::OwnerDomain::Unspecified;
    C::RecordKey publisher {};
    C::Symbol operation {};
    C::BoundedList<ObservedField, FieldCapacity> fields {};
    std::optional<C::RecordReference> ownerReceipt {};
    std::optional<C::MetadataRef<C::GenerationVector>> generations {};
    std::optional<C::MetadataRef<C::ResourceIdentityToken>> resourceIdentity {};
    std::optional<C::MetadataRef<C::FrameIdentity>> associations {};
    bool ownerPublicationAvailable = false;
    bool contentRevisionAvailable = false;
    bool valid = true;

    static SourceSnapshot OwnerPublication(C::OwnerDomain ownerDomain, std::string_view nameSpace,
                                           std::string_view issuer, std::uint64_t publisherValue,
                                           std::string_view operationName) noexcept
    {
        SourceSnapshot result;
        result.owner = ownerDomain;
        result.publisher = Key(nameSpace, issuer, publisherValue);
        result.ownerPublicationAvailable = true;
        result.valid = result.publisher.Check() == C::Error::None && result.operation.Assign(operationName) &&
                       ownerDomain != C::OwnerDomain::Unspecified && ownerDomain != C::OwnerDomain::Diagnostics;
        return result;
    }

    static SourceSnapshot OwnerReferences(C::OwnerDomain ownerDomain,
                                          std::string_view operationName) noexcept
    {
        SourceSnapshot result;
        result.owner = ownerDomain;
        result.valid = result.operation.Assign(operationName) &&
                       ownerDomain != C::OwnerDomain::Unspecified &&
                       ownerDomain != C::OwnerDomain::Diagnostics;
        return result;
    }

    static SourceSnapshot Unavailable(std::string_view operationName = "FrameTrace.Event") noexcept
    {
        SourceSnapshot result;
        result.operation.Assign(operationName);
        return result;
    }

    bool Add(std::string_view name, bool value) noexcept
    {
        return AddValue(name, C::ScalarValue {value});
    }

    bool Add(std::string_view name, std::uint64_t value) noexcept
    {
        return AddValue(name, C::ScalarValue {value});
    }

    bool Add(std::string_view name, double value) noexcept
    {
        return AddValue(name, C::ScalarValue {value});
    }

    bool AddSymbol(std::string_view name, std::string_view value) noexcept
    {
        C::Symbol symbol;
        if (!symbol.Assign(value))
        {
            valid = false;
            return false;
        }
        return AddValue(name, C::ScalarValue {symbol});
    }

    void SetOwnerReceipt(const C::RecordReference& value) noexcept { ownerReceipt = value; }
    void SetGenerations(const C::MetadataRef<C::GenerationVector>& value) noexcept { generations = value; }
    void SetResourceIdentity(const C::MetadataRef<C::ResourceIdentityToken>& value) noexcept
    {
        resourceIdentity = value;
    }
    void SetAssociations(const C::MetadataRef<C::FrameIdentity>& value) noexcept { associations = value; }
    void SetContentRevisionAvailable(bool value) noexcept { contentRevisionAvailable = value; }

  private:
    bool AddValue(std::string_view name, const C::ScalarValue& value) noexcept
    {
        ObservedField field;
        if (!field.field.Assign(name) || !fields.Push(ObservedField {field.field, value}))
        {
            valid = false;
            return false;
        }
        return true;
    }
};

enum class SourceStatus : std::uint8_t
{
    Valid,
    Invalid,
    UnsupportedReceipt
};

template<class T>
inline bool ValidOwnerMetadataReference(const C::MetadataRef<T>& reference) noexcept
{
    return reference.owner != C::OwnerDomain::Diagnostics &&
           reference.Check() == C::Error::None &&
           reference.record.Check() == C::Error::None &&
           reference.schemaVersion.Check() == C::Error::None;
}

inline SourceStatus Check(const SourceSnapshot& source) noexcept
{
    if (!source.valid || source.operation.Empty())
        return SourceStatus::Invalid;
    if (source.ownerPublicationAvailable)
    {
        if (source.owner == C::OwnerDomain::Unspecified || source.owner == C::OwnerDomain::Diagnostics ||
            source.publisher.Check() != C::Error::None)
            return SourceStatus::Invalid;
        for (const auto& field : source.fields)
            if (field.field.Empty())
                return SourceStatus::Invalid;
    }
    else if (source.fields.Size() != 0)
    {
        // Values cannot become mirrored owner facts without an owner-authorized publication seam.
        return SourceStatus::Invalid;
    }
    if (source.ownerReceipt)
    {
        if (source.owner == C::OwnerDomain::Unspecified || source.owner == C::OwnerDomain::Diagnostics)
            return SourceStatus::UnsupportedReceipt;
        if (source.ownerReceipt->contract != C::ContractId::C11 ||
            source.ownerReceipt->recordType.View() != C::OwnerReceipt::WireName)
            return SourceStatus::UnsupportedReceipt;
        if (source.ownerReceipt->record.Check() != C::Error::None)
            return SourceStatus::Invalid;
    }
    if ((source.generations && !ValidOwnerMetadataReference(*source.generations)) ||
        (source.resourceIdentity && !ValidOwnerMetadataReference(*source.resourceIdentity)) ||
        (source.associations && !ValidOwnerMetadataReference(*source.associations)))
        return SourceStatus::Invalid;
    return SourceStatus::Valid;
}

class AtomicTryGate
{
    std::atomic_flag held_ = ATOMIC_FLAG_INIT;

  public:
    bool TryEnter() noexcept { return !held_.test_and_set(std::memory_order_acquire); }
    void Leave() noexcept { held_.clear(std::memory_order_release); }
};

struct DropCounters
{
    std::uint64_t queueFull = 0;
    std::optional<std::uint64_t> producerContention = std::uint64_t {0};
    std::uint64_t validationRejected = 0;
    std::uint64_t unsupportedEventEncoding = 0;
    std::uint64_t consumerExportDropped = 0;
    std::uint64_t consumerSerializationFailed = 0;
    std::uint64_t transportUnavailableDrop = 0;
    bool saturated = false;
};

struct CoverageCounters
{
    std::uint64_t observedEvents = 0;
    std::uint64_t ownerPublicationUnavailable = 0;
    std::uint64_t unsupportedReceiptType = 0;
    std::uint64_t missingAssociation = 0;
    std::uint64_t missingGeneration = 0;
    std::uint64_t missingContentRevision = 0;
    std::uint64_t observerDisabled = 0;
    std::uint64_t transportUnavailable = 0;
    std::uint64_t partialExport = 0;
    bool saturated = false;
};

struct SessionCounters
{
    Mode observationMode = Mode::Off;
    std::uint64_t queueCapacity = 0;
    std::uint64_t attemptedPublications = 0;
    std::uint64_t successfullyEnqueued = 0;
    std::uint64_t drainedEvents = 0;
    std::uint64_t exportedEvents = 0;
    std::uint64_t queuedEvents = 0;
    bool accountingComplete = true;
};

struct CounterSnapshot
{
    DropCounters drops {};
    CoverageCounters coverage {};
    SessionCounters session {};
};

struct ObservationDraft
{
    std::uint64_t producerSequence = 0;
    C::Symbol eventType {};
    SourceSnapshot source {};
    std::uint64_t frameTraceNativeObservation = 0;
    std::uint64_t frameTracePresentObservation = 0;
    bool saturatedAtPublication = false;
};

struct EventClosure
{
    C::DiagnosticEvent event {};
    C::DiagnosticCoverage coverage {};
    C::GenerationVector generationFallback {};
    C::FrameIdentity associationFallback {};
    C::BoundedList<C::MirroredOwnerFact, 16> mirroredFacts {};
    C::BoundedList<C::OwnerFact, 16> envelopeFacts {};
    C::BoundedList<C::Symbol, 16> missingOwners {};
};

struct SummaryBundle
{
    CounterSnapshot snapshot {};
    std::array<EventClosure, 4> pages {};
};

struct ExportSink
{
    using Function = ExportDisposition (*)(void*, const EventClosure&) noexcept;
    void* context = nullptr;
    Function function = nullptr;
};

namespace Detail
{
inline bool Increment(std::uint64_t& value, bool& saturated) noexcept
{
    if (value == (std::numeric_limits<std::uint64_t>::max)())
    {
        saturated = true;
        return false;
    }
    ++value;
    return true;
}

inline bool Add(C::BoundedList<C::OwnerFact, 16>& destination, std::string_view field,
                const C::OptionalFact<C::ScalarValue>& value) noexcept
{
    C::OwnerFact fact;
    if (!fact.field.Assign(field))
        return false;
    fact.value = value;
    return destination.Push(fact);
}

inline C::OptionalFact<C::ScalarValue> Known(const C::ScalarValue& value, const C::RecordKey& evidence) noexcept
{
    return C::OptionalFact<C::ScalarValue>::FromKnown(value, Evidence(evidence));
}

inline C::OptionalFact<C::ScalarValue> MissingCounter(std::string_view ownerCode) noexcept
{
    return C::OptionalFact<C::ScalarValue>::FromUnknown(Unknown(C::UnknownReason::NotObserved, ownerCode));
}

inline C::MetadataRef<C::GenerationVector> GenerationRef(std::uint64_t sequence) noexcept
{
    C::MetadataRef<C::GenerationVector> ref;
    ref.owner = C::OwnerDomain::Diagnostics;
    ref.record = Key("Neurotic.M0", "Diagnostics", sequence * 16 + 2);
    ref.revision = 1;
    return ref;
}

inline C::MetadataRef<C::FrameIdentity> AssociationRef(std::uint64_t sequence) noexcept
{
    C::MetadataRef<C::FrameIdentity> ref;
    ref.owner = C::OwnerDomain::Diagnostics;
    ref.record = Key("Neurotic.M0", "Diagnostics", sequence * 16 + 3);
    ref.revision = 1;
    return ref;
}

inline C::MetadataRef<C::DiagnosticCoverage> CoverageRef(std::uint64_t sequence) noexcept
{
    C::MetadataRef<C::DiagnosticCoverage> ref;
    ref.owner = C::OwnerDomain::Diagnostics;
    ref.record = Key("Neurotic.M0", "Diagnostics", sequence * 16 + 4);
    ref.revision = 1;
    return ref;
}

template<class T, std::size_t N>
inline C::MetadataList<T, N> ListRef(const C::BoundedList<T, N>& values, std::uint64_t sequence,
                                     std::uint64_t suffix) noexcept
{
    C::MetadataList<T, N> result;
    result.count = static_cast<std::uint32_t>(values.Size());
    if (result.count != 0)
    {
        C::MetadataRef<C::BoundedList<T, N>> backing;
        backing.owner = C::OwnerDomain::Diagnostics;
        backing.record = Key("Neurotic.M0", "Diagnostics", sequence * 16 + suffix);
        backing.revision = 1;
        result.backing = backing;
    }
    return result;
}

inline void BeginEvent(EventClosure& output, std::uint64_t sequence, std::string_view eventType) noexcept
{
    output = {};
    output.event.header.contract = C::ContractId::C11;
    output.event.header.owner = C::OwnerDomain::Diagnostics;
    output.event.header.record = Key("Neurotic.M0", "Diagnostics", sequence * 16 + 1);
    output.event.header.revision = 1;
    output.event.header.scope.key = Key("Neurotic.M0", "Session", 1);
    output.event.producerSequence = sequence;
    output.event.eventType.Assign(eventType);
    output.event.timestamp = C::OptionalFact<C::MetadataRef<C::Measurement>>::FromUnknown(
        Unknown(C::UnknownReason::NotObserved, "DIAG.NoProducerTimestamp"));
    output.event.relevantGenerations = GenerationRef(sequence);
    output.event.associations = AssociationRef(sequence);
    output.event.coverage = CoverageRef(sequence);
    output.event.ownerReceipt = C::OptionalFact<C::RecordReference>::FromUnknown(
        Unknown(C::UnknownReason::OwnerUnpublished, "DIAG.NoOwnerReceipt"));
    output.event.resourceIdentity = C::OptionalFact<C::MetadataRef<C::ResourceIdentityToken>>::FromUnknown(
        Unknown(C::UnknownReason::OwnerUnpublished, "DIAG.NoResourceIdentity"));
}

inline void AddEnvelopeKnown(EventClosure& output, std::string_view field, const C::ScalarValue& value) noexcept
{
    Add(output.envelopeFacts, field, Known(value, output.event.header.record));
}

inline void AddEnvelopeUnknown(EventClosure& output, std::string_view field, std::string_view ownerCode) noexcept
{
    Add(output.envelopeFacts, field, MissingCounter(ownerCode));
}

inline void FinalizeEnvelope(EventClosure& output, std::uint64_t sequence) noexcept
{
    output.event.mirroredFacts = ListRef(output.mirroredFacts, sequence, 5);
    output.event.envelopeFacts = ListRef(output.envelopeFacts, sequence, 6);
    output.coverage.missingOwners = ListRef(output.missingOwners, sequence, 7);
}
} // namespace Detail

template<std::size_t Capacity = 64, std::uint64_t PublicationBudget = 65536, class Gate = AtomicTryGate>
class Observer
{
    static_assert(Capacity != 0, "The M0 queue must have at least one slot.");
    static_assert(PublicationBudget != 0, "The M0 publication budget must be nonzero.");
    static_assert(PublicationBudget < (((std::numeric_limits<std::uint64_t>::max)() - 15) / 16) - 16,
                  "The M0 publication budget must leave record-key space for summary pages.");

    Gate gate_ {};
    std::array<ObservationDraft, Capacity> queue_ {};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t count_ = 0;
    std::uint64_t nextProducerSequence_ = 1;
    std::uint64_t summarySequence_ = 1;
    DropCounters drops_ {};
    CoverageCounters coverage_ {};
    SessionCounters session_ {};
    std::atomic<std::uint32_t> producerContention_ {0};
    std::atomic<bool> producerContentionIncomplete_ {false};
    std::atomic<bool> asynchronousSaturation_ {false};
    std::atomic<bool> consumerAccountingIncomplete_ {false};
    // 0 = unconfigured (operationally Off), 1 = configured Off, 2 = configured Shadow.
    std::atomic<std::uint8_t> configuration_ {0};

  public:
    static constexpr std::size_t QueueCapacity = Capacity;

    Observer() noexcept
    {
        static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                      "M0 requires lock-free 32-bit producer contention accounting.");
        session_.queueCapacity = Capacity;
    }

    explicit Observer(Mode mode) noexcept : Observer()
    {
        configuration_.store(mode == Mode::Shadow ? std::uint8_t {2} : std::uint8_t {1},
                             std::memory_order_relaxed);
    }

    Observer(const Observer&) = delete;
    Observer& operator=(const Observer&) = delete;

    bool ConfigureOnce(Mode mode) noexcept
    {
        std::uint8_t expected = 0;
        const std::uint8_t desired = mode == Mode::Shadow ? std::uint8_t {2} : std::uint8_t {1};
        return configuration_.compare_exchange_strong(expected, desired, std::memory_order_release,
                                                      std::memory_order_acquire);
    }

    Mode CurrentMode() const noexcept
    {
        return configuration_.load(std::memory_order_acquire) == 2 ? Mode::Shadow : Mode::Off;
    }
    bool Enabled() const noexcept { return CurrentMode() == Mode::Shadow; }

    template<class Builder>
    PublishResult ObserveLazy(std::string_view eventType, Builder&& builder,
                              std::uint64_t nativeObservation = 0,
                              std::uint64_t presentObservation = 0) noexcept
    {
        if (!Enabled())
            return PublishResult::Disabled;
        try
        {
            const SourceSnapshot source = builder();
            return TryPublish(eventType, &source, nativeObservation, presentObservation);
        }
        catch (...)
        {
            NoteValidationRejected();
            return PublishResult::ValidationRejected;
        }
    }

    PublishResult TryPublish(std::string_view eventType, const SourceSnapshot* source,
                             std::uint64_t nativeObservation = 0,
                             std::uint64_t presentObservation = 0) noexcept
    {
        if (!Enabled())
            return PublishResult::Disabled;
        if (!gate_.TryEnter())
        {
            NoteProducerContention();
            return PublishResult::Contended;
        }

        Detail::Increment(session_.attemptedPublications, drops_.saturated);
        Detail::Increment(coverage_.observedEvents, coverage_.saturated);

        C::Symbol encodedEvent;
        if (!encodedEvent.Assign(eventType))
        {
            Detail::Increment(drops_.unsupportedEventEncoding, drops_.saturated);
            gate_.Leave();
            return PublishResult::ValidationRejected;
        }

        const SourceSnapshot unavailable = SourceSnapshot::Unavailable();
        const SourceSnapshot& selected = source != nullptr ? *source : unavailable;
        const SourceStatus sourceStatus = Check(selected);
        if (sourceStatus == SourceStatus::UnsupportedReceipt)
        {
            Detail::Increment(coverage_.unsupportedReceiptType, coverage_.saturated);
            gate_.Leave();
            return PublishResult::UnsupportedReceipt;
        }
        if (sourceStatus != SourceStatus::Valid)
        {
            Detail::Increment(drops_.validationRejected, drops_.saturated);
            gate_.Leave();
            return PublishResult::ValidationRejected;
        }

        if (!selected.ownerPublicationAvailable)
            Detail::Increment(coverage_.ownerPublicationUnavailable, coverage_.saturated);
        if (!selected.associations)
            Detail::Increment(coverage_.missingAssociation, coverage_.saturated);
        if (!selected.generations)
            Detail::Increment(coverage_.missingGeneration, coverage_.saturated);
        if (!selected.contentRevisionAvailable)
            Detail::Increment(coverage_.missingContentRevision, coverage_.saturated);

        if (nextProducerSequence_ > PublicationBudget)
        {
            drops_.saturated = true;
            coverage_.saturated = true;
            gate_.Leave();
            return PublishResult::Saturated;
        }
        if (count_ == Capacity)
        {
            Detail::Increment(drops_.queueFull, drops_.saturated);
            gate_.Leave();
            return PublishResult::QueueFull;
        }

        ObservationDraft& slot = queue_[tail_];
        slot = {};
        slot.producerSequence = nextProducerSequence_++;
        slot.eventType = encodedEvent;
        slot.source = selected;
        slot.frameTraceNativeObservation = nativeObservation;
        slot.frameTracePresentObservation = presentObservation;
        slot.saturatedAtPublication = drops_.saturated || coverage_.saturated ||
                                      asynchronousSaturation_.load(std::memory_order_relaxed);
        tail_ = (tail_ + 1) % Capacity;
        ++count_;
        Detail::Increment(session_.successfullyEnqueued, drops_.saturated);
        gate_.Leave();
        return PublishResult::Accepted;
    }

    bool TryPop(EventClosure& output) noexcept
    {
        ObservationDraft draft;
        if (!gate_.TryEnter())
            return false;
        if (count_ == 0)
        {
            gate_.Leave();
            return false;
        }
        draft = queue_[head_];
        queue_[head_] = {};
        head_ = (head_ + 1) % Capacity;
        --count_;
        Detail::Increment(session_.drainedEvents, drops_.saturated);
        gate_.Leave();
        return BuildEvent(draft, output);
    }

    bool DrainOne(const ExportSink& sink) noexcept
    {
        EventClosure event;
        if (!TryPop(event))
            return false;
        ExportDisposition disposition = ExportDisposition::TransportUnavailable;
        if (sink.function != nullptr)
        {
            try
            {
                disposition = sink.function(sink.context, event);
            }
            catch (...)
            {
                disposition = ExportDisposition::SerializationFailed;
            }
        }
        AcknowledgeExport(disposition);
        return true;
    }

    void AcknowledgeExport(ExportDisposition disposition) noexcept
    {
        if (!gate_.TryEnter())
        {
            consumerAccountingIncomplete_.store(true, std::memory_order_relaxed);
            return;
        }
        switch (disposition)
        {
        case ExportDisposition::Exported:
            Detail::Increment(session_.exportedEvents, drops_.saturated);
            break;
        case ExportDisposition::ConsumerRejected:
            Detail::Increment(drops_.consumerExportDropped, drops_.saturated);
            Detail::Increment(coverage_.partialExport, coverage_.saturated);
            break;
        case ExportDisposition::SerializationFailed:
            Detail::Increment(drops_.consumerSerializationFailed, drops_.saturated);
            Detail::Increment(coverage_.partialExport, coverage_.saturated);
            break;
        case ExportDisposition::TransportUnavailable:
            Detail::Increment(drops_.transportUnavailableDrop, drops_.saturated);
            Detail::Increment(coverage_.transportUnavailable, coverage_.saturated);
            Detail::Increment(coverage_.partialExport, coverage_.saturated);
            break;
        }
        gate_.Leave();
    }

    CounterSnapshot Snapshot() noexcept
    {
        CounterSnapshot result;
        if (!gate_.TryEnter())
        {
            result.session.observationMode = CurrentMode();
            result.session.queueCapacity = Capacity;
            result.session.accountingComplete = false;
            result.drops.producerContention.reset();
            return result;
        }
        result.drops = drops_;
        result.coverage = coverage_;
        result.session = session_;
        result.session.observationMode = CurrentMode();
        if (consumerAccountingIncomplete_.load(std::memory_order_relaxed))
            result.session.accountingComplete = false;
        result.session.queueCapacity = Capacity;
        result.session.queuedEvents = count_;
        result.coverage.observerDisabled = CurrentMode() == Mode::Off ? 1 : 0;
        const std::uint32_t contention = producerContention_.load(std::memory_order_relaxed);
        if (producerContentionIncomplete_.load(std::memory_order_relaxed))
        {
            result.drops.producerContention.reset();
            result.session.accountingComplete = false;
        }
        else
        {
            result.drops.producerContention = static_cast<std::uint64_t>(contention);
        }
        result.drops.saturated = result.drops.saturated || asynchronousSaturation_.load(std::memory_order_relaxed);
        result.coverage.saturated = result.coverage.saturated || result.drops.saturated;
        gate_.Leave();
        return result;
    }

    bool BuildSummary(SummaryBundle& output) noexcept
    {
        if (!gate_.TryEnter())
            return false;
        constexpr std::uint64_t maxEventSequence =
            ((std::numeric_limits<std::uint64_t>::max)() - 15) / 16;
        if (summarySequence_ > (maxEventSequence - PublicationBudget - 8) / 8)
        {
            drops_.saturated = true;
            coverage_.saturated = true;
            gate_.Leave();
            return false;
        }
        const std::uint64_t sequenceBase = PublicationBudget + summarySequence_++ * 8;
        gate_.Leave();
        output.snapshot = Snapshot();
        // BuildSummary is the bounded terminal/session view. If Shadow accepted work but no
        // consumer ever drained it, report the frozen transport-unavailable state rather than
        // inventing a fifth "NoConsumer" outcome outside the C11 vocabulary.
        if (output.snapshot.session.observationMode == Mode::Shadow &&
            output.snapshot.session.successfullyEnqueued != 0 &&
            output.snapshot.session.drainedEvents == 0 &&
            output.snapshot.coverage.transportUnavailable == 0)
            output.snapshot.coverage.transportUnavailable = 1;
        BuildDropPage(output.snapshot, sequenceBase + 1, output.pages[0]);
        BuildCoveragePage(output.snapshot, sequenceBase + 2, output.pages[1]);
        BuildFacetPage(output.snapshot, sequenceBase + 3, output.pages[2]);
        BuildSessionPage(output.snapshot, sequenceBase + 4, output.pages[3]);
        return true;
    }

  private:
    void NoteProducerContention() noexcept
    {
        std::uint32_t current = producerContention_.load(std::memory_order_relaxed);
        if (current == (std::numeric_limits<std::uint32_t>::max)())
        {
            asynchronousSaturation_.store(true, std::memory_order_relaxed);
            return;
        }
        // One bounded CAS attempt only. A race makes the count explicitly incomplete; it never retries or waits.
        if (!producerContention_.compare_exchange_strong(current, current + 1,
                                                         std::memory_order_relaxed,
                                                         std::memory_order_relaxed))
            producerContentionIncomplete_.store(true, std::memory_order_relaxed);
    }

    void NoteValidationRejected() noexcept
    {
        if (!gate_.TryEnter())
        {
            NoteProducerContention();
            return;
        }
        Detail::Increment(drops_.validationRejected, drops_.saturated);
        gate_.Leave();
    }

    static bool BuildEvent(const ObservationDraft& draft, EventClosure& output) noexcept
    {
        Detail::BeginEvent(output, draft.producerSequence, draft.eventType.View());
        const C::RecordKey diagnosticEvidence = output.event.header.record;

        if (draft.source.generations)
            output.event.relevantGenerations = *draft.source.generations;
        if (draft.source.associations)
            output.event.associations = *draft.source.associations;
        if (draft.source.resourceIdentity)
            output.event.resourceIdentity = C::OptionalFact<C::MetadataRef<C::ResourceIdentityToken>>::FromKnown(
                *draft.source.resourceIdentity, Evidence(draft.source.resourceIdentity->record));
        if (draft.source.ownerReceipt)
            output.event.ownerReceipt = C::OptionalFact<C::RecordReference>::FromKnown(
                *draft.source.ownerReceipt, Evidence(draft.source.ownerReceipt->record));

        if (draft.source.ownerPublicationAvailable)
        {
            for (const auto& sourceField : draft.source.fields)
            {
                C::MirroredOwnerFact fact;
                fact.owner = draft.source.owner;
                fact.publisher = draft.source.publisher;
                fact.field = sourceField.field;
                fact.value = C::OptionalFact<C::ScalarValue>::FromKnown(sourceField.value,
                                                                        Evidence(draft.source.publisher));
                if (!output.mirroredFacts.Push(fact))
                    return false;
            }
        }
        else
        {
            C::Symbol missing;
            missing.Assign("OwnerPublication");
            output.missingOwners.Push(missing);
        }

        Detail::AddEnvelopeKnown(output, "source.operation", C::ScalarValue {draft.source.operation});
        C::Symbol ownerName;
        ownerName.Assign(draft.source.owner != C::OwnerDomain::Unspecified ?
                             C::EnumName(draft.source.owner) : "Unspecified");
        Detail::AddEnvelopeKnown(output, "source.owner", C::ScalarValue {ownerName});
        Detail::AddEnvelopeKnown(output, "source.publisherAvailable",
                                 C::ScalarValue {draft.source.ownerPublicationAvailable});
        Detail::AddEnvelopeKnown(output, "source.ownerReceiptAvailable",
                                 C::ScalarValue {draft.source.ownerReceipt.has_value()});
        Detail::AddEnvelopeKnown(output, "source.canonicalGenerationsAvailable",
                                 C::ScalarValue {draft.source.generations.has_value()});
        Detail::AddEnvelopeKnown(output, "source.canonicalAssociationAvailable",
                                 C::ScalarValue {draft.source.associations.has_value()});
        Detail::AddEnvelopeKnown(output, "source.contentRevisionAvailable",
                                 C::ScalarValue {draft.source.contentRevisionAvailable});
        Detail::AddEnvelopeKnown(output, "frameTrace.nativeObservation",
                                 C::ScalarValue {draft.frameTraceNativeObservation});
        Detail::AddEnvelopeKnown(output, "frameTrace.presentObservation",
                                 C::ScalarValue {draft.frameTracePresentObservation});
        Detail::AddEnvelopeKnown(output, "frameTrace.identifiersAreDiagnosticOnly", C::ScalarValue {true});

        output.coverage.produced = C::OptionalFact<std::uint64_t>::FromKnown(1, Evidence(diagnosticEvidence));
        output.coverage.accepted = C::OptionalFact<std::uint64_t>::FromKnown(1, Evidence(diagnosticEvidence));
        output.coverage.dropped = C::OptionalFact<std::uint64_t>::FromKnown(0, Evidence(diagnosticEvidence));
        const bool complete = draft.source.ownerPublicationAvailable && draft.source.generations.has_value() &&
                              draft.source.associations.has_value() && draft.source.contentRevisionAvailable;
        output.coverage.complete = C::OptionalFact<bool>::FromKnown(complete, Evidence(diagnosticEvidence));
        output.coverage.saturated = C::OptionalFact<bool>::FromKnown(draft.saturatedAtPublication,
                                                                     Evidence(diagnosticEvidence));
        Detail::FinalizeEnvelope(output, draft.producerSequence);
        return output.event.Check() == C::Error::None && output.event.mirroredFacts.Check() == C::Error::None &&
               output.event.envelopeFacts.Check() == C::Error::None && output.coverage.missingOwners.Check() == C::Error::None;
    }

    static void BeginSummaryPage(EventClosure& output, std::uint64_t sequence, std::string_view eventType,
                                 bool complete, bool saturated) noexcept
    {
        Detail::BeginEvent(output, sequence, eventType);
        const C::RecordKey evidence = output.event.header.record;
        output.coverage.produced = C::OptionalFact<std::uint64_t>::FromKnown(1, Evidence(evidence));
        output.coverage.accepted = C::OptionalFact<std::uint64_t>::FromKnown(1, Evidence(evidence));
        output.coverage.dropped = C::OptionalFact<std::uint64_t>::FromKnown(0, Evidence(evidence));
        output.coverage.complete = C::OptionalFact<bool>::FromKnown(complete, Evidence(evidence));
        output.coverage.saturated = C::OptionalFact<bool>::FromKnown(saturated, Evidence(evidence));
    }

    static void AddCounter(EventClosure& output, std::string_view field,
                           const std::optional<std::uint64_t>& value) noexcept
    {
        if (value)
            Detail::AddEnvelopeKnown(output, field, C::ScalarValue {*value});
        else
            Detail::AddEnvelopeUnknown(output, field, "DIAG.CounterIncomplete");
    }

    static void AddCounter(EventClosure& output, std::string_view field, std::uint64_t value) noexcept
    {
        AddCounter(output, field, std::optional<std::uint64_t> {value});
    }

    static void AddBoolean(EventClosure& output, std::string_view field, bool value) noexcept
    {
        Detail::AddEnvelopeKnown(output, field, C::ScalarValue {value});
    }

    static void AddSymbol(EventClosure& output, std::string_view field, std::string_view value) noexcept
    {
        C::Symbol symbol;
        symbol.Assign(value);
        Detail::AddEnvelopeKnown(output, field, C::ScalarValue {symbol});
    }

    static void FinishSummaryPage(EventClosure& output, std::uint64_t sequence) noexcept
    {
        Detail::FinalizeEnvelope(output, sequence);
    }

    static void BuildDropPage(const CounterSnapshot& snapshot, std::uint64_t sequence,
                              EventClosure& output) noexcept
    {
        BeginSummaryPage(output, sequence, "M0.DropCounters", snapshot.session.accountingComplete,
                         snapshot.drops.saturated);
        AddCounter(output, "DropCounters.queueFull", snapshot.drops.queueFull);
        AddCounter(output, "DropCounters.producerContention", snapshot.drops.producerContention);
        AddCounter(output, "DropCounters.validationRejected", snapshot.drops.validationRejected);
        AddCounter(output, "DropCounters.unsupportedEventEncoding", snapshot.drops.unsupportedEventEncoding);
        AddCounter(output, "DropCounters.consumerExportDropped", snapshot.drops.consumerExportDropped);
        AddCounter(output, "DropCounters.consumerSerializationFailed",
                   snapshot.drops.consumerSerializationFailed);
        AddCounter(output, "DropCounters.transportUnavailableDrop", snapshot.drops.transportUnavailableDrop);
        AddBoolean(output, "DropCounters.saturated", snapshot.drops.saturated);
        FinishSummaryPage(output, sequence);
    }

    static void BuildCoveragePage(const CounterSnapshot& snapshot, std::uint64_t sequence,
                                  EventClosure& output) noexcept
    {
        BeginSummaryPage(output, sequence, "M0.CoverageCounters", snapshot.session.accountingComplete,
                         snapshot.coverage.saturated);
        AddCounter(output, "CoverageCounters.observedEvents", snapshot.coverage.observedEvents);
        AddCounter(output, "CoverageCounters.ownerPublicationUnavailable",
                   snapshot.coverage.ownerPublicationUnavailable);
        AddCounter(output, "CoverageCounters.unsupportedReceiptType", snapshot.coverage.unsupportedReceiptType);
        AddCounter(output, "CoverageCounters.missingAssociation", snapshot.coverage.missingAssociation);
        AddCounter(output, "CoverageCounters.missingGeneration", snapshot.coverage.missingGeneration);
        AddCounter(output, "CoverageCounters.missingContentRevision", snapshot.coverage.missingContentRevision);
        AddCounter(output, "CoverageCounters.observerDisabled", snapshot.coverage.observerDisabled);
        AddCounter(output, "CoverageCounters.transportUnavailable", snapshot.coverage.transportUnavailable);
        AddCounter(output, "CoverageCounters.partialExport", snapshot.coverage.partialExport);
        AddBoolean(output, "CoverageCounters.saturated", snapshot.coverage.saturated);
        FinishSummaryPage(output, sequence);
    }

    static const char* TransportFacet(const CounterSnapshot& snapshot) noexcept
    {
        if (snapshot.session.observationMode == Mode::Off)
            return "Unknown";
        if (snapshot.coverage.transportUnavailable != 0 || snapshot.drops.transportUnavailableDrop != 0)
            return "Unavailable";
        if (snapshot.drops.consumerExportDropped != 0 || snapshot.drops.consumerSerializationFailed != 0)
            return "DroppedAfterQueue";
        if (snapshot.drops.queueFull != 0 || snapshot.drops.validationRejected != 0 ||
            snapshot.drops.unsupportedEventEncoding != 0 || snapshot.coverage.unsupportedReceiptType != 0 ||
            (snapshot.drops.producerContention && *snapshot.drops.producerContention != 0))
            return "DroppedBeforeQueue";
        if (snapshot.session.exportedEvents != 0)
            return "Published";
        return "Unknown";
    }

    static void BuildFacetPage(const CounterSnapshot& snapshot, std::uint64_t sequence,
                               EventClosure& output) noexcept
    {
        // All declared loss/missing-coverage paths degrade diagnostic completeness,
        // including attempts rejected before the per-field counters were visited.
        // Never sum counters here: saturation must not wrap into an apparent zero.
        const bool impaired = !snapshot.session.accountingComplete || snapshot.drops.saturated ||
                              snapshot.coverage.saturated || snapshot.drops.queueFull != 0 ||
                              !snapshot.drops.producerContention.has_value() ||
                              snapshot.drops.producerContention.value_or(0) != 0 ||
                              snapshot.drops.validationRejected != 0 ||
                              snapshot.drops.unsupportedEventEncoding != 0 ||
                              snapshot.drops.consumerExportDropped != 0 ||
                              snapshot.drops.consumerSerializationFailed != 0 ||
                              snapshot.drops.transportUnavailableDrop != 0 ||
                              snapshot.coverage.ownerPublicationUnavailable != 0 ||
                              snapshot.coverage.unsupportedReceiptType != 0 ||
                              snapshot.coverage.missingAssociation != 0 ||
                              snapshot.coverage.missingGeneration != 0 ||
                              snapshot.coverage.missingContentRevision != 0 ||
                              snapshot.coverage.observerDisabled != 0 ||
                              snapshot.coverage.transportUnavailable != 0 ||
                              snapshot.coverage.partialExport != 0;
        const bool observed = snapshot.session.successfullyEnqueued != 0;
        const bool notObserved = snapshot.session.observationMode == Mode::Off ||
                                 (snapshot.session.attemptedPublications == 0 && !observed && !impaired);
        // A terminal snapshot with undrained/unacknowledged events is incomplete
        // even when earlier events reached a consumer successfully.
        const bool partial = impaired || !observed || snapshot.session.queuedEvents != 0 ||
                             snapshot.session.drainedEvents != snapshot.session.successfullyEnqueued ||
                             snapshot.session.exportedEvents != snapshot.session.drainedEvents;
        BeginSummaryPage(output, sequence, "M0.CoverageFacets", !notObserved && !partial,
                         snapshot.drops.saturated || snapshot.coverage.saturated);
        AddSymbol(output, "coverage.overall", notObserved ? "NotObserved" : partial ? "Partial" :
                                                                "CompleteForDeclaredFields");
        AddSymbol(output, "coverage.ownerFacts", !observed ? "Unknown" :
                  snapshot.coverage.ownerPublicationUnavailable == 0 ? "Complete" : "Partial");
        AddSymbol(output, "coverage.association", !observed ? "Unknown" :
                  snapshot.coverage.missingAssociation == 0 ? "Complete" : "Unknown");
        AddSymbol(output, "coverage.generations", !observed ? "Unknown" :
                  snapshot.coverage.missingGeneration == 0 ? "Complete" : "Unknown");
        AddSymbol(output, "coverage.contentFreshness", !observed ? "Unknown" :
                  snapshot.coverage.missingContentRevision == 0 ? "Complete" : "Unknown");
        AddSymbol(output, "coverage.transport", TransportFacet(snapshot));
        FinishSummaryPage(output, sequence);
    }

    static const char* ExportTermination(const CounterSnapshot& snapshot) noexcept
    {
        if (snapshot.session.observationMode == Mode::Off)
            return "NotObserved";
        if (snapshot.coverage.transportUnavailable != 0)
            return "Unavailable";
        if (snapshot.coverage.partialExport != 0)
            return "Partial";
        if (snapshot.session.exportedEvents != 0)
            return "Published";
        if (snapshot.session.drainedEvents != 0)
            return "DrainedNotExported";
        return "Unavailable";
    }

    static void BuildSessionPage(const CounterSnapshot& snapshot, std::uint64_t sequence,
                                 EventClosure& output) noexcept
    {
        BeginSummaryPage(output, sequence, "M0.SessionSummary", snapshot.session.accountingComplete,
                         snapshot.drops.saturated || snapshot.coverage.saturated);
        AddSymbol(output, "observationMode", snapshot.session.observationMode == Mode::Shadow ? "Shadow" : "Off");
        AddCounter(output, "queueCapacity", snapshot.session.queueCapacity);
        AddCounter(output, "attemptedPublications", snapshot.session.attemptedPublications);
        AddCounter(output, "successfullyEnqueued", snapshot.session.successfullyEnqueued);
        AddCounter(output, "drainedEvents", snapshot.session.drainedEvents);
        AddCounter(output, "exportedEvents", snapshot.session.exportedEvents);
        AddSymbol(output, "exportTermination", ExportTermination(snapshot));
        FinishSummaryPage(output, sequence);
    }
};

using GlobalObserver = Observer<>;
// The optional reserves fixed static storage but does not construct/zero the 64-slot queue in Off
// mode. FrameTrace::Current serializes ConfigureGlobal; state 3 is published only after emplace.
inline std::optional<GlobalObserver> globalObserver {};
inline std::atomic<std::uint8_t> globalConfiguration {0}; // 0 unset, 1 configuring, 2 Off, 3 Shadow
inline thread_local const SourceSnapshot* currentSource = nullptr;

inline bool ConfigureGlobal(Mode mode) noexcept
{
    std::uint8_t expected = 0;
    if (!globalConfiguration.compare_exchange_strong(expected, std::uint8_t {1},
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire))
        return false;
    if (mode == Mode::Shadow)
        globalObserver.emplace(Mode::Shadow);
    globalConfiguration.store(mode == Mode::Shadow ? std::uint8_t {3} : std::uint8_t {2},
                              std::memory_order_release);
    return true;
}
inline bool ConfigureGlobal(std::string_view text) noexcept { return ConfigureGlobal(ParseMode(text)); }
inline GlobalObserver* ActiveObserver() noexcept
{
    return globalConfiguration.load(std::memory_order_acquire) == 3 && globalObserver.has_value()
        ? &*globalObserver : nullptr;
}
inline bool Enabled() noexcept { return ActiveObserver() != nullptr; }

class PublisherContext
{
    SourceSnapshot snapshot_ {};
    const SourceSnapshot* previous_ = nullptr;
    bool active_ = false;

  public:
    PublisherContext() noexcept = default;
    explicit PublisherContext(SourceSnapshot snapshot) noexcept
        : snapshot_(std::move(snapshot)), previous_(currentSource), active_(true)
    {
        currentSource = &snapshot_;
    }
    PublisherContext(PublisherContext&& other) noexcept
        : snapshot_(std::move(other.snapshot_)), previous_(other.previous_), active_(other.active_)
    {
        if (active_ && currentSource == &other.snapshot_)
            currentSource = &snapshot_;
        other.active_ = false;
    }
    ~PublisherContext()
    {
        if (active_)
            currentSource = previous_;
    }
    PublisherContext(const PublisherContext&) = delete;
    PublisherContext& operator=(const PublisherContext&) = delete;
    PublisherContext& operator=(PublisherContext&&) = delete;
};

template<class Builder>
PublisherContext WithPublisher(Builder&& builder) noexcept
{
    if (!Enabled())
        return {};
    try
    {
        SourceSnapshot source = builder();
        if (Check(source) != SourceStatus::Valid)
        {
            if (auto* observer = ActiveObserver())
                observer->TryPublish("M0.SourceValidationRejected", &source);
            return {};
        }
        return PublisherContext(std::move(source));
    }
    catch (...)
    {
        SourceSnapshot missing = SourceSnapshot::Unavailable("M0.SourceBuilderException");
        if (auto* observer = ActiveObserver())
            observer->TryPublish("M0.SourceBuilderException", &missing);
        return {};
    }
}

inline PublishResult ObserveBoundary(std::string_view eventType, std::uint64_t nativeObservation,
                                     std::uint64_t presentObservation) noexcept
{
    if (auto* observer = ActiveObserver())
        return observer->TryPublish(eventType, currentSource, nativeObservation, presentObservation);
    return PublishResult::Disabled;
}

inline CounterSnapshot Snapshot() noexcept
{
    if (auto* observer = ActiveObserver())
        return observer->Snapshot();
    CounterSnapshot result;
    result.session.observationMode = Mode::Off;
    result.session.queueCapacity = GlobalObserver::QueueCapacity;
    result.coverage.observerDisabled = 1;
    return result;
}
inline bool TryPop(EventClosure& output) noexcept
{
    if (auto* observer = ActiveObserver())
        return observer->TryPop(output);
    return false;
}
inline bool DrainOne(const ExportSink& sink) noexcept
{
    if (auto* observer = ActiveObserver())
        return observer->DrainOne(sink);
    return false;
}
inline bool BuildSummary(SummaryBundle& output) noexcept
{
    if (auto* observer = ActiveObserver())
        return observer->BuildSummary(output);
    return false;
}

} // namespace Neurotic::Diagnostics::M0
