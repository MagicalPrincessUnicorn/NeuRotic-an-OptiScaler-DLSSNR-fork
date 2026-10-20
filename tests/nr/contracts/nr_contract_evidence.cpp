#include "ContractTestSupport.h"

using namespace Neurotic::Contracts;
using namespace Neurotic::Contracts::Test;

int main()
{
    EvidenceVector left;
    left.provenance = OptionalFact<SourceClass>::FromKnown(SourceClass::Native, Evidence(1));
    left.semanticCertainty = OptionalFact<SemanticCertainty>::FromKnown(SemanticCertainty::Claimed, Evidence(2));
    left.ageInRealFrames = OptionalFact<std::uint64_t>::FromKnown(2, Evidence(3));
    left.coverage = OptionalFact<CoverageKind>::FromKnown(CoverageKind::Partial, Evidence(4));
    left.access = OptionalFact<DescriptiveAccess>::FromKnown(DescriptiveAccess::CallbackBorrow, Evidence(5));
    NR_CHECK("A05-02", left.lineage.Push(Key(11, "native-provider")));
    RoundTrip("A05-02", left);
    auto right = left;
    right.ageInRealFrames = OptionalFact<std::uint64_t>::FromKnown(0, Evidence(6));
    right.access = OptionalFact<DescriptiveAccess>::FromUnknown(Missing());
    RoundTrip("A05-02", right);
    NR_CHECK("A05-02", right.provenance == left.provenance && right.access != left.access);
    SemanticClaim a;
    a.field = Sym("depth-direction");
    a.raw = OptionalFact<ScalarValue>::FromKnown(ScalarValue {true}, Evidence(11));
    auto b = a;
    b.raw = OptionalFact<ScalarValue>::FromKnown(ScalarValue {false}, Evidence(12));
    Contradiction conflict;
    conflict.field = a.field;
    NR_CHECK("A05-01", conflict.claims.Push(Key(11)) && conflict.claims.Push(Key(12)));
    conflict.reason.code = UnknownReason::ContradictoryEvidence;
    RoundTrip("A05-01", a);
    RoundTrip("A05-01", b);
    RoundTrip("A05-01", conflict);
    NR_CHECK("A05-01", a.raw.KnownPart()->value != b.raw.KnownPart()->value);
    // This preserves conflict, not a second NFC resolution algorithm.
    auto alias = left;
    NR_CHECK("A05-03", alias.lineage == left.lineage);
    RoundTrip("A05-03", alias);
    MetadataFixture receipts;
    OwnerReceipt first;
    first.header = Header(ContractId::C11, OwnerDomain::Strategy);
    first.operation = Sym("evaluation-outcome");
    receipts.Prepare(first);
    auto second = first;
    second.header.owner = OwnerDomain::Resource;
    second.header.record.issuer = Sym("resource-owner");
    RoundTrip("A05-04", first, receipts.catalog);
    RoundTrip("A05-04", second, receipts.catalog);
    NR_CHECK("A05-04", first.header.record != second.header.record);
    MetadataFixture mirrors;
    DiagnosticEvent mirror;
    mirror.header = Header(ContractId::C11, OwnerDomain::Diagnostics);
    mirror.eventType = Sym("receipt-mirror");
    mirrors.Prepare(mirror);
    RoundTrip("A05-05", mirror, mirrors.catalog);
    const auto encoded = Codec::EncodeWithMetadata(mirror, mirrors.catalog);
    NR_CHECK("A05-05", Codec::Decode<OwnerReceipt>(encoded.text).error == Error::WrongRecordType);
    GenerationVector generations;
    const GenerationToken token {Id(IdentityKind::ResourceGeneration, 5)};
    NR_CHECK("A05-06", generations.Insert(token) == Error::None);
    NR_CHECK("A05-06", generations.Insert(token) == Error::DuplicateEntry);
    NR_CHECK("A05-06", generations.Insert({Id(IdentityKind::ResourceGeneration, 6)}) == Error::ContradictoryEntry);
    NR_CHECK("A05-06", generations.Entries().Size() == 1);
    RoundTrip("A05-06", generations);
    // DIAG's exact counter names remain distinct pages of a common summary scope. These are
    // literal serialized snapshots only: no counter increment, saturation algorithm or transport.
    for (const bool dropPage : {true, false})
    {
        MetadataFixture pageMetadata;
        DiagnosticEvent page;
        page.header = Header(ContractId::C11, OwnerDomain::Diagnostics);
        page.header.scope.key = Key(500, "summary-fixture");
        page.header.record = Key(dropPage ? 501 : 502, "summary-fixture");
        page.eventType = Sym(dropPage ? "M0.DropCounters" : "M0.CoverageCounters");
        pageMetadata.Prepare(page);
        BoundedList<OwnerFact, 16> counters;
        const std::initializer_list<const char*> drops = {
            "DropCounters.queueFull", "DropCounters.producerContention", "DropCounters.validationRejected",
            "DropCounters.unsupportedEventEncoding", "DropCounters.consumerExportDropped",
            "DropCounters.consumerSerializationFailed", "DropCounters.transportUnavailableDrop"};
        const std::initializer_list<const char*> coverage = {
            "CoverageCounters.observedEvents", "CoverageCounters.ownerPublicationUnavailable",
            "CoverageCounters.unsupportedReceiptType", "CoverageCounters.missingAssociation",
            "CoverageCounters.missingGeneration", "CoverageCounters.missingContentRevision",
            "CoverageCounters.observerDisabled", "CoverageCounters.transportUnavailable", "CoverageCounters.partialExport"};
        for (const char* field : (dropPage ? drops : coverage))
        {
            OwnerFact counter;
            counter.field = Sym(field);
            counter.value = OptionalFact<ScalarValue>::FromKnown(ScalarValue {std::uint64_t {0}}, Evidence());
            NR_CHECK("DIAG-exact-counter-name", counters.Push(counter));
        }
        OwnerFact saturated;
        saturated.field = Sym(dropPage ? "DropCounters.saturated" : "CoverageCounters.saturated");
        saturated.value = OptionalFact<ScalarValue>::FromKnown(ScalarValue {false}, Evidence());
        NR_CHECK("DIAG-separate-saturation", counters.Push(saturated));
        page.envelopeFacts = pageMetadata.AddList(counters, OwnerDomain::Diagnostics);
        NR_CHECK("DIAG-page-within-fixed-capacity", page.envelopeFacts.count == (dropPage ? 8u : 10u));
        RoundTrip("DIAG-counter-page", page, pageMetadata.catalog);
        NR_CHECK("DIAG-missing-owner-not-transport-drop", page.drop == DiagnosticDrop::None && !page.ownerReceipt.IsKnown());
    }
    std::puts("DEFERRED A05-07: NFC conflict resolution requires CTX-001 production SUT; not executed here");
    std::puts("COMPLETE T05 representation cases; no semantic winner, route choice or owner authentication inferred");
    return 0;
}
