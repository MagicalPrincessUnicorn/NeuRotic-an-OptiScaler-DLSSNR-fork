#include "ContractTestSupport.h"
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

using namespace Neurotic::Contracts;
using namespace Neurotic::Contracts::Test;

static_assert(!std::is_convertible_v<DiagnosticEvent, OwnerReceipt>);
static_assert(!std::is_convertible_v<PreparedView, ConsumptionLease>);
static_assert(!std::is_convertible_v<Codec::ReplayRecord<ConsumptionLease>, ConsumptionLease>);
static_assert(!std::is_convertible_v<ContentRevision, GenerationToken>);
static_assert(!std::is_convertible_v<BaseRealFrameId, PresentId>);
static_assert(!std::is_convertible_v<TypedMask<MaskPurpose::UiExclusion>, TypedMask<MaskPurpose::NrApplication>>);
static_assert(std::is_same_v<RenderingProtocolInput, NrExecutionRecipe>);
static_assert(std::is_same_v<RenderingProtocolOutput, EvaluationResult>);
static_assert(ContractManifest.size() == 16);
static_assert(!std::is_convertible_v<OwnerValueReference, RecordReference>);
static_assert(!std::is_assignable_v<ImmutableRecord<RecordKey>&, ImmutableRecord<RecordKey>>);
static_assert(std::is_same_v<decltype(std::declval<const ImmutableRecord<RecordKey>&>().Value()), const RecordKey&>);
static_assert(!std::is_convertible_v<TypedMask<MaskPurpose::Disocclusion>, TypedMask<MaskPurpose::MotionValidity>>);

template<class T> void InstantiateCodecSurface()
{
    const auto encode = &Codec::Encode<T>;
    const auto decode = &Codec::Decode<T>;
    (void)encode;
    (void)decode;
    std::printf("TYPE %s bytes=%zu\n", Codec::Detail::TypeName<T>().c_str(), sizeof(T));
}

namespace
{
// Literal fixture inputs and expectations only; no gate policy or production normalizer.
constexpr std::string_view C15UnknownWire =
    R"c15({"known":false,"unknown":{"reason":{"version":1,"category":"Untested",)c15"
    R"c15("code":"NotObserved","ownerCode":null},"scope":{"known":false},"detail":""}})c15";

std::string C15KnownVerdictWire(std::string_view verdictName)
{
    return std::string(R"c15({"known":true,"value":")c15") + std::string(verdictName) +
        R"c15(","evidence":{"record":{"nameSpace":"fixture-session","issuer":"fixture-owner","value":"1"}}})c15";
}

std::string C15AxisWire(std::string_view executionJson, std::string_view verdictJson)
{
    return std::string(R"c15({"schema":"nr.arch.fixture","major":2,"minor":0,"type":"AxisResult",)c15") +
        R"c15("record":{"execution":)c15" + std::string(executionJson) +
        R"c15(,"verdict":)c15" + std::string(verdictJson) +
        R"c15(,"missingCoverage":{"count":0,"backing":null},"reason":)c15" +
        std::string(C15UnknownWire) + R"c15(},"optionalExtensions":{},"metadata":[]})c15";
}

void CheckC15Refusal(const char* caseId, const std::string& wire, Error expected)
{
    const auto saved = wire;
    const auto decoded = Codec::Decode<AxisResult>(wire);
    NR_CHECK(caseId, decoded.error == expected && !decoded.record.has_value());
    NR_CHECK(caseId, wire == saved);
}

void CheckC15ExecutionVocabulary()
{
    static_assert(!std::is_same_v<ExecutionState, Verdict>);
    static_assert(!std::is_convertible_v<ExecutionState, Verdict>);
    static_assert(!std::is_convertible_v<Verdict, ExecutionState>);
    static_assert(EnumTraits<ExecutionState>::Values.size() == 5);
    static_assert(EnumTraits<Verdict>::Values.size() == 5);

    struct StateLiteral
    {
        ExecutionState state;
        const char* name;
        const char* json;
    };
    constexpr std::array<StateLiteral, 5> states {{
        {ExecutionState::NotRun, "NOT_RUN", "\"NOT_RUN\""},
        {ExecutionState::Attempted, "ATTEMPTED", "\"ATTEMPTED\""},
        {ExecutionState::Partial, "PARTIAL", "\"PARTIAL\""},
        {ExecutionState::Completed, "COMPLETED", "\"COMPLETED\""},
        {ExecutionState::BlockedByEnvironment, "BLOCKED_BY_ENVIRONMENT", "\"BLOCKED_BY_ENVIRONMENT\""}
    }};
    for (const auto& test : states)
    {
        NR_CHECK("C15-STATE-SYMBOLS", EnumName(test.state) == test.name);
        ExecutionState parsed = ExecutionState::Completed;
        NR_CHECK("C15-STATE-SYMBOLS", ParseEnum(test.name, parsed) && parsed == test.state);

        AxisResult expected;
        expected.execution = test.state;
        NR_CHECK("C15-STATE-NO-VERDICT-INFERENCE", !expected.verdict.IsKnown());
        const auto encoded = Codec::Encode(expected);
        NR_CHECK("C15-STATE-ROUNDTRIP", encoded.error == Error::None);
        const auto field = std::string("\"execution\":") + test.json;
        NR_CHECK("C15-STATE-EXACT-WIRE", encoded.text.find(field) != std::string::npos);

        // Decode an independently authored wire value, not a production-generated expectation.
        const auto literal = C15AxisWire(test.json, C15UnknownWire);
        const auto decoded = Codec::Decode<AxisResult>(literal);
        NR_CHECK("C15-STATE-LITERAL-DECODE", decoded.error == Error::None && decoded.record.has_value());
        NR_CHECK("C15-STATE-LITERAL-DECODE", decoded.record->Value() == expected);
        NR_CHECK("C15-STATE-EXACT-WIRE", decoded.record->OriginalText() == literal);
        RoundTrip("C15-STATE-ROUNDTRIP", expected);

        Verdict notAState = Verdict::FAIL;
        NR_CHECK("C15-STATE-NOT-VERDICT", !ParseEnum(test.name, notAState) && notAState == Verdict::FAIL);
        CheckC15Refusal("C15-STATE-NOT-VERDICT",
            C15AxisWire("\"COMPLETED\"", C15KnownVerdictWire(test.name)), Error::UnknownEnum);
    }

    struct PairLiteral
    {
        ExecutionState state;
        const char* stateJson;
        Verdict verdict;
        const char* verdictName;
    };
    constexpr std::array<PairLiteral, 5> pairs {{
        {ExecutionState::NotRun, "\"NOT_RUN\"", Verdict::INCONCLUSIVE, "INCONCLUSIVE"},
        {ExecutionState::Attempted, "\"ATTEMPTED\"", Verdict::FAIL, "FAIL"},
        {ExecutionState::Partial, "\"PARTIAL\"", Verdict::INCONCLUSIVE, "INCONCLUSIVE"},
        {ExecutionState::BlockedByEnvironment, "\"BLOCKED_BY_ENVIRONMENT\"", Verdict::INFRA_ERROR, "INFRA_ERROR"},
        {ExecutionState::Completed, "\"COMPLETED\"", Verdict::PASS, "PASS"}
    }};
    for (const auto& test : pairs)
    {
        AxisResult expected;
        expected.execution = test.state;
        expected.verdict = OptionalFact<Verdict>::FromKnown(test.verdict, Evidence());
        const auto literal = C15AxisWire(test.stateJson, C15KnownVerdictWire(test.verdictName));
        const auto decoded = Codec::Decode<AxisResult>(literal);
        NR_CHECK("C15-EXECUTION-VERDICT-PAIRS", decoded.error == Error::None && decoded.record.has_value());
        NR_CHECK("C15-EXECUTION-VERDICT-PAIRS", decoded.record->Value() == expected);
        RoundTrip("C15-EXECUTION-VERDICT-PAIRS", expected);
    }

    // Completed does not choose PASS, and all five verdict symbols remain independently representable.
    struct VerdictLiteral { Verdict verdict; const char* name; };
    constexpr std::array<VerdictLiteral, 5> verdicts {{
        {Verdict::PASS, "PASS"}, {Verdict::FAIL, "FAIL"},
        {Verdict::SKIP_UNSUPPORTED, "SKIP_UNSUPPORTED"},
        {Verdict::INCONCLUSIVE, "INCONCLUSIVE"}, {Verdict::INFRA_ERROR, "INFRA_ERROR"}
    }};
    for (const auto& test : verdicts)
    {
        NR_CHECK("C15-VERDICT-VOCABULARY", EnumName(test.verdict) == test.name);
        AxisResult expected;
        expected.execution = ExecutionState::Completed;
        expected.verdict = OptionalFact<Verdict>::FromKnown(test.verdict, Evidence());
        const auto literal = C15AxisWire("\"COMPLETED\"", C15KnownVerdictWire(test.name));
        const auto decoded = Codec::Decode<AxisResult>(literal);
        NR_CHECK("C15-VERDICT-VOCABULARY", decoded.error == Error::None && decoded.record.has_value());
        NR_CHECK("C15-VERDICT-VOCABULARY", decoded.record->Value() == expected);
        RoundTrip("C15-VERDICT-VOCABULARY", expected);
        CheckC15Refusal("C15-VERDICT-NOT-STATE",
            C15AxisWire(std::string("\"") + test.name + "\"", C15UnknownWire), Error::UnknownEnum);
    }

    for (const auto& test : states)
    {
        if (test.state != ExecutionState::Completed)
        {
            // Same pre-existing local shape invariant, now exercised for every non-completed state.
            CheckC15Refusal("C15-NONCOMPLETED-PASS-DECODE",
                C15AxisWire(test.json, C15KnownVerdictWire("PASS")), Error::Malformed);
        }
    }
    for (const char* token : {"\"RUNNING\"", "\"\"", "\"NotRun\"", "\"Completed\"",
                              "\"EnvironmentUnavailable\"", "\"FailedToLaunch\""})
    {
        CheckC15Refusal("C15-UNKNOWN-OR-RETIRED-STATE", C15AxisWire(token, C15UnknownWire), Error::UnknownEnum);
    }
    for (const char* token : {"0", "null", "true", "[]", "{}"})
        CheckC15Refusal("C15-WRONG-STATE-TYPE", C15AxisWire(token, C15UnknownWire), Error::WrongValueType);
    const auto notRun = C15AxisWire("\"NOT_RUN\"", C15UnknownWire);
    CheckC15Refusal("C15-MISSING-STATE",
        ReplaceOnce(notRun, "\"execution\":\"NOT_RUN\",", ""), Error::MissingField);
    ExecutionState unchanged = ExecutionState::Partial;
    NR_CHECK("C15-UNKNOWN-STATE-NONDESTRUCTIVE", !ParseEnum("RUNNING", unchanged));
    NR_CHECK("C15-UNKNOWN-STATE-NONDESTRUCTIVE", unchanged == ExecutionState::Partial);
    AxisResult invalid;
    invalid.execution = static_cast<ExecutionState>(3); // Retired FailedToLaunch ordinal, not reused.
    NR_CHECK("C15-INVALID-STATE-ENCODE", Codec::Encode(invalid).error == Error::UnknownEnum);
    invalid.execution = static_cast<ExecutionState>((std::numeric_limits<std::uint32_t>::max)());
    NR_CHECK("C15-INVALID-STATE-ENCODE", Codec::Encode(invalid).error == Error::UnknownEnum);

    // Illustrative TEST-owned detail in the existing reason slot, not another status taxonomy.
    ReasonId launchReason;
    launchReason.category = ReasonCategory::OpaqueRuntimeFailure;
    launchReason.code = UnknownReason::OpaqueRuntimeState;
    launchReason.ownerCode = Sym("TEST.FailedToLaunch");
    AxisResult failedLaunch;
    failedLaunch.execution = ExecutionState::Attempted;
    failedLaunch.reason = OptionalFact<ReasonId>::FromKnown(launchReason, Evidence());
    NR_CHECK("C15-LAUNCH-REASON-INDEPENDENT", !failedLaunch.verdict.IsKnown());
    RoundTrip("C15-LAUNCH-REASON-INDEPENDENT", failedLaunch);
    failedLaunch.verdict = OptionalFact<Verdict>::FromKnown(Verdict::INFRA_ERROR, Evidence());
    const auto launchWire = Codec::Encode(failedLaunch);
    NR_CHECK("C15-LAUNCH-REASON-INDEPENDENT", launchWire.error == Error::None);
    NR_CHECK("C15-LAUNCH-REASON-INDEPENDENT",
        launchWire.text.find("\"execution\":\"ATTEMPTED\"") != std::string::npos);
    NR_CHECK("C15-LAUNCH-REASON-INDEPENDENT",
        launchWire.text.find("\"ownerCode\":\"TEST.FailedToLaunch\"") != std::string::npos);
    RoundTrip("C15-LAUNCH-REASON-INDEPENDENT", failedLaunch);
    CheckC15Refusal("C15-LAUNCH-REASON-NOT-VERDICT",
        C15AxisWire("\"ATTEMPTED\"", C15KnownVerdictWire("FailedToLaunch")), Error::UnknownEnum);

    FourAxisResult independent;
    independent.protocolSemantic.execution = ExecutionState::Completed;
    independent.protocolSemantic.verdict = OptionalFact<Verdict>::FromKnown(Verdict::PASS, Evidence());
    independent.gpuResource.execution = ExecutionState::Partial;
    independent.gpuResource.verdict = OptionalFact<Verdict>::FromKnown(Verdict::INCONCLUSIVE, Evidence());
    independent.temporalImageQuality.execution = ExecutionState::BlockedByEnvironment;
    // performanceLatency remains NotRun with Unknown verdict; no cross-axis propagation.
    NR_CHECK("C15-FOUR-AXES-INDEPENDENT", !independent.temporalImageQuality.verdict.IsKnown());
    NR_CHECK("C15-FOUR-AXES-INDEPENDENT", !independent.performanceLatency.verdict.IsKnown());
    RoundTrip("C15-FOUR-AXES-INDEPENDENT", independent);
}
} // namespace

int main()
{
    InstantiateCodecSurface<AcquisitionCandidate>();
    InstantiateCodecSurface<ObservationSet>();
    InstantiateCodecSurface<CanonicalFrameContext>();
    InstantiateCodecSurface<DependencyProof>();
    InstantiateCodecSurface<ConsumptionLease>();
    InstantiateCodecSurface<QualificationCertificate>();
    InstantiateCodecSurface<FrameCapabilitySet>();
    InstantiateCodecSurface<RoutingDecision>();
    InstantiateCodecSurface<PlanCommit>();
    InstantiateCodecSurface<NrExecutionRecipe>();
    InstantiateCodecSurface<EvaluationResult>();
    InstantiateCodecSurface<ResetPlan>();
    InstantiateCodecSurface<ResetAcknowledgment>();
    InstantiateCodecSurface<MultipassPlan>();
    InstantiateCodecSurface<MultipassResult>();
    InstantiateCodecSurface<CapabilityGapRequest>();
    InstantiateCodecSurface<ProviderOffer>();
    InstantiateCodecSurface<OwnerReceipt>();
    InstantiateCodecSurface<DiagnosticEvent>();
    InstantiateCodecSurface<RepresentationPlan>();
    InstantiateCodecSurface<PreparedView>();
    InstantiateCodecSurface<FinalRealFramePacket>();
    InstantiateCodecSurface<FgHandoffReceipt>();
    InstantiateCodecSurface<IdentityRecord>();
    InstantiateCodecSurface<ValidationFixture>();
    InstantiateCodecSurface<LocalExecutionPlan>();
    InstantiateCodecSurface<CostRecord>();
    struct Literal { const char* caseId; const char* text; std::uint64_t value; };
    constexpr std::array<Literal, 6> ids {{
        {"A06-ID-0", "0", 0},
        {"A06-ID-1", "1", 1},
        {"A06-ID-9007199254740991", "9007199254740991", UINT64_C(9007199254740991)},
        {"A06-ID-9007199254740992", "9007199254740992", UINT64_C(9007199254740992)},
        {"A06-ID-18446744073709551614", "18446744073709551614", UINT64_C(18446744073709551614)},
        {"A06-ID-18446744073709551615", "18446744073709551615", UINT64_C(18446744073709551615)}
    }};
    for (const auto& test : ids)
    {
        std::uint64_t value = 123;
        NR_CHECK(test.caseId, ParseDecimal(test.text, value) == Error::None && value == test.value);
        NR_CHECK(test.caseId, Decimal(value) == test.text);
        const auto encoded = Codec::Encode(value);
        NR_CHECK(test.caseId, encoded.error == Error::None);
        NR_CHECK(test.caseId, encoded.text.find(std::string("\"record\":\"") + test.text + "\"") != std::string::npos);
        RoundTrip(test.caseId, value);
    }
    struct MalformedLiteral { const char* caseId; const char* text; };
    constexpr std::array<MalformedLiteral, 8> malformed {{
        {"A06-PARSE-EMPTY", ""}, {"A06-PARSE-NEGATIVE", "-1"}, {"A06-PARSE-SIGN", "+1"},
        {"A06-PARSE-GARBAGE", "42x"}, {"A06-PARSE-OVERFLOW", "18446744073709551616"},
        {"A06-PARSE-LEADING-ZERO", "01"}, {"A06-PARSE-LEADING-SPACE", " 1"}, {"A06-PARSE-TRAILING-SPACE", "1 "}
    }};
    for (const auto& test : malformed)
    {
        std::uint64_t value = 123;
        NR_CHECK(test.caseId, ParseDecimal(test.text, value) != Error::None && value == 123);
    }
    for (auto kind : {IdentityKind::ObjectIncarnation, IdentityKind::ResourceGeneration,
                      IdentityKind::RepresentationGeneration})
        RoundTrip("A06-ENUM-01", Id(kind, 7));
    auto encodedId = Codec::Encode(Id(IdentityKind::ObjectIncarnation));
    NR_CHECK("A06-ENUM-03", Codec::Decode<StableIdentity>(ReplaceOnce(encodedId.text,
             "ObjectIncarnation", "FutureRequiredKind")).error == Error::UnknownEnum);
    GenerationVector vector;
    NR_CHECK("A06-ENUM-02", vector.Insert({Id(IdentityKind::ResourceGeneration)}) == Error::None);
    auto encodedVector = Codec::Encode(vector);
    NR_CHECK("A06-ENUM-02", Codec::Decode<GenerationVector>(ReplaceOnce(encodedVector.text,
             "ResourceGeneration", "ContentRevision")).error == Error::UnknownEnum);
    NR_CHECK("A06-IDENTITY-01", Id(IdentityKind::PresentId) != Id(IdentityKind::BaseRealFrameId));
    NR_CHECK("A06-IDENTITY-01", Id(IdentityKind::PresentId, 1, "a") != Id(IdentityKind::PresentId, 1, "b"));
    NR_CHECK("A06-IDENTITY-01", Id(IdentityKind::PresentId, 1, "a", "p") != Id(IdentityKind::PresentId, 1, "a", "q"));
    ResourceIdentityToken resource;
    resource.objectIncarnation = OptionalFact<StableIdentity>::FromKnown(Id(IdentityKind::ResourceGeneration), Evidence());
    NR_CHECK("A06-IDENTITY-02", Codec::Encode(resource).error == Error::WrongIdentityKind);
    const auto scalar = Codec::Encode(std::uint64_t {42});
    auto future = ReplaceOnce(scalar.text, "\"major\":2", "\"major\":3");
    const auto saved = future;
    NR_CHECK("A06-VERSION-01", Codec::Decode<std::uint64_t>(future).error == Error::UnsupportedVersion);
    NR_CHECK("A06-VERSION-01", future == saved);
    auto minor = ReplaceOnce(scalar.text, "\"minor\":0", "\"minor\":1");
    minor = ReplaceOnce(minor, "\"optionalExtensions\":{}", "\"optionalExtensions\":{\"x-note\":\"descriptive\"}");
    const auto parsedMinor = Codec::Decode<std::uint64_t>(minor);
    NR_CHECK("A06-VERSION-02", parsedMinor.error == Error::None && parsedMinor.record->Value() == 42);
    NR_CHECK("A06-VERSION-02", parsedMinor.record->OriginalText() == minor);
    NR_CHECK("A06-VERSION-02", Codec::Decode<std::uint64_t>(ReplaceOnce(minor, "x-note", "required-field")).error == Error::UnknownField);
    BoundedList<std::uint64_t, 2> list;
    NR_CHECK("A06-BOUNDS-01", list.Push(1) && list.Push(2) && !list.Push(3) && list.Size() == 2);
    auto listText = Codec::Encode(list).text;
    NR_CHECK("A06-BOUNDS-01", Codec::Decode<decltype(list)>(ReplaceOnce(listText,
             "[\"1\",\"2\"]", "[\"1\",\"2\",\"3\"]")).error == Error::LimitExceeded);
    Text<4> text;
    NR_CHECK("A06-BOUNDS-02", text.Assign("abcd") && !text.Assign("abcde") && text.View() == "abcd");
    auto badText = ReplaceOnce(Codec::Encode(text).text, "abcd", "abcde");
    NR_CHECK("A06-BOUNDS-02", Codec::Decode<Text<4>>(badText).error == Error::LimitExceeded);
    const std::string nested(Codec::Limits::MaxDepth + 1, '[');
    NR_CHECK("A06-BOUNDS-02", Codec::InspectJson(nested + "0" + std::string(Codec::Limits::MaxDepth + 1, ']')) == Error::LimitExceeded);
    NR_CHECK("A06-BOUNDS-02", Codec::InspectJson(std::string(Codec::Limits::MaxBytes + 1, ' ')) == Error::LimitExceeded);
    NR_CHECK("A06-WIRE-01", Codec::Decode<std::uint64_t>(ReplaceOnce(scalar.text,
             "\"record\":\"42\"", "\"record\":42")).error == Error::WrongValueType);
    NR_CHECK("A06-WIRE-01", Codec::Decode<std::uint64_t>(ReplaceOnce(scalar.text,
             "\"optionalExtensions\":{}", "\"nativePointer\":\"42\",\"optionalExtensions\":{}")).error == Error::UnknownField);
    NR_CHECK("A06-WIRE-01", Codec::Decode<std::uint64_t>(ReplaceOnce(scalar.text,
             "\"optionalExtensions\":{}", "\"optionalExtensions\":{\"x-handle\":\"0x1234\"}")).error == Error::UnsafeExtension);
    MetadataFixture receipts;
    OwnerReceipt receipt;
    receipt.header = Header(ContractId::C11, OwnerDomain::Strategy);
    receipt.operation = Sym("evaluation");
    receipts.Prepare(receipt);
    auto receiptText = Codec::EncodeWithMetadata(receipt, receipts.catalog).text;
    receipt.header.record.issuer = Symbol {};
    NR_CHECK("A06-WIRE-02", Codec::Encode(receipt).error == Error::MissingProvenance);
    NR_CHECK("A06-WIRE-02", Codec::Decode<OwnerReceipt>(ReplaceOnce(receiptText,
             "\"owner\":\"Strategy\"", "\"owner\":\"Diagnostics\"")).error == Error::WrongOwner);
    NR_CHECK("hostile-duplicate", Codec::InspectJson("{\"a\":1,\"a\":2}") == Error::DuplicateField);
    for (const char* bad : {"{", "[1,]", "{\"a\":01}", "true false", "\"\\uD800\"", "\"\\uDC00\""})
        NR_CHECK("hostile-json", Codec::InspectJson(bad) != Error::None);
    NR_CHECK("hostile-float", Codec::Encode(std::numeric_limits<double>::infinity()).error == Error::NonFinite);
    NR_CHECK("hostile-float", Codec::Encode(std::numeric_limits<double>::quiet_NaN()).error == Error::NonFinite);
    NR_CHECK("unicode", Codec::InspectJson("\"\\uD83D\\uDE00\"") == Error::None);
    RoundTrip("external-version-descriptive", OptionalFact<VersionNumber>::FromKnown(VersionNumber {9, 42}, Evidence()));
    NR_CHECK("canonical-major-still-strict", Codec::Encode(SchemaVersion {9, 42}).error == Error::UnsupportedVersion);
    TypedMask<MaskPurpose::MotionValidity> validity;
    validity.source = {ContractId::C01, Sym("AcquisitionCandidate"), Key(), 0};
    const auto validityText = Codec::Encode(validity);
    NR_CHECK("typed-mask-symbol", validityText.error == Error::None);
    NR_CHECK("typed-mask-mutation", Codec::Decode<decltype(validity)>(ReplaceOnce(validityText.text,
        "\"purpose\":\"MotionValidity\"", "\"purpose\":\"Disocclusion\"")).error == Error::WrongValueType);
    OwnerValueReference settings;
    settings.owner = OwnerDomain::Configuration;
    settings.recordType = Sym("SettingsSnapshot");
    settings.record = Key();
    settings.revision = 42;
    const auto settingsText = Codec::Encode(settings);
    NR_CHECK("cfg-reference", settingsText.error == Error::None);
    NR_CHECK("cfg-reference", settingsText.text.find("\"contract\"") == std::string::npos);
    RoundTrip("cfg-reference", settings);
    RoutingDecision decision;
    decision.header = Header(ContractId::C05, OwnerDomain::Configuration);
    NR_CHECK("cfg-not-router", decision.Check() == Error::WrongOwner);
    NrExecutionRecipe recipe;
    recipe.header = Header(ContractId::C06, OwnerDomain::RenderingProtocol);
    recipe.settingsSnapshot.owner = OwnerDomain::Diagnostics;
    NR_CHECK("cfg-settings-owner", recipe.Check() == Error::WrongOwner);
    for (const auto state : {ExecutionState::NotRun, ExecutionState::Attempted,
                             ExecutionState::Partial, ExecutionState::BlockedByEnvironment})
    {
        AxisResult axis;
        axis.execution = state;
        axis.verdict = OptionalFact<Verdict>::FromKnown(Verdict::PASS, Evidence());
        NR_CHECK("c15-no-unrun-pass", Codec::Encode(axis).error == Error::Malformed);
    }
    AxisResult completed;
    completed.execution = ExecutionState::Completed;
    completed.verdict = OptionalFact<Verdict>::FromKnown(Verdict::PASS, Evidence());
    RoundTrip("c15-separate-state", completed); // Tests the vocabulary, not a claim about this preparation.
    CheckC15ExecutionVocabulary(); // Candidate assertions only; execution is still NOT_RUN.
    auto reason = Missing().reason;
    reason.ownerCode = Sym("CTX.WrongIncarnationOrGeneration");
    RoundTrip("owner-reason-id", reason);
    for (const auto& entry : EnumTraits<IdentityKind>::Values)
        RoundTrip("all-frozen-kind-symbols", Id(entry.first));
    NR_CHECK("text-limit", Codec::InspectJson("\"" + std::string(Codec::Limits::MaxText + 1, 'a') + "\"") == Error::LimitExceeded);
    NR_CHECK("utf8-invalid", Codec::InspectJson("\"\xC0\xAF\"") == Error::Malformed);
    ImmutableRecord<RecordKey> frozen(Key(8));
    NR_CHECK("immutable-value", frozen.Value() == Key(8));
    std::puts("COMPLETE T06 candidate schema/type checks; independent header compilation is scheduled by the runner");
    return 0;
}
