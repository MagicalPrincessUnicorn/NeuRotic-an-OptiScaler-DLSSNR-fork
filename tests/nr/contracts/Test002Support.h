#pragma once
// NR-TEST-002 corrected candidate source; unapplied and NOT_RUN at preparation time.
// Test fixtures and evidence only. No operational identity, normalizer, or diagnostics owner.
#include <nr/contracts/FixtureCodec.h>
#include <nr/lifecycle/IdentityTransitions.h>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace NrTest002
{
namespace C = Neurotic::Contracts;
namespace L = Neurotic::Lifecycle;
inline C::Symbol Sym(std::string_view s)
{
    C::Symbol result;
    if (!result.Assign(s)) throw std::runtime_error("Invalid test literal Symbol");
    return result;
}
inline C::RecordKey Key(std::string_view ns, std::uint64_t value = 1,
                        std::string_view issuer = "test002.owner")
{
    return {Sym(ns), Sym(issuer), value};
}
inline C::EvidenceRef Ev(std::uint64_t value = 1) { return {Key("test002.evidence", value)}; }
template<class T> C::OptionalFact<T> Known(const T& value, std::uint64_t evidence = 1)
{
    return C::OptionalFact<T>::FromKnown(value, Ev(evidence));
}
template<class T> T Id(std::string_view ns, std::uint64_t value)
{
    return {Sym(ns), Sym("test002.owner"), value};
}
inline C::UnknownFact Unknown(C::UnknownReason code = C::UnknownReason::NotObserved)
{
    C::UnknownFact u; u.reason.code = code; u.scope.key = Key("test002.scope"); return u;
}
inline C::RecordHeader Header(C::ContractId contract, C::OwnerDomain owner, std::uint64_t value = 1)
{
    C::RecordHeader h; h.contract = contract; h.owner = owner;
    h.record = Key("test002.records", value); h.scope.key = Key("test002.scope"); return h;
}
inline C::GenerationToken Gen(C::IdentityKind kind, std::string_view ns, std::uint64_t value)
{
    return {{kind, Sym(ns), Sym("test002.owner"), value}};
}
// A neutral, fixture-local catalog. The production codec performs all serialization and validation.
// Reference sequence is a synthetic metadata address, never a C14 identity allocator.
class Catalog
{
    std::uint64_t next_ = 100;
  public:
    C::Codec::MetadataCatalog values;
    template<class T> C::MetadataRef<T> Add(const T& body, C::OwnerDomain owner)
    {
        C::MetadataRef<T> ref {owner, Key("test002.metadata", ++next_), 1, {}};
        if (values.Add(ref, body) != C::Error::None) throw std::runtime_error("Fixture metadata encoding failed");
        return ref;
    }
    template<class T, std::size_t N> C::MetadataList<T, N> List(const C::BoundedList<T, N>& body,
                                                               C::OwnerDomain owner)
    {
        C::MetadataList<T, N> result;
        result.count = static_cast<std::uint32_t>(body.Size());
        if (result.count != 0) result.backing = Add(body, owner);
        return result;
    }
};
inline void AddGap(C::AxisResult& axis, Catalog& catalog, std::string_view gap)
{
    C::BoundedList<C::Symbol, 16> gaps;
    if (!gaps.Push(Sym(gap))) throw std::runtime_error("Fixture gap capacity");
    axis.missingCoverage = catalog.List(gaps, C::OwnerDomain::Validation);
}
// JSON quoting is report framing only. It is not a fixture parser or a semantic oracle.
inline std::string Quote(std::string_view text)
{
    if (text.size() > 4096) throw std::runtime_error("Bounded report field exceeded");
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : text)
    {
        if (ch == '\\' || ch == '"') { out += '\\'; out += static_cast<char>(ch); }
        else if (ch < 32 || ch >= 127)
        {
            out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15];
        }
        else out += static_cast<char>(ch);
    }
    out += '"'; return out;
}
inline std::string Number(double x)
{
    std::ostringstream s; s << std::setprecision(std::numeric_limits<double>::max_digits10) << x;
    return s.str();
}
inline std::uint64_t Fnv1a64(std::string_view text) noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char ch : text) { hash ^= ch; hash *= 1099511628211ULL; }
    return hash;
}
enum class EvidenceClass { CurrentCallable, LaterOwnerBinding, FixturePrecondition };
inline std::string_view EvidenceClassName(EvidenceClass value)
{
    switch (value)
    {
    case EvidenceClass::CurrentCallable: return "CURRENT_CALLABLE";
    case EvidenceClass::LaterOwnerBinding: return "LATER_OWNER_BINDING";
    case EvidenceClass::FixturePrecondition: return "FIXTURE_PRECONDITION";
    }
    throw std::runtime_error("Unknown TEST evidence classification");
}
// All IDs below are test-case IDs, not operational IDs. No sixth Verdict is defined.
class Report
{
    std::string suite_;
    std::string runId_;
    std::string runNamespace_;
    std::string recordNamespace_;
    std::set<std::string> seen_;
    std::uint64_t ordinal_ = 0, semanticAssertions_ = 0, preconditions_ = 0, failures_ = 0, pending_ = 0;
    void Emit(std::string_view id, std::string_view expected, std::string_view actual,
              bool matched, bool ran, std::string_view sut, std::string_view oracle, EvidenceClass evidenceClass)
    {
        if (ordinal_ >= 4096 || !seen_.insert(std::string(id)).second)
            throw std::runtime_error("Duplicate ID or report capacity");
        ++ordinal_;
        const auto classification = EvidenceClassName(evidenceClass);
        if (!ran) ++pending_;
        else if (evidenceClass == EvidenceClass::FixturePrecondition) { ++preconditions_; if (!matched) ++failures_; }
        else { ++semanticAssertions_; if (!matched) ++failures_; }
        const auto expectedDigest = Fnv1a64(expected);
        const auto actualDigest = Fnv1a64(actual);
        // Print raw expected/actual BEFORE trying the C15 writer; codec/report failure cannot
        // erase the observation that identified a production mutant or assertion failure.
        std::cout << "{\"kind\":\"assertion\",\"suite\":" << Quote(suite_)
                  << ",\"run_id\":" << Quote(runId_) << ",\"run_namespace\":" << Quote(runNamespace_)
                  << ",\"record_namespace\":" << Quote(recordNamespace_) << ",\"ordinal\":" << Quote(std::to_string(ordinal_))
                  << ",\"id\":" << Quote(id) << ",\"fixture\":" << Quote(id)
                  << ",\"classification\":" << Quote(classification)
                  << ",\"sut\":" << Quote(sut) << ",\"oracle\":" << Quote(oracle)
                  << ",\"expected\":" << Quote(expected) << ",\"actual\":" << Quote(actual)
                  << ",\"expected_fnv1a64\":" << Quote(std::to_string(expectedDigest))
                  << ",\"actual_fnv1a64\":" << Quote(std::to_string(actualDigest))
                  << ",\"execution\":" << Quote(ran ? "COMPLETED" : "NOT_RUN")
                  << ",\"matched\":" << (ran ? (matched ? "true" : "false") : "null") << "}\n" << std::flush;
#ifdef NR_TEST002_MUTATION_RAW_ONLY
        // Under a production-code mutation the serializer may itself be the SUT. Preserve
        // independent raw assertion control flow; the unmutated evaluator later supplies C15.
        std::cout << "{\"kind\":\"c15-omitted\",\"suite\":" << Quote(suite_)
                  << ",\"run_id\":" << Quote(runId_) << ",\"run_namespace\":" << Quote(runNamespace_)
                  << ",\"record_namespace\":" << Quote(recordNamespace_) << ",\"ordinal\":" << Quote(std::to_string(ordinal_))
                  << ",\"id\":" << Quote(id) << ",\"fixture\":" << Quote(id)
                  << ",\"classification\":" << Quote(classification)
                  << ",\"expected_fnv1a64\":" << Quote(std::to_string(expectedDigest))
                  << ",\"actual_fnv1a64\":" << Quote(std::to_string(actualDigest))
                  << ",\"reason\":\"MUTATION_RAW_ONLY\"}\n" << std::flush;
        if (!std::cout) throw std::runtime_error("Mutation raw evidence unavailable");
        return;
#else
        Catalog catalog;
        C::ValidationFixture f;
        f.header = Header(C::ContractId::C15, C::OwnerDomain::Validation, ordinal_);
        f.header.record = Key(recordNamespace_, ordinal_);
        f.header.scope.key = Key(runNamespace_);
        f.origin = C::FixtureOrigin::Synthetic;
        f.provenance = Known(C::DocumentEvidence::BlueprintNormative);
        f.redistribution = C::RedistributionStatus::NotEstablished;
        f.requiredProfile = {Sym("TEST"), Sym("CPU.contracts.only"), 1};
        C::BoundedList<C::Symbol, 32> checks;
        if (!checks.Push(Sym(id))) throw std::runtime_error("Invariant capacity");
        f.expectedInvariants = catalog.List(checks, C::OwnerDomain::Validation);
        C::BoundedList<C::SemanticClaim, 32> env;
        const auto pushClaim = [&](std::string_view field, C::ScalarValue value) {
            C::SemanticClaim claim; claim.field = Sym(field);
            claim.raw = Known(value); claim.effective = Known(value);
            if (!env.Push(claim)) throw std::runtime_error("Environment capacity");
        };
        pushClaim("SyntheticNoGpuPayload", C::ScalarValue{true});
        pushClaim("RunIdentity", C::ScalarValue{Sym(runId_)});
        pushClaim("SuiteIdentity", C::ScalarValue{Sym(suite_)});
        pushClaim("FixtureIdentity", C::ScalarValue{Sym(id)});
        pushClaim("RawExpectedFnv1a64", C::ScalarValue{expectedDigest});
        pushClaim("RawActualFnv1a64", C::ScalarValue{actualDigest});
        pushClaim("EvidenceClassification", C::ScalarValue{Sym(classification)});
        f.environment = catalog.List(env, C::OwnerDomain::Validation);
        if (ran && evidenceClass == EvidenceClass::CurrentCallable)
        {
            f.result.protocolSemantic.execution = C::ExecutionState::Completed;
            f.result.protocolSemantic.verdict = C::OptionalFact<C::Verdict>::FromKnown(
                matched ? C::Verdict::PASS : C::Verdict::FAIL, C::EvidenceRef{f.header.record});
        }
        else if (evidenceClass == EvidenceClass::FixturePrecondition)
            AddGap(f.result.protocolSemantic, catalog, "FixturePreconditionOnly");
        else
            AddGap(f.result.protocolSemantic, catalog, "ProductionBindingUnavailable");
        AddGap(f.result.gpuResource, catalog, "GpuExecutionNotPerformed");
        AddGap(f.result.temporalImageQuality, catalog, "TemporalImagesNotObserved");
        AddGap(f.result.performanceLatency, catalog, "RuntimePerformanceNotMeasured");
        const auto encoded = C::Codec::EncodeWithMetadata(f, catalog.values);
        if (encoded.error != C::Error::None) throw std::runtime_error("C15 output encoding failed; raw assertion retained");
        std::cout << "{\"kind\":\"c15\",\"suite\":" << Quote(suite_)
                  << ",\"run_id\":" << Quote(runId_) << ",\"run_namespace\":" << Quote(runNamespace_)
                  << ",\"record_namespace\":" << Quote(recordNamespace_) << ",\"ordinal\":" << Quote(std::to_string(ordinal_))
                  << ",\"id\":" << Quote(id) << ",\"fixture\":" << Quote(id)
                  << ",\"classification\":" << Quote(classification)
                  << ",\"expected_fnv1a64\":" << Quote(std::to_string(expectedDigest))
                  << ",\"actual_fnv1a64\":" << Quote(std::to_string(actualDigest))
                  << ",\"record\":" << encoded.text << "}\n" << std::flush;
        if (!std::cout) throw std::runtime_error("Evidence stdout unavailable");
#endif
    }
  public:
    Report(std::string_view suite, std::string_view runId) : suite_(suite), runId_(runId)
    {
        if (runId.size() != 32 || runId.find_first_not_of("0123456789abcdef") != std::string_view::npos)
            throw std::runtime_error("One runner-owned 32-hex test-run ID argument is required");
        runNamespace_ = "test002." + runId_;
        recordNamespace_ = runNamespace_ + "." + suite_;
        (void)Sym(runNamespace_); (void)Sym(recordNamespace_); (void)Sym(suite_);
    }
    void Check(std::string_view id, bool actual, bool expected, std::string_view sut)
    {
        Emit(id, expected ? "true" : "false", actual ? "true" : "false", actual == expected, true, sut,
             "literal or declared relational invariant; not production-generated", EvidenceClass::CurrentCallable);
    }
    void Precondition(std::string_view id, bool actual, bool expected, std::string_view description)
    {
        Emit(id, expected ? "true" : "false", actual ? "true" : "false", actual == expected, true, description,
             "fixture setup precondition only; excluded from production semantic coverage", EvidenceClass::FixturePrecondition);
    }
    void Equal(std::string_view id, std::uint64_t actual, std::uint64_t expected, std::string_view sut)
    {
        Emit(id, std::to_string(expected), std::to_string(actual), actual == expected, true, sut,
             "literal integer", EvidenceClass::CurrentCallable);
    }
    void Error(std::string_view id, C::Error actual, C::Error expected, std::string_view sut)
    {
        Emit(id, std::to_string(static_cast<unsigned>(expected)), std::to_string(static_cast<unsigned>(actual)),
             actual == expected, true, sut, "literal canonical Error enum; version pinned by sources",
             EvidenceClass::CurrentCallable);
    }
    void Status(std::string_view id, L::IdentityStatus actual, L::IdentityStatus expected, std::string_view sut)
    {
        Emit(id, L::StatusName(expected), L::StatusName(actual), actual == expected, true, sut,
             "literal identity status", EvidenceClass::CurrentCallable);
    }
    void Near(std::string_view id, double actual, double expected, double tolerance, std::string_view sut)
    {
        Emit(id, Number(expected), Number(actual), std::isfinite(actual) && std::isfinite(expected) &&
             std::abs(actual - expected) <= tolerance, true, sut,
             "independent literal analytic value; stated absolute tolerance", EvidenceClass::CurrentCallable);
    }
    void Raw(std::string_view id, std::string_view actual, std::string_view expected, bool matched,
             std::string_view sut, std::string_view oracle)
    {
        Emit(id, expected, actual, matched, true, sut, oracle, EvidenceClass::CurrentCallable);
    }
    void Pending(std::string_view id, std::string_view expected, std::string_view owner)
    {
        Emit(id, expected, "no bound production entry point", false, false, owner,
             "literal future-owner expectation; NOT an executed comparison", EvidenceClass::LaterOwnerBinding);
    }
    int Finish() const
    {
        std::cout << "{\"kind\":\"suite-summary\",\"suite\":" << Quote(suite_)
                  << ",\"run_id\":" << Quote(runId_) << ",\"run_namespace\":" << Quote(runNamespace_)
                  << ",\"record_namespace\":" << Quote(recordNamespace_)
                  << ",\"ordinal_count\":" << Quote(std::to_string(ordinal_))
                  << ",\"semantic_assertions\":" << semanticAssertions_
                  << ",\"preconditions\":" << preconditions_ << ",\"mismatches\":" << failures_
                  << ",\"unbound\":" << pending_
                  << ",\"gate_approval\":\"NOT_PERFORMED\",\"exit_scope\":\"executed-subset-only\"}\n" << std::flush;
        return std::cout && failures_ == 0 ? 0 : 1;
    }
};
template<class F> int Guard(F action)
{
    try { return action(); }
    catch (const std::exception& e)
    {
        std::cerr << "INFRA_ERROR fixture/report failure: " << e.what() << '\n' << std::flush; return 2;
    }
    catch (...) { std::cerr << "INFRA_ERROR unexpected fixture exception\n" << std::flush; return 2; }
}
// Inline literal envelope construction is neutral framing, not a second decoder or normalizer.
inline std::string Envelope(std::string_view type, std::string_view record,
                            std::string_view extension = "{}", unsigned major = 2, unsigned minor = 0)
{
    return "{\"schema\":\"nr.arch.fixture\",\"major\":" + std::to_string(major) +
           ",\"minor\":" + std::to_string(minor) + ",\"type\":" + Quote(type) +
           ",\"record\":" + std::string(record) + ",\"optionalExtensions\":" +
           std::string(extension) + ",\"metadata\":[]}";
}
inline std::string ReplaceUnique(std::string value, std::string_view before, std::string_view after)
{
    const auto pos = value.find(before);
    if (pos == std::string::npos || value.find(before, pos + before.size()) != std::string::npos)
        throw std::runtime_error("Ambiguous fixture mutation anchor");
    value.replace(pos, before.size(), after); return value;
}
} // namespace NrTest002
