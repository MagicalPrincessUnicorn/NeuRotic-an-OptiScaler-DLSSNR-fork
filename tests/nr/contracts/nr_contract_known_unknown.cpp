#include "ContractTestSupport.h"
#include <optional>

using namespace Neurotic::Contracts;
using namespace Neurotic::Contracts::Test;

int main()
{
    const auto zero = OptionalFact<std::uint64_t>::FromKnown(0, Evidence());
    const OptionalFact<std::uint64_t> missing = OptionalFact<std::uint64_t>::FromUnknown(Missing());
    NR_CHECK("A04-01", zero.IsKnown() && !missing.IsKnown());
    NR_CHECK("A04-01", zero.KnownPart()->value == 0);
    NR_CHECK("A04-01", missing.UnknownPart()->reason.code == UnknownReason::NotObserved);
    RoundTrip("A04-01", zero);
    RoundTrip("A04-01", missing);
    RoundTrip("A04-02", OptionalFact<bool>::FromKnown(false, Evidence()));
    RoundTrip("A04-02", OptionalFact<bool>::FromUnknown(Missing()));
    RoundTrip("A04-03", OptionalFact<double>::FromKnown(1.0, Evidence()));
    RoundTrip("A04-03", OptionalFact<Vec2>::FromKnown({0.0, 0.0}, Evidence()));
    const auto revision = OptionalFact<ContentRevision>::FromKnown(ContentRevision {0}, Evidence());
    RoundTrip("A04-04", revision);
    ResourceIdentityToken resource;
    resource.contentRevision = revision;
    RoundTrip("A04-04", resource);
    const auto identity = OptionalFact<StableIdentity>::FromKnown(Id(IdentityKind::ResourceIncarnation, 0), Evidence());
    RoundTrip("A04-05", identity);
    RoundTrip("A04-05", OptionalFact<StableIdentity>::FromUnknown(Missing()));
    using Choice = OptionalFact<std::optional<RecordReference>>;
    const auto none = Choice::FromKnown(std::nullopt, Evidence());
    const auto unknown = Choice::FromUnknown(Missing());
    NR_CHECK("A04-06", none.IsKnown() && !none.KnownPart()->value.has_value() && !unknown.IsKnown());
    RoundTrip("A04-06", none);
    RoundTrip("A04-06", unknown);
    RoundTrip("A04-06", OptionalFact<Rectangle>::FromKnown({0, 0, 0, 0}, Evidence()));
    RasterDescription malformedRaster;
    malformedRaster.allocation = OptionalFact<Extent>::FromKnown({4, 4}, Evidence());
    malformedRaster.active = OptionalFact<Rectangle>::FromKnown({4, 0, 1, 1}, Evidence());
    NR_CHECK("A04-06", Codec::Encode(malformedRaster).error == Error::Malformed);
    MetadataFixture metadata;
    DiagnosticEvent event;
    event.header = Header(ContractId::C11, OwnerDomain::Diagnostics);
    event.eventType = Sym("mirrored-unknown");
    metadata.Prepare(event);
    event.ownerReceipt = OptionalFact<RecordReference>::FromUnknown(Missing(UnknownReason::OwnerUnpublished));
    MirroredOwnerFact fact;
    fact.owner = OwnerDomain::Strategy;
    fact.publisher = Key(4, "strategy-owner");
    fact.field = Sym("model-evaluated");
    fact.value = OptionalFact<ScalarValue>::FromUnknown(Missing(UnknownReason::OpaqueRuntimeState));
    BoundedList<MirroredOwnerFact, 16> facts;
    NR_CHECK("A04-07", facts.Push(fact));
    event.mirroredFacts = metadata.AddList(facts, OwnerDomain::Diagnostics);
    RoundTrip("A04-07", event, metadata.catalog);
    NR_CHECK("A04-07", !event.ownerReceipt.IsKnown() && !facts.Get(0)->value.IsKnown());
    std::puts("COMPLETE T04 candidate contract cases; no GPU, model, IQ or performance qualification");
    return 0;
}
