#include "Test002Support.h"
#include "Test002WireFixtures.h"
using namespace NrTest002;
int main(int argc, char** argv)
{
    return Guard([&] {
        Report r("schema-hostile", argc == 2 ? std::string_view(argv[1]) : std::string_view{});
        const auto wire = Envelope("StableIdentity", Wire::ExactId);
        auto decoded = C::Codec::Decode<C::StableIdentity>(wire);
        r.Error("T06.id.decode", decoded.error, C::Error::None, "Codec::Decode<StableIdentity>");
        r.Check("T06.id.present", decoded.record.has_value(), true, "Codec::Decode<StableIdentity>");
        if (!decoded.record) return r.Finish();
        const auto& id = decoded.record->Value();
        r.Equal("T06.id.exact64", id.value, 9007199254740993ULL, "Codec::Decode<StableIdentity>");
        r.Check("T06.id.scope", id.nameSpace.View() == "wire.resource" && id.issuer.View() == "wire.owner",
                true, "Codec::Decode<StableIdentity>");
        const auto encoded = C::Codec::Encode(id);
        r.Error("T06.id.encode", encoded.error, C::Error::None, "Codec::Encode<StableIdentity>");
        r.Check("T06.id.decimal-wire", encoded.text.find("\"9007199254740993\"") != std::string::npos,
                true, "Codec::Encode<StableIdentity>");
        struct IdBoundary
        {
            const char* encodeId; const char* wireId; const char* roundTripId;
            std::uint64_t value; const char* decimal;
        };
        constexpr IdBoundary boundaries[] = {
            {"T06.id.boundary.zero.encode", "T06.id.boundary.zero.wire", "T06.id.boundary.zero.roundtrip", 0ULL, "0"},
            {"T06.id.boundary.one.encode", "T06.id.boundary.one.wire", "T06.id.boundary.one.roundtrip", 1ULL, "1"},
            {"T06.id.boundary.2p53m1.encode", "T06.id.boundary.2p53m1.wire", "T06.id.boundary.2p53m1.roundtrip", 9007199254740991ULL, "9007199254740991"},
            {"T06.id.boundary.2p53.encode", "T06.id.boundary.2p53.wire", "T06.id.boundary.2p53.roundtrip", 9007199254740992ULL, "9007199254740992"},
            {"T06.id.boundary.2p53p1.encode", "T06.id.boundary.2p53p1.wire", "T06.id.boundary.2p53p1.roundtrip", 9007199254740993ULL, "9007199254740993"},
            {"T06.id.boundary.maxm1.encode", "T06.id.boundary.maxm1.wire", "T06.id.boundary.maxm1.roundtrip", 18446744073709551614ULL, "18446744073709551614"},
            {"T06.id.boundary.max.encode", "T06.id.boundary.max.wire", "T06.id.boundary.max.roundtrip", 18446744073709551615ULL, "18446744073709551615"}
        };
        for (const auto& boundary : boundaries)
        {
            auto value = id; value.value = boundary.value;
            const auto valueEncoded = C::Codec::Encode(value);
            r.Error(boundary.encodeId, valueEncoded.error, C::Error::None, "Codec::Encode<StableIdentity>");
            const std::string expectedWire = "\"value\":\"" + std::string(boundary.decimal) + "\"";
            r.Check(boundary.wireId, valueEncoded.text.find(expectedWire) != std::string::npos, true,
                    "Codec::Encode<StableIdentity>");
            const auto valueDecoded = C::Codec::Decode<C::StableIdentity>(valueEncoded.text);
            r.Check(boundary.roundTripId, valueDecoded.record && valueDecoded.record->Value().value == boundary.value,
                    true, "Codec::Encode/Decode<StableIdentity>");
        }
        const auto zero = C::Codec::Decode<C::OptionalFact<std::uint64_t>>(Envelope("OptionalFact.UInt64", Wire::KnownZero));
        const auto missing = C::Codec::Decode<C::OptionalFact<std::uint64_t>>(Envelope("OptionalFact.UInt64", Wire::UnknownInteger));
        r.Check("T04.wire.zero", zero.record && zero.record->Value().IsKnown() &&
                zero.record->Value().KnownPart()->value == 0, true, "Codec::Decode<OptionalFact<uint64_t>>");
        r.Check("T04.wire.unknown", missing.record && !missing.record->Value().IsKnown(), true,
                "Codec::Decode<OptionalFact<uint64_t>>");
        std::uint64_t number = 77;
        r.Error("T06.decimal.maximum", C::ParseDecimal("18446744073709551615", number), C::Error::None, "ParseDecimal");
        r.Equal("T06.decimal.maximum-value", number, 18446744073709551615ULL, "ParseDecimal");
        struct BadDecimal { const char* id; const char* text; C::Error error; };
        constexpr BadDecimal bad[] = {
            {"T57.decimal.overflow", "18446744073709551616", C::Error::Overflow},
            {"T57.decimal.negative", "-1", C::Error::Malformed},
            {"T57.decimal.leading-zero", "01", C::Error::Malformed},
            {"T57.decimal.hex", "0x1234", C::Error::Malformed},
            {"T57.decimal.exponent", "1e3", C::Error::Malformed},
            {"T57.decimal.empty", "", C::Error::Malformed},
            {"T57.decimal.space", " 1", C::Error::Malformed}};
        for (const auto& c : bad)
        {
            number = 77; const auto error = C::ParseDecimal(c.text, number);
            r.Error(c.id, error, c.error, "ParseDecimal");
            if (number != 77) throw std::runtime_error("ParseDecimal refusal modified output; raw status already retained");
        }
        struct BadWire { const char* id; std::string wire; C::Error error; };
        const BadWire invalid[] = {
            {"T57.major", Envelope("StableIdentity", Wire::ExactId, "{}", 3), C::Error::UnsupportedVersion},
            {"T57.required-enum", ReplaceUnique(wire, "ResourceIncarnation", "FutureIncarnation"), C::Error::UnknownEnum},
            {"T57.native-handle", ReplaceUnique(wire, "\"value\":\"9007199254740993\"", "\"value\":\"9007199254740993\",\"handle\":\"1234\""), C::Error::UnknownField},
            {"T57.numeric-id", ReplaceUnique(wire, "\"9007199254740993\"", "9007199254740993"), C::Error::WrongValueType},
            {"T57.unknown-member", ReplaceUnique(wire, "\"record\":{", "\"record\":{\"newRequired\":true,"), C::Error::UnknownField},
            {"T57.missing-member", ReplaceUnique(wire, "\"issuer\":\"wire.owner\",", ""), C::Error::MissingField},
            {"T57.duplicate-member", ReplaceUnique(wire, "\"issuer\":\"wire.owner\"", "\"issuer\":\"wire.owner\",\"issuer\":\"other\""), C::Error::DuplicateField},
            {"T57.traversal", Envelope("StableIdentity", Wire::ExactId, R"({"x-note":"../escape"})"), C::Error::UnsafeExtension},
            {"T57.absolute-path", Envelope("StableIdentity", Wire::ExactId, R"({"x-note":"C:\\outside"})"), C::Error::UnsafeExtension},
            {"T57.device-path", Envelope("StableIdentity", Wire::ExactId, R"({"x-note":"\\\\.\\PhysicalDrive0"})"), C::Error::UnsafeExtension},
            {"T57.dll-field", Envelope("StableIdentity", Wire::ExactId, R"({"x-dll":"provider"})"), C::Error::UnsafeExtension},
            {"T57.executable-address", Envelope("StableIdentity", Wire::ExactId, R"({"x-note":"0xABCDEF"})"), C::Error::UnsafeExtension},
            {"T57.symbol-path", ReplaceUnique(wire, "wire.resource", "../resource"), C::Error::Malformed},
            {"T57.symbol-oversize", ReplaceUnique(wire, "wire.resource", std::string(97, 'a')), C::Error::LimitExceeded},
            {"T57.document-oversize", std::string(C::Codec::Limits::MaxBytes + 1, ' '), C::Error::LimitExceeded},
            {"T57.trailing-bytes", wire + "false", C::Error::Malformed}};
        for (const auto& c : invalid)
        {
            auto actual = C::Codec::Decode<C::StableIdentity>(c.wire);
            r.Error(c.id, actual.error, c.error, "Codec::Decode<StableIdentity>");
            if (actual.record && actual.error != C::Error::None)
                throw std::runtime_error("Refused wire produced a replay record");
        }
        const auto future = C::Codec::Decode<C::StableIdentity>(Envelope("StableIdentity", Wire::ExactId,
                                                    R"({"x-note":"optional descriptive future field"})", 2, 9));
        r.Check("T06.future-minor", future.record && future.record->Value().value == 9007199254740993ULL,
                true, "Codec::Decode<StableIdentity>");
        const std::string one = "[" + std::string(Wire::Gen) + "]";
        const auto gv = C::Codec::Decode<C::GenerationVector>(Envelope("GenerationVector", one));
        r.Check("T06.generation.valid", gv.record && gv.record->Value().Entries().Size() == 1, true,
                "Codec::Decode<GenerationVector>");
        r.Error("T57.generation.duplicate", C::Codec::Decode<C::GenerationVector>(Envelope("GenerationVector",
            "[" + std::string(Wire::Gen) + "," + std::string(Wire::Gen) + "]")).error,
            C::Error::DuplicateEntry, "Codec::Decode<GenerationVector>");
        r.Error("T57.generation.contradiction", C::Codec::Decode<C::GenerationVector>(Envelope("GenerationVector",
            "[" + std::string(Wire::Gen) + "," + std::string(Wire::GenDifferent) + "]")).error,
            C::Error::ContradictoryEntry, "Codec::Decode<GenerationVector>");
        r.Error("T57.generation.content-kind", C::Codec::Decode<C::GenerationVector>(Envelope("GenerationVector",
            ReplaceUnique(one, "ResourceGeneration", "ContentRevision"))).error, C::Error::UnknownEnum,
            "Codec::Decode<GenerationVector>");
        r.Error("T57.generation.frame-kind", C::Codec::Decode<C::GenerationVector>(Envelope("GenerationVector",
            ReplaceUnique(one, "ResourceGeneration", "PresentId"))).error, C::Error::WrongIdentityKind,
            "Codec::Decode<GenerationVector>");
        std::string tooMany = "[";
        for (unsigned i = 0; i != 65; ++i)
        {
            if (i) tooMany += ',';
            tooMany += ReplaceUnique(std::string(Wire::Gen), "wire.resource", "wire.capacity." + std::to_string(i));
        }
        tooMany += ']';
        r.Error("T57.generation.capacity", C::Codec::Decode<C::GenerationVector>(Envelope("GenerationVector", tooMany)).error,
                C::Error::LimitExceeded, "Codec::Decode<GenerationVector>");
        r.Error("T57.nan", C::Codec::Decode<C::Vec2>(Envelope("Vec2", R"({"x":NaN,"y":0})")).error,
                C::Error::Malformed, "Codec::Decode<Vec2>");
        r.Error("T57.inf", C::Codec::Decode<C::Vec2>(Envelope("Vec2", R"({"x":1e999,"y":0})")).error,
                C::Error::Overflow, "Codec::Decode<Vec2>");
        r.Error("T57.nonfinite-encode", C::Codec::Encode(C::Vec2{std::numeric_limits<double>::infinity(), 0}).error,
                C::Error::NonFinite, "Codec::Encode<Vec2>");
        std::string nested(34, '['); nested += '0'; nested += std::string(34, ']');
        r.Error("T57.nesting", C::Codec::InspectJson(nested), C::Error::LimitExceeded, "Codec::InspectJson");
        r.Error("T57.nul", C::Codec::InspectJson(R"("\u0000")"), C::Error::Malformed, "Codec::InspectJson");
        r.Error("T57.surrogate", C::Codec::InspectJson(R"("\ud800")"), C::Error::Malformed, "Codec::InspectJson");
        const std::string invalidUtf8 = "\"\xC0\xAF\"";
        r.Error("T57.utf8", C::Codec::InspectJson(invalidUtf8), C::Error::Malformed, "Codec::InspectJson");
        r.Error("T57.text-budget", C::Codec::InspectJson("\"" + std::string(4097, 'a') + "\""),
                C::Error::LimitExceeded, "Codec::InspectJson");
        C::AxisResult axis;
        r.Check("T06.c15.default-verdict-unknown", !axis.verdict.IsKnown(), true, "AxisResult");
        axis.verdict = Known(C::Verdict::PASS);
        r.Error("T06.c15.not-run-pass-refused", axis.Check(), C::Error::Malformed, "AxisResult::Check");
        axis.execution = C::ExecutionState::Completed;
        r.Error("T06.c15.completed-explicit-pass", axis.Check(), C::Error::None, "AxisResult::Check");
        axis.verdict = {}; axis.execution = C::ExecutionState::Attempted;
        r.Check("T06.c15.attempt-no-verdict", !axis.verdict.IsKnown(), true, "AxisResult");
        axis.execution = C::ExecutionState::Partial;
        r.Check("T06.c15.partial-no-verdict", !axis.verdict.IsKnown(), true, "AxisResult");
        C::ExecutionState execution = C::ExecutionState::NotRun;
        r.Check("T06.c15.uppercase-state", C::ParseEnum("BLOCKED_BY_ENVIRONMENT", execution), true, "ParseEnum<ExecutionState>");
        r.Check("T06.c15.retired-state", C::ParseEnum("FailedToLaunch", execution), false, "ParseEnum<ExecutionState>");
        r.Check("T06.c15.no-sixth-verdict", C::EnumTraits<C::Verdict>::Values.size() == 5, true, "EnumTraits<Verdict>");
        return r.Finish();
    });
}
