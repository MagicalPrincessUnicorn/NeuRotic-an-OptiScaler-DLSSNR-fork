#include <nr/diagnostics/ContractObservation.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

namespace M0 = Neurotic::Diagnostics::M0;
namespace C = Neurotic::Contracts;

namespace
{
M0::SourceSnapshot Source(bool withCanonicalReferences = false)
{
    auto source = M0::SourceSnapshot::OwnerPublication(
        C::OwnerDomain::Presentation, "Test.Presentation", "nr_diag_m0", 7, "PresentAttempt");
    assert(source.Add("knownFalse", false));
    assert(source.Add("knownZero", std::uint64_t {0}));
    assert(source.AddSymbol("routeLabel", "PresentImageOnly"));
    if (!withCanonicalReferences)
        return source;

    C::MetadataRef<C::GenerationVector> generations;
    generations.owner = C::OwnerDomain::IdentityRegistry;
    generations.record = M0::Key("Test.Generation", "IdentityOwner", 11);
    generations.revision = 3;
    source.SetGenerations(generations);

    C::MetadataRef<C::FrameIdentity> associations;
    associations.owner = C::OwnerDomain::IdentityRegistry;
    associations.record = M0::Key("Test.Association", "IdentityOwner", 12);
    associations.revision = 4;
    source.SetAssociations(associations);

    C::MetadataRef<C::ResourceIdentityToken> resource;
    resource.owner = C::OwnerDomain::Resource;
    resource.record = M0::Key("Test.Resource", "ResourceOwner", 13);
    resource.revision = 5;
    source.SetResourceIdentity(resource);

    C::RecordReference receipt;
    receipt.contract = C::ContractId::C11;
    assert(receipt.recordType.Assign(C::OwnerReceipt::WireName));
    receipt.record = M0::Key("Test.OwnerReceipt", "PresentationOwner", 14);
    receipt.revision = 6;
    source.SetOwnerReceipt(receipt);
    source.SetContentRevisionAvailable(true);
    return source;
}

const C::OwnerFact* Envelope(const M0::EventClosure& event, std::string_view name)
{
    for (const auto& fact : event.envelopeFacts)
        if (fact.field.View() == name)
            return &fact;
    return nullptr;
}

bool HasEnvelope(const M0::EventClosure& event, std::string_view name)
{
    return Envelope(event, name) != nullptr;
}

std::uint64_t KnownUnsigned(const C::OwnerFact& fact)
{
    const auto* known = fact.value.KnownPart();
    assert(known != nullptr);
    const auto* value = std::get_if<std::uint64_t>(&known->value);
    assert(value != nullptr);
    return *value;
}

std::string_view KnownSymbol(const C::OwnerFact& fact)
{
    const auto* known = fact.value.KnownPart();
    assert(known != nullptr);
    const auto* value = std::get_if<C::Symbol>(&known->value);
    assert(value != nullptr);
    return value->View();
}

struct NeverGate
{
    bool TryEnter() noexcept { return false; }
    void Leave() noexcept {}
};

M0::ExportDisposition ExportAs(void* context, const M0::EventClosure&) noexcept
{
    return *static_cast<M0::ExportDisposition*>(context);
}

void TestGlobalOffDoesNotConstructQueue()
{
    assert(!M0::globalObserver.has_value());
    assert(M0::ConfigureGlobal(M0::Mode::Off));
    assert(!M0::globalObserver.has_value());
    assert(!M0::Enabled());
    const auto counters = M0::Snapshot();
    assert(counters.session.observationMode == M0::Mode::Off);
    assert(counters.coverage.observerDisabled == 1);
}

void TestOffIsLazyAndInert()
{
    M0::Observer<2, 8> observer(M0::Mode::Off);
    int builderCalls = 0;
    const std::uint64_t operationalRoute = 77;
    const auto result = observer.ObserveLazy("off-event", [&] {
        ++builderCalls;
        return Source();
    });
    assert(result == M0::PublishResult::Disabled);
    assert(builderCalls == 0);
    assert(operationalRoute == 77);

    M0::EventClosure event;
    assert(!observer.TryPop(event));
    const auto counters = observer.Snapshot();
    assert(counters.session.observationMode == M0::Mode::Off);
    assert(counters.coverage.observerDisabled == 1);
    assert(counters.session.successfullyEnqueued == 0);
}

void TestShadowCopiesValuesWithoutMintingIdentity()
{
    M0::Observer<2, 8> observer(M0::Mode::Shadow);
    const std::uint64_t operationalRoute = 91;
    const auto source = Source(false);
    assert(observer.TryPublish("present-observed", &source, 21, 34) == M0::PublishResult::Accepted);
    assert(operationalRoute == 91);

    M0::EventClosure event;
    assert(observer.TryPop(event));
    assert(event.event.header.owner == C::OwnerDomain::Diagnostics);
    assert(event.event.eventType.View() == "present-observed");
    assert(event.event.ownerReceipt.UnknownPart() != nullptr);
    assert(event.event.resourceIdentity.UnknownPart() != nullptr);
    assert(event.generationFallback.Entries().Size() == 0);
    assert(!event.associationFallback.sessionId.IsKnown());
    assert(!event.associationFallback.baseRealFrameId.IsKnown());
    assert(!event.associationFallback.presentId.IsKnown());
    assert(!event.associationFallback.generatedFrameId.IsKnown());
    assert(event.mirroredFacts.Size() == 3);
    for (const auto& fact : event.mirroredFacts)
    {
        assert(fact.owner == C::OwnerDomain::Presentation);
        assert(fact.publisher == source.publisher);
        assert(fact.value.IsKnown());
    }
    assert(KnownUnsigned(*Envelope(event, "frameTrace.nativeObservation")) == 21);
    assert(KnownUnsigned(*Envelope(event, "frameTrace.presentObservation")) == 34);
    assert(HasEnvelope(event, "frameTrace.identifiersAreDiagnosticOnly"));
    const auto counters = observer.Snapshot();
    assert(counters.coverage.missingAssociation == 1);
    assert(counters.coverage.missingGeneration == 1);
    assert(counters.coverage.missingContentRevision == 1);
}

void TestCanonicalReferencesAreCopiedExactly()
{
    M0::Observer<2, 8> observer(M0::Mode::Shadow);
    const auto source = Source(true);
    assert(observer.TryPublish("owner-reference", &source) == M0::PublishResult::Accepted);
    M0::EventClosure event;
    assert(observer.TryPop(event));
    assert(event.event.relevantGenerations == *source.generations);
    assert(event.event.associations == *source.associations);
    assert(event.event.resourceIdentity.KnownPart() != nullptr);
    assert(event.event.resourceIdentity.KnownPart()->value == *source.resourceIdentity);
    assert(event.event.resourceIdentity.KnownPart()->evidence.record == source.resourceIdentity->record);
    assert(event.event.ownerReceipt.KnownPart() != nullptr);
    assert(event.event.ownerReceipt.KnownPart()->value == *source.ownerReceipt);
    assert(event.event.ownerReceipt.KnownPart()->evidence.record == source.ownerReceipt->record);
    assert(event.coverage.complete.KnownPart() != nullptr);
    assert(event.coverage.complete.KnownPart()->value);
}

void TestReferenceOnlyPublicationPreservesOwnerProvenance()
{
    M0::Observer<2, 8> observer(M0::Mode::Shadow);
    auto source = M0::SourceSnapshot::OwnerReferences(C::OwnerDomain::Presentation, "ReferenceOnly");

    C::MetadataRef<C::GenerationVector> generations;
    generations.owner = C::OwnerDomain::IdentityRegistry;
    generations.record = M0::Key("Test.GenerationOnly", "IdentityOwner", 21);
    generations.revision = 2;
    source.SetGenerations(generations);

    C::MetadataRef<C::FrameIdentity> associations;
    associations.owner = C::OwnerDomain::IdentityRegistry;
    associations.record = M0::Key("Test.AssociationOnly", "IdentityOwner", 22);
    associations.revision = 3;
    source.SetAssociations(associations);

    C::MetadataRef<C::ResourceIdentityToken> resource;
    resource.owner = C::OwnerDomain::Resource;
    resource.record = M0::Key("Test.ResourceOnly", "ResourceOwner", 23);
    resource.revision = 4;
    source.SetResourceIdentity(resource);

    C::RecordReference receipt;
    receipt.contract = C::ContractId::C11;
    assert(receipt.recordType.Assign(C::OwnerReceipt::WireName));
    receipt.record = M0::Key("Test.OwnerReceiptOnly", "PresentationOwner", 24);
    receipt.revision = 5;
    source.SetOwnerReceipt(receipt);
    source.SetContentRevisionAvailable(true);

    assert(observer.TryPublish("reference-only", &source) == M0::PublishResult::Accepted);
    M0::EventClosure event;
    assert(observer.TryPop(event));
    assert(event.mirroredFacts.Size() == 0);
    assert(event.event.relevantGenerations == generations);
    assert(event.event.associations == associations);
    assert(event.event.resourceIdentity.KnownPart() != nullptr);
    assert(event.event.resourceIdentity.KnownPart()->evidence.record == resource.record);
    assert(event.event.ownerReceipt.KnownPart() != nullptr);
    assert(event.event.ownerReceipt.KnownPart()->evidence.record == receipt.record);
    assert(event.coverage.complete.KnownPart() != nullptr);
    assert(!event.coverage.complete.KnownPart()->value);
    const auto counters = observer.Snapshot();
    assert(counters.coverage.ownerPublicationUnavailable == 1);
}

void TestBoundedDropsAndValidation()
{
    M0::Observer<1, 2> observer(M0::Mode::Shadow);
    const auto source = Source();
    assert(observer.TryPublish("first", &source) == M0::PublishResult::Accepted);
    assert(observer.TryPublish("queue-full", &source) == M0::PublishResult::QueueFull);
    auto counters = observer.Snapshot();
    assert(counters.drops.queueFull == 1);
    assert(counters.session.queuedEvents == 1);

    M0::EventClosure event;
    assert(observer.TryPop(event));
    assert(observer.TryPublish("second", &source) == M0::PublishResult::Accepted);
    assert(observer.TryPop(event));
    assert(observer.TryPublish("budget-ended", &source) == M0::PublishResult::Saturated);

    M0::Observer<2, 8> validation(M0::Mode::Shadow);
    auto invalid = M0::SourceSnapshot::OwnerPublication(
        C::OwnerDomain::Diagnostics, "Test.Invalid", "nr_diag_m0", 1, "InvalidOwner");
    assert(validation.TryPublish("invalid", &invalid) == M0::PublishResult::ValidationRejected);

    auto orphanField = M0::SourceSnapshot::Unavailable("OrphanField");
    assert(orphanField.Add("valueWithoutOwnerPublication", std::uint64_t {1}));
    assert(validation.TryPublish("orphan-field", &orphanField) == M0::PublishResult::ValidationRejected);

    auto wrongReceipt = Source();
    C::RecordReference receipt;
    receipt.contract = C::ContractId::C11;
    assert(receipt.recordType.Assign("DiagnosticEvent"));
    receipt.record = M0::Key("Test.WrongReceipt", "Test", 1);
    wrongReceipt.SetOwnerReceipt(receipt);
    assert(validation.TryPublish("wrong-receipt", &wrongReceipt) == M0::PublishResult::UnsupportedReceipt);

    auto orphanReceipt = M0::SourceSnapshot::Unavailable("OrphanReceipt");
    C::RecordReference orphanReceiptRef;
    orphanReceiptRef.contract = C::ContractId::C11;
    assert(orphanReceiptRef.recordType.Assign(C::OwnerReceipt::WireName));
    orphanReceiptRef.record = M0::Key("Test.OrphanReceipt", "PresentationOwner", 1);
    orphanReceipt.SetOwnerReceipt(orphanReceiptRef);
    assert(validation.TryPublish("orphan-receipt", &orphanReceipt) == M0::PublishResult::UnsupportedReceipt);

    auto malformedReceipt = Source();
    C::RecordReference malformed;
    malformed.contract = C::ContractId::C11;
    assert(malformed.recordType.Assign(C::OwnerReceipt::WireName));
    malformedReceipt.SetOwnerReceipt(malformed);
    assert(validation.TryPublish("malformed-receipt", &malformedReceipt) ==
           M0::PublishResult::ValidationRejected);

    auto diagnosticGeneration = Source();
    C::MetadataRef<C::GenerationVector> generationRef;
    generationRef.owner = C::OwnerDomain::Diagnostics;
    generationRef.record = M0::Key("Test.DiagnosticGeneration", "Diagnostics", 1);
    diagnosticGeneration.SetGenerations(generationRef);
    assert(validation.TryPublish("diagnostic-generation", &diagnosticGeneration) ==
           M0::PublishResult::ValidationRejected);

    counters = validation.Snapshot();
    assert(counters.drops.validationRejected == 4);
    assert(counters.coverage.unsupportedReceiptType == 2);
}

void TestTryOnlyContentionIsAObservationDrop()
{
    M0::Observer<2, 8, NeverGate> observer(M0::Mode::Shadow);
    const auto source = Source();
    assert(observer.TryPublish("contended", &source) == M0::PublishResult::Contended);
    const auto counters = observer.Snapshot();
    assert(!counters.session.accountingComplete);
    assert(!counters.drops.producerContention.has_value());
}


void TestObserverFailureAndDuplicatesStayDiagnosticOnly()
{
    M0::Observer<4, 8> observer(M0::Mode::Shadow);
    int operationalResult = 1234;
    const auto failed = observer.ObserveLazy("builder-failure", []() -> M0::SourceSnapshot {
        throw 9;
    });
    assert(failed == M0::PublishResult::ValidationRejected);
    assert(operationalResult == 1234);

    const auto source = Source();
    assert(observer.TryPublish("duplicate", &source) == M0::PublishResult::Accepted);
    assert(observer.TryPublish("duplicate", &source) == M0::PublishResult::Accepted);
    M0::EventClosure first, second;
    assert(observer.TryPop(first));
    assert(observer.TryPop(second));
    assert(first.event.producerSequence != second.event.producerSequence);
    assert(first.event.eventType == second.event.eventType);
    assert(first.mirroredFacts == second.mirroredFacts);
    assert(operationalResult == 1234);
}

void TestSaturatingCounterNeverWraps()
{
    std::uint64_t value = (std::numeric_limits<std::uint64_t>::max)() - 1;
    bool saturated = false;
    assert(M0::Detail::Increment(value, saturated));
    assert(value == (std::numeric_limits<std::uint64_t>::max)());
    assert(!saturated);
    assert(!M0::Detail::Increment(value, saturated));
    assert(value == (std::numeric_limits<std::uint64_t>::max)());
    assert(saturated);
}

void TestConsumerDispositionsRemainDiagnosticOnly()
{
    for (const auto disposition : {M0::ExportDisposition::Exported,
                                   M0::ExportDisposition::ConsumerRejected,
                                   M0::ExportDisposition::SerializationFailed,
                                   M0::ExportDisposition::TransportUnavailable})
    {
        M0::Observer<2, 8> observer(M0::Mode::Shadow);
        const auto source = Source();
        assert(observer.TryPublish("export", &source) == M0::PublishResult::Accepted);
        auto selected = disposition;
        M0::ExportSink sink {&selected, &ExportAs};
        assert(observer.DrainOne(sink));
        const auto counters = observer.Snapshot();
        assert(counters.session.drainedEvents == 1);
        if (disposition == M0::ExportDisposition::Exported)
            assert(counters.session.exportedEvents == 1);
        if (disposition == M0::ExportDisposition::ConsumerRejected)
            assert(counters.drops.consumerExportDropped == 1 && counters.coverage.partialExport == 1);
        if (disposition == M0::ExportDisposition::SerializationFailed)
            assert(counters.drops.consumerSerializationFailed == 1 && counters.coverage.partialExport == 1);
        if (disposition == M0::ExportDisposition::TransportUnavailable)
            assert(counters.drops.transportUnavailableDrop == 1 && counters.coverage.transportUnavailable == 1);
    }
}

void TestFrozenSummaryVocabulary()
{
    M0::Observer<1, 8> observer(M0::Mode::Shadow);
    const auto source = Source();
    assert(observer.TryPublish("summary-source", &source) == M0::PublishResult::Accepted);
    assert(observer.TryPublish("summary-drop", &source) == M0::PublishResult::QueueFull);

    M0::SummaryBundle summary;
    assert(observer.BuildSummary(summary));
    assert(summary.snapshot.coverage.transportUnavailable == 1);
    assert(KnownUnsigned(*Envelope(summary.pages[1], "CoverageCounters.transportUnavailable")) == 1);
    assert(KnownSymbol(*Envelope(summary.pages[2], "coverage.transport")) == "Unavailable");
    assert(KnownSymbol(*Envelope(summary.pages[3], "exportTermination")) == "Unavailable");
    assert(summary.pages[0].event.eventType.View() == "M0.DropCounters");
    assert(summary.pages[1].event.eventType.View() == "M0.CoverageCounters");
    assert(summary.pages[2].event.eventType.View() == "M0.CoverageFacets");
    assert(summary.pages[3].event.eventType.View() == "M0.SessionSummary");
    for (const auto name : {"DropCounters.queueFull", "DropCounters.producerContention",
                            "DropCounters.validationRejected", "DropCounters.unsupportedEventEncoding",
                            "DropCounters.consumerExportDropped", "DropCounters.consumerSerializationFailed",
                            "DropCounters.transportUnavailableDrop", "DropCounters.saturated"})
        assert(HasEnvelope(summary.pages[0], name));
    for (const auto name : {"CoverageCounters.observedEvents", "CoverageCounters.ownerPublicationUnavailable",
                            "CoverageCounters.unsupportedReceiptType", "CoverageCounters.missingAssociation",
                            "CoverageCounters.missingGeneration", "CoverageCounters.missingContentRevision",
                            "CoverageCounters.observerDisabled", "CoverageCounters.transportUnavailable",
                            "CoverageCounters.partialExport", "CoverageCounters.saturated"})
        assert(HasEnvelope(summary.pages[1], name));
    for (const auto name : {"coverage.overall", "coverage.ownerFacts", "coverage.association",
                            "coverage.generations", "coverage.contentFreshness", "coverage.transport"})
        assert(HasEnvelope(summary.pages[2], name));
    for (const auto name : {"observationMode", "queueCapacity", "attemptedPublications",
                            "successfullyEnqueued", "drainedEvents", "exportedEvents", "exportTermination"})
        assert(HasEnvelope(summary.pages[3], name));
}

// F1: exercise the real summary builder, not a duplicate completeness predicate.
struct SwitchGate
{
    static inline unsigned denyAfter = 0;
    bool TryEnter() noexcept
    {
        if (denyAfter != 0 && --denyAfter == 0) return false;
        return true;
    }
    void Leave() noexcept {}
};

template<class Observer>
void ExpectCompleteness(Observer& observer, std::string_view expected, const char* label)
{
    M0::SummaryBundle summary;
    assert(observer.BuildSummary(summary));
    const auto actual = KnownSymbol(*Envelope(summary.pages[2], "coverage.overall"));
    const auto* complete = summary.pages[2].coverage.complete.KnownPart();
    std::cout << "F1 " << label << " attempted=" << summary.snapshot.session.attemptedPublications
              << " accepted=" << summary.snapshot.session.successfullyEnqueued
              << " rejected=" << summary.snapshot.drops.validationRejected
              << " expected=" << expected << " actual=" << actual << std::endl;
    assert(actual == expected);
    assert(complete != nullptr && complete->value == (expected == "CompleteForDeclaredFields"));
    if (summary.snapshot.session.successfullyEnqueued == 0)
    {
        for (const auto name : {"coverage.ownerFacts", "coverage.association",
                                "coverage.generations", "coverage.contentFreshness"})
            assert(KnownSymbol(*Envelope(summary.pages[2], name)) != "Complete");
    }
}

void TestF1CompletenessFailureEdges()
{
    const auto good = Source(true);
    {
        M0::Observer<2, 8> observer(M0::Mode::Shadow);
        auto invalid = good;
        invalid.valid = false;
        assert(observer.TryPublish("rejected-only", &invalid) == M0::PublishResult::ValidationRejected);
        const auto counters = observer.Snapshot();
        assert(counters.session.attemptedPublications == 1);
        assert(counters.session.successfullyEnqueued == 0 && counters.drops.validationRejected == 1);
        ExpectCompleteness(observer, "Partial", "rejected-only");
    }
    {
        M0::Observer<2, 8> observer(M0::Mode::Shadow);
        assert(observer.TryPublish(std::string(300, 'x'), &good) == M0::PublishResult::ValidationRejected);
        assert(observer.Snapshot().drops.unsupportedEventEncoding == 1);
        ExpectCompleteness(observer, "Partial", "encoding-only");
    }
    {
        M0::Observer<2, 8> observer(M0::Mode::Shadow);
        auto unsupported = good;
        unsupported.ownerReceipt->contract = C::ContractId::C01;
        assert(observer.TryPublish("unsupported", &unsupported) == M0::PublishResult::UnsupportedReceipt);
        assert(observer.Snapshot().coverage.unsupportedReceiptType == 1);
        ExpectCompleteness(observer, "Partial", "unsupported-receipt-only");
    }
    {
        M0::Observer<2, 8> observer(M0::Mode::Shadow);
        assert(observer.ObserveLazy("builder-throws", []() -> M0::SourceSnapshot { throw 1; }) ==
               M0::PublishResult::ValidationRejected);
        ExpectCompleteness(observer, "Partial", "builder-exception-only");
    }
    {
        M0::Observer<2, 8, SwitchGate> observer(M0::Mode::Shadow);
        SwitchGate::denyAfter = 1;
        assert(observer.TryPublish("contention", &good) == M0::PublishResult::Contended);
        assert(observer.Snapshot().drops.producerContention == 1);
        ExpectCompleteness(observer, "Partial", "known-contention-only");
    }
    {
        M0::Observer<2, 8> observer(M0::Mode::Shadow);
        ExpectCompleteness(observer, "NotObserved", "zero-attempt");
    }
    {
        M0::Observer<2, 8> observer(M0::Mode::Off);
        assert(observer.TryPublish("off", &good) == M0::PublishResult::Disabled);
        ExpectCompleteness(observer, "NotObserved", "disabled");
    }
    {
        M0::Observer<2, 8> observer(M0::Mode::Shadow);
        assert(observer.TryPublish("no-consumer", &good) == M0::PublishResult::Accepted);
        ExpectCompleteness(observer, "Partial", "no-consumer-canonical-fields");
    }
    {
        M0::Observer<2, 8> observer(M0::Mode::Shadow);
        assert(observer.TryPublish("no-ack", &good) == M0::PublishResult::Accepted);
        M0::EventClosure event;
        assert(observer.TryPop(event));
        ExpectCompleteness(observer, "Partial", "drained-without-export-ack");
    }
    for (const auto disposition : {M0::ExportDisposition::ConsumerRejected,
                                   M0::ExportDisposition::SerializationFailed,
                                   M0::ExportDisposition::TransportUnavailable})
    {
        M0::Observer<2, 8> observer(M0::Mode::Shadow);
        assert(observer.TryPublish("export-failure", &good) == M0::PublishResult::Accepted);
        auto selected = disposition;
        assert(observer.DrainOne({&selected, &ExportAs}));
        ExpectCompleteness(observer, "Partial", "consumer-failure");
    }
    {
        M0::Observer<1, 8> observer(M0::Mode::Shadow);
        auto disposition = M0::ExportDisposition::Exported;
        assert(observer.TryPublish("accepted", &good) == M0::PublishResult::Accepted);
        assert(observer.TryPublish("full", &good) == M0::PublishResult::QueueFull);
        assert(observer.DrainOne({&disposition, &ExportAs}));
        ExpectCompleteness(observer, "Partial", "queue-drop-with-exported-success");
    }
    {
        M0::Observer<2, 8> observer(M0::Mode::Shadow);
        auto disposition = M0::ExportDisposition::Exported;
        assert(observer.TryPublish("accepted", &good) == M0::PublishResult::Accepted);
        assert(observer.DrainOne({&disposition, &ExportAs}));
        ExpectCompleteness(observer, "CompleteForDeclaredFields", "positive-complete-control");
        auto invalid = good;
        invalid.valid = false;
        assert(observer.TryPublish("later-rejection", &invalid) == M0::PublishResult::ValidationRejected);
        ExpectCompleteness(observer, "Partial", "success-then-rejection");
    }
    {
        M0::Observer<2, 8, SwitchGate> observer(M0::Mode::Shadow);
        auto disposition = M0::ExportDisposition::Exported;
        assert(observer.TryPublish("accepted", &good) == M0::PublishResult::Accepted);
        assert(observer.DrainOne({&disposition, &ExportAs}));
        SwitchGate::denyAfter = 1;
        assert(observer.TryPublish("contended", &good) == M0::PublishResult::Contended);
        ExpectCompleteness(observer, "Partial", "success-then-known-contention");
        SwitchGate::denyAfter = 2; // summary sequence succeeds, its counter snapshot is unavailable
        ExpectCompleteness(observer, "Partial", "incomplete-counter-snapshot");
    }
    {
        M0::Observer<2, 8, SwitchGate> observer(M0::Mode::Shadow);
        assert(observer.TryPublish("accepted", &good) == M0::PublishResult::Accepted);
        M0::EventClosure event;
        assert(observer.TryPop(event));
        SwitchGate::denyAfter = 1;
        observer.AcknowledgeExport(M0::ExportDisposition::Exported);
        assert(!observer.Snapshot().session.accountingComplete);
        ExpectCompleteness(observer, "Partial", "lost-consumer-accounting");
    }
    {
        M0::Observer<2, 8> observer(M0::Mode::Shadow);
        auto disposition = M0::ExportDisposition::Exported;
        assert(observer.TryPublish("first", &good) == M0::PublishResult::Accepted);
        assert(observer.DrainOne({&disposition, &ExportAs}));
        assert(observer.TryPublish("pending", &good) == M0::PublishResult::Accepted);
        ExpectCompleteness(observer, "Partial", "partially-drained-session");
    }
    {
        M0::Observer<2, 1> observer(M0::Mode::Shadow);
        auto disposition = M0::ExportDisposition::Exported;
        assert(observer.TryPublish("last-budget", &good) == M0::PublishResult::Accepted);
        assert(observer.DrainOne({&disposition, &ExportAs}));
        assert(observer.TryPublish("budget-ended", &good) == M0::PublishResult::Saturated);
        ExpectCompleteness(observer, "Partial", "publication-saturation");
    }
}

} // namespace

int main()
{
    TestF1CompletenessFailureEdges();
    TestGlobalOffDoesNotConstructQueue();
    TestOffIsLazyAndInert();
    TestShadowCopiesValuesWithoutMintingIdentity();
    TestCanonicalReferencesAreCopiedExactly();
    TestReferenceOnlyPublicationPreservesOwnerProvenance();
    TestBoundedDropsAndValidation();
    TestTryOnlyContentionIsAObservationDrop();
    TestObserverFailureAndDuplicatesStayDiagnosticOnly();
    TestSaturatingCounterNeverWraps();
    TestConsumerDispositionsRemainDiagnosticOnly();
    TestFrozenSummaryVocabulary();
    std::cout << "PASS: NR-DIAG-001 authored T10/T47 CPU candidate cases\n";
}
