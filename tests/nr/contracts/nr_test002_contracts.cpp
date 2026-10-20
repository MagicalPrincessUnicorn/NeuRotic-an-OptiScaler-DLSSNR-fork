#include "Test002Support.h"
#include "Test002WireFixtures.h"
using namespace NrTest002;
int main(int argc, char** argv)
{
    return Guard([&] {
        Report r("canonical-contracts", argc == 2 ? std::string_view(argv[1]) : std::string_view{});
        C::MotionDescription motion;
        C::ExposureDescription exposure;
        r.Check("T04.jitter.unknown", !motion.currentJitter.IsKnown(), true, "MotionDescription");
        r.Check("T04.exposure.unknown", !exposure.preExposure.IsKnown(), true, "ExposureDescription");
        r.Check("T04.boolean.unknown", !C::OptionalFact<bool>{}.IsKnown(), true, "OptionalFact<bool>");
        const auto falseFact = C::Codec::Decode<C::OptionalFact<bool>>(Envelope("OptionalFact.Bool", Wire::KnownFalse));
        const auto trueFact = C::Codec::Decode<C::OptionalFact<bool>>(Envelope("OptionalFact.Bool", Wire::KnownTrue));
        const auto unknownBool = C::Codec::Decode<C::OptionalFact<bool>>(Envelope("OptionalFact.Bool", Wire::UnknownBoolean));
        r.Error("T04.boolean.false-decode", falseFact.error, C::Error::None, "Codec::Decode<OptionalFact<bool>>");
        r.Error("T04.boolean.true-decode", trueFact.error, C::Error::None, "Codec::Decode<OptionalFact<bool>>");
        r.Error("T04.boolean.unknown-decode", unknownBool.error, C::Error::None, "Codec::Decode<OptionalFact<bool>>");
        r.Check("T04.boolean.known-false", falseFact.record && falseFact.record->Value().IsKnown() &&
                !falseFact.record->Value().KnownPart()->value, true, "Codec::Decode<OptionalFact<bool>>");
        r.Check("T04.boolean.known-true", trueFact.record && trueFact.record->Value().IsKnown() &&
                trueFact.record->Value().KnownPart()->value, true, "Codec::Decode<OptionalFact<bool>>");
        r.Check("T04.boolean.false-not-unknown", falseFact.record && unknownBool.record &&
                falseFact.record->Value().IsKnown() && !unknownBool.record->Value().IsKnown(), true,
                "OptionalFact<bool> discriminant");
        C::Codec::EncodeResult falseEncoded;
        if (falseFact.record) falseEncoded = C::Codec::Encode(falseFact.record->Value());
        r.Error("T04.boolean.false-encode", falseEncoded.error, C::Error::None, "Codec::Encode<OptionalFact<bool>>");
        r.Check("T04.boolean.false-wire", falseEncoded.text.find("\"value\":false") != std::string::npos,
                true, "Codec::Encode<OptionalFact<bool>>");
        const auto falseRoundTrip = C::Codec::Decode<C::OptionalFact<bool>>(falseEncoded.text);
        r.Check("T04.boolean.false-roundtrip", falseRoundTrip.record && falseRoundTrip.record->Value().IsKnown() &&
                !falseRoundTrip.record->Value().KnownPart()->value, true, "Codec::Encode/Decode<OptionalFact<bool>>");
        motion.currentJitter = Known(C::Vec2{0, 0});
        exposure.preExposure = Known(1.0);
        const auto m = C::Codec::Encode(motion);
        const auto e = C::Codec::Encode(exposure);
        r.Error("T04.jitter.encode", m.error, C::Error::None, "Codec::Encode<MotionDescription>");
        r.Error("T04.exposure.encode", e.error, C::Error::None, "Codec::Encode<ExposureDescription>");
        const auto md = C::Codec::Decode<C::MotionDescription>(m.text);
        const auto ed = C::Codec::Decode<C::ExposureDescription>(e.text);
        r.Check("T04.jitter.known-zero", md.record && md.record->Value().currentJitter.IsKnown() &&
                md.record->Value().currentJitter.KnownPart()->value == C::Vec2{0, 0}, true,
                "Codec::Decode<MotionDescription>");
        r.Check("T04.exposure.known-neutral", ed.record && ed.record->Value().preExposure.IsKnown() &&
                ed.record->Value().preExposure.KnownPart()->value == 1, true, "Codec::Decode<ExposureDescription>");
        r.Check("T04.exposure.no-cross-default", ed.record && !ed.record->Value().rawExposure.IsKnown(), true,
                "Codec::Decode<ExposureDescription>");
        C::EvidenceVector evidence;
        evidence.provenance = Known(C::SourceClass::Synthetic);
        evidence.semanticCertainty = Known(C::SemanticCertainty::Claimed);
        evidence.ageInRealFrames = Known(std::uint64_t{4});
        evidence.providerScore = Known(0.99);
        evidence.scoreDefinition = Known(Sym("fixture-score-only"));
        evidence.aliasOf = Known(Key("same-source", 8));
        if (!evidence.lineage.Push(Key("same-source", 8))) throw std::runtime_error("Lineage capacity");
        auto ev = C::Codec::Encode(evidence);
        auto evd = C::Codec::Decode<C::EvidenceVector>(ev.text);
        r.Check("T05.alias.retained", evd.record && evd.record->Value().aliasOf.IsKnown() &&
                evd.record->Value().aliasOf.KnownPart()->value == Key("same-source", 8), true, "Codec::Decode<EvidenceVector>");
        r.Check("T05.score.not-freshness", evd.record && evd.record->Value().ageInRealFrames.IsKnown() &&
                evd.record->Value().ageInRealFrames.KnownPart()->value == 4, true, "Codec::Decode<EvidenceVector>");
        r.Check("T05.score.not-rights", evd.record && !evd.record->Value().access.IsKnown(), true,
                "Codec::Decode<EvidenceVector>");
        r.Check("T05.synthetic.not-native", evd.record && evd.record->Value().provenance.IsKnown() &&
                evd.record->Value().provenance.KnownPart()->value == C::SourceClass::Synthetic, true,
                "Codec::Decode<EvidenceVector>");
        C::Contradiction contradiction;
        contradiction.field = Sym("depth-direction");
        contradiction.reason.code = C::UnknownReason::ContradictoryEvidence;
        if (!contradiction.claims.Push(Key("source-a")) || !contradiction.claims.Push(Key("source-b")))
            throw std::runtime_error("Contradiction literal capacity");
        const auto conflict = C::Codec::Decode<C::Contradiction>(C::Codec::Encode(contradiction).text);
        r.Check("T05.conflict.two-claims", conflict.record && conflict.record->Value().claims.Size() == 2,
                true, "Codec::Decode<Contradiction>");
        C::Contradiction incomplete; incomplete.field = Sym("depth-direction");
        if (!incomplete.claims.Push(Key("source-a"))) throw std::runtime_error("Claim capacity");
        r.Error("T05.conflict.missing-second", incomplete.Check(), C::Error::MissingField, "Contradiction::Check");
        // Canonical root closure: these are explicitly synthetic owner-described values,
        // not an operationally qualified acquisition or frame context.
        Catalog aCatalog;
        C::AcquisitionCandidate a;
        a.header = Header(C::ContractId::C01, C::OwnerDomain::Acquisition);
        a.provider = Key("provider.native"); a.sourceSchema = Sym("test002.acquisition");
        a.semantic = C::SemanticKind::Motion;
        a.payload = Known(C::CandidateValue{C::ScalarValue{0.0}});
        a.frame = aCatalog.Add(C::FrameIdentity{}, C::OwnerDomain::IdentityRegistry);
        a.evidence = aCatalog.Add(evidence, C::OwnerDomain::Provider);
        a.masks = aCatalog.Add(C::MaskSet{}, C::OwnerDomain::Provider);
        const auto aText = C::Codec::EncodeWithMetadata(a, aCatalog.values);
        const auto aDecoded = C::Codec::Decode<C::AcquisitionCandidate>(aText.text);
        r.Error("T04.c01.closed-encode", aText.error, C::Error::None, "Codec::EncodeWithMetadata<AcquisitionCandidate>");
        r.Check("T04.c01.payload-with-unknown-jitter", aDecoded.record && aDecoded.record->Value().payload.IsKnown() &&
                !aDecoded.record->Value().jitter.IsKnown() && !aDecoded.record->Value().rrActive.IsKnown(), true,
                "Codec::Decode<AcquisitionCandidate>");
        a.header.owner = C::OwnerDomain::Diagnostics;
        r.Error("T05.c01.observer-not-acquirer", a.Check(), C::Error::WrongOwner, "AcquisitionCandidate::Check");
        Catalog cCatalog;
        C::CanonicalFrameContext c;
        c.header = Header(C::ContractId::C02, C::OwnerDomain::Context);
        c.frame = cCatalog.Add(C::FrameIdentity{}, C::OwnerDomain::IdentityRegistry);
        c.renderRaster = cCatalog.Add(C::RasterDescription{}, C::OwnerDomain::Context);
        c.outputRaster = cCatalog.Add(C::RasterDescription{}, C::OwnerDomain::Context);
        c.continuity = cCatalog.Add(C::EvidenceVector{}, C::OwnerDomain::Context);
        c.generations = cCatalog.Add(C::GenerationVector{}, C::OwnerDomain::Context);
        c.masks = cCatalog.Add(C::MaskSet{}, C::OwnerDomain::Context);
        const auto cText = C::Codec::EncodeWithMetadata(c, cCatalog.values);
        const auto cDecoded = C::Codec::Decode<C::CanonicalFrameContext>(cText.text);
        r.Error("T04.c02.closed-encode", cText.error, C::Error::None, "Codec::EncodeWithMetadata<CanonicalFrameContext>");
        r.Check("T04.c02.incomplete-stays-unknown", cDecoded.record && !cDecoded.record->Value().jitter.IsKnown() &&
                !cDecoded.record->Value().exposure.IsKnown(), true, "Codec::Decode<CanonicalFrameContext>");
        c.header.owner = C::OwnerDomain::Provider;
        r.Error("T05.c02.provider-not-context", c.Check(), C::Error::WrongOwner, "CanonicalFrameContext::Check");
        C::OwnerReceipt receipt;
        receipt.header = Header(C::ContractId::C11, C::OwnerDomain::Strategy);
        receipt.operation = Sym("evaluated");
        r.Error("T05.owner.operational", receipt.Check(), C::Error::None, "OwnerReceipt::Check");
        receipt.header.owner = C::OwnerDomain::Diagnostics;
        r.Error("T05.owner.mirror-not-owner", receipt.Check(), C::Error::WrongOwner, "OwnerReceipt::Check");
        C::DiagnosticEvent event;
        event.header = Header(C::ContractId::C11, C::OwnerDomain::Diagnostics);
        event.eventType = Sym("owner-mirror");
        r.Error("T05.observation.missing-owner-reference", event.Check(), C::Error::None, "DiagnosticEvent::Check");
        r.Check("T05.observation.no-fabricated-receipt", !event.ownerReceipt.IsKnown(), true, "DiagnosticEvent");
        // Repeat an immutable production comparison with and without an observer-owned value.
        // This proves only this pure call's values-only non-interference, not an M0 transport.
        const auto identity = Known(Id<C::ResourceIncarnation>("resource", 12).Describe());
        const auto before = L::CompareIdentity(identity, identity);
        event.drop = C::DiagnosticDrop::Disabled;
        const auto disabled = L::CompareIdentity(identity, identity);
        event.drop = C::DiagnosticDrop::QueueFull;
        const auto dropped = L::CompareIdentity(identity, identity);
        r.Status("T10.pure.disabled", disabled, L::IdentityStatus::Ok, "CompareIdentity");
        r.Status("T10.pure.dropped", dropped, L::IdentityStatus::Ok, "CompareIdentity");
        r.Check("T10.pure.result-preserved", before == disabled && before == dropped, true, "CompareIdentity");
        r.Pending("T05.binding.credible-conflict", "equal credible incompatible claims remain unresolved", "NR-CTX-001 qualification owner");
        r.Pending("T05.binding.alias-dedup", "one lineage contributes once; copy adds no credibility", "NR-CTX-001 qualification owner");
        r.Pending("T05.binding.unequal-credibility", "stronger eligible evidence wins only under frozen purpose rule", "NR-CTX-001 qualification owner");
        r.Pending("T10.binding.m0-transport", "disabled or dropped diagnostics do not alter owner execution", "NR-DIAG-001 transport and later runtime validation");
        return r.Finish();
    });
}
