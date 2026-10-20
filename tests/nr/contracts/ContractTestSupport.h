#pragma once
// Candidate source only. No test execution is asserted by this file's presence.
#include <nr/contracts/FixtureCodec.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>

namespace Neurotic::Contracts::Test
{
inline void Check(bool condition, const char* caseId, const char* expression, int line)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL %s line=%d expected=%s actual=false\n", caseId, line, expression);
        std::exit(1);
    }
    std::printf("CHECK %s line=%d expected=%s actual=true\n", caseId, line, expression);
}
#define NR_CHECK(id, expression) ::Neurotic::Contracts::Test::Check(bool(expression), id, #expression, __LINE__)
inline Symbol Sym(std::string_view value)
{
    Symbol result;
    NR_CHECK("fixture-symbol", result.Assign(value));
    return result;
}
inline RecordKey Key(std::uint64_t value = 1, std::string_view issuer = "fixture-owner")
{
    return {Sym("fixture-session"), Sym(issuer), value};
}
inline EvidenceRef Evidence(std::uint64_t value = 1)
{
    return {Key(value)};
}
inline UnknownFact Missing(UnknownReason reason = UnknownReason::NotObserved)
{
    UnknownFact value;
    value.reason.code = reason;
    value.scope.key = Key();
    return value;
}
inline RecordHeader Header(ContractId id, OwnerDomain owner)
{
    RecordHeader result;
    result.contract = id;
    result.owner = owner;
    result.record = Key();
    result.scope.key = Key();
    return result;
}
inline StableIdentity Id(IdentityKind kind, std::uint64_t value = 1,
                         std::string_view space = "session-a", std::string_view issuer = "resource-owner")
{
    return {kind, Sym(space), Sym(issuer), value};
}
inline std::string ReplaceOnce(std::string text, std::string_view oldValue, std::string_view replacement)
{
    const auto at = text.find(oldValue);
    NR_CHECK("fixture-mutation-target", at != std::string::npos);
    text.replace(at, oldValue.size(), replacement);
    return text;
}
// Fixture-local reference numbers identify synthetic metadata publications only. This is
// not a production C14 registry, ownership allocator, semantic normalizer, or runtime store.
class MetadataFixture
{
    std::uint64_t sequence_ = 100;
  public:
    Codec::MetadataCatalog catalog;
    template<class T> MetadataRef<T> Add(const T& value, OwnerDomain owner)
    {
        MetadataRef<T> reference;
        reference.owner = owner;
        reference.record = Key(++sequence_, "fixture-metadata-author");
        reference.revision = 1;
        NR_CHECK("fixture-metadata-add", catalog.Add(reference, value) == Error::None);
        return reference;
    }
    template<class T, std::size_t N> MetadataList<T, N> AddList(const BoundedList<T, N>& value, OwnerDomain owner)
    {
        MetadataList<T, N> result;
        result.count = static_cast<std::uint32_t>(value.Size());
        if (result.count) result.backing = Add(value, owner);
        return result;
    }
    void Prepare(OwnerReceipt& receipt)
    {
        receipt.relevantGenerations = Add(GenerationVector {}, receipt.header.owner);
    }
    void Prepare(DiagnosticEvent& event)
    {
        event.relevantGenerations = Add(GenerationVector {}, OwnerDomain::Strategy);
        event.associations = Add(FrameIdentity {}, OwnerDomain::Strategy);
        event.coverage = Add(DiagnosticCoverage {}, OwnerDomain::Diagnostics);
    }
};
template<class T> void RoundTrip(const char* id, const T& value, const Codec::MetadataCatalog& metadata)
{
    const auto encoded = Codec::EncodeWithMetadata(value, metadata);
    NR_CHECK(id, encoded.error == Error::None);
    const auto decoded = Codec::Decode<T>(encoded.text);
    NR_CHECK(id, decoded.error == Error::None && decoded.record.has_value());
    NR_CHECK(id, decoded.record->Value() == value);
}
template<class T> void RoundTrip(const char* id, const T& value)
{
    const auto encoded = Codec::Encode(value);
    NR_CHECK(id, encoded.error == Error::None);
    const auto decoded = Codec::Decode<T>(encoded.text);
    NR_CHECK(id, decoded.error == Error::None);
    NR_CHECK(id, decoded.record.has_value());
    NR_CHECK(id, decoded.record->Value() == value);
}
}
