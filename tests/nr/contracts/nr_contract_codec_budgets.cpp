#include "ContractTestSupport.h"
#include "ContractSpectreLiteralFixtures.h"
#include <cmath>
#include <memory>

using namespace Neurotic::Contracts;
using namespace Neurotic::Contracts::Test;

static std::string ArrayOf(std::size_t count)
{
    std::string text = "[";
    for (std::size_t i = 0; i < count; ++i) { if (i) text += ','; text += '0'; }
    return text + ']';
}
static std::string ObjectOf(std::size_t count)
{
    std::string text = "{";
    for (std::size_t i = 0; i < count; ++i)
    {
        if (i) text += ',';
        text += "\"k" + std::to_string(i) + "\":0";
    }
    return text + '}';
}
int main()
{
    using L = Codec::Limits;
    NR_CHECK("CP02-text-at", Codec::InspectJson("\"" + std::string(L::MaxText, 'a') + "\"") == Error::None);
    NR_CHECK("CP02-text-over", Codec::InspectJson("\"" + std::string(L::MaxText + 1, 'a') + "\"") == Error::LimitExceeded);
    NR_CHECK("CP02-array-at", Codec::InspectJson(ArrayOf(L::MaxArray)) == Error::None);
    NR_CHECK("CP02-array-over", Codec::InspectJson(ArrayOf(L::MaxArray + 1)) == Error::LimitExceeded);
    NR_CHECK("CP02-object-at", Codec::InspectJson(ObjectOf(L::MaxMembers)) == Error::None);
    NR_CHECK("CP02-object-over", Codec::InspectJson(ObjectOf(L::MaxMembers + 1)) == Error::LimitExceeded);
    const auto deep = std::string(L::MaxDepth, '[') + "0" + std::string(L::MaxDepth, ']');
    NR_CHECK("CP02-depth-at", Codec::InspectJson(deep) == Error::None);
    NR_CHECK("CP02-depth-over", Codec::InspectJson("[" + deep + "]") == Error::LimitExceeded);
    NR_CHECK("CP02-bytes-at", Codec::InspectJson("true" + std::string(L::MaxBytes - 4, ' ')) == Error::None);
    NR_CHECK("CP02-bytes-over", Codec::InspectJson("true" + std::string(L::MaxBytes - 3, ' ')) == Error::LimitExceeded);
    // Literal combinatorial node oracle: root + 63 * (array + 255 scalars) + (array + 254 scalars).
    std::string exactNodes = "[";
    for (unsigned i = 0; i < 63; ++i) exactNodes += ArrayOf(255) + ',';
    exactNodes += ArrayOf(254) + ']';
    NR_CHECK("CP02-nodes-at", Codec::InspectJson(exactNodes) == Error::None);
    auto oneMore = exactNodes; oneMore.insert(oneMore.size() - 2, ",0");
    NR_CHECK("CP02-nodes-over", Codec::InspectJson(oneMore) == Error::LimitExceeded);
    NR_CHECK("CP02-duplicate-decoded-key", Codec::InspectJson("{\"a\":0,\"\\u0061\":1}") == Error::DuplicateField);
    NR_CHECK("CP02-truncated-surrogate", Codec::InspectJson("\"\\uD800\\u") == Error::Malformed);
    NR_CHECK("CP02-invalid-surrogate-pair", Codec::InspectJson("\"\\uD800\\u0041\"") == Error::Malformed);
    for (double value : {-0.0, (std::numeric_limits<double>::denorm_min)(), (std::numeric_limits<double>::max)()})
    {
        auto encoded = Codec::Encode(value);
        NR_CHECK("CP02-finite-extremes", encoded.error == Error::None);
        auto decoded = Codec::Decode<double>(encoded.text);
        NR_CHECK("CP02-finite-extremes", decoded.error == Error::None && decoded.record->Value() == value);
        if (value == 0) NR_CHECK("CP02-negative-zero", std::signbit(decoded.record->Value()) == std::signbit(value));
    }
    // Exercise the actual construction allocator rather than relying on final output rejection.
    {
        Codec::Detail::Arena arena;
        bool rejected = false;
        try { (void)arena.Allocate(L::MaxArenaBytes + 1, 1); }
        catch (const Codec::Detail::Failure& failure) { rejected = failure.error == Error::LimitExceeded; }
        NR_CHECK("CP02-arena-before-allocation", rejected);
    }
    {
        Codec::Detail::ArenaScope arena;
        bool rejected = false;
        try { for (std::size_t i = 0; i <= L::MaxConstructionNodes; ++i) { Codec::Detail::Json value; (void)value; } }
        catch (const Codec::Detail::Failure& failure) { rejected = failure.error == Error::LimitExceeded; }
        NR_CHECK("CP02-construction-node-budget", rejected);
    }
    {
        Codec::Detail::DecodeContext context;
        bool rejected = false;
        try { Codec::Detail::ScratchScope oversized(context, L::MaxTypedObjectBytes + 1); }
        catch (const Codec::Detail::Failure& failure) { rejected = failure.error == Error::LimitExceeded; }
        NR_CHECK("CP02-typed-allocation-before-new", rejected && context.scratchBytes == 0);
    }
    auto literal = Literals::CanonicalFrameContext();
    auto root = Codec::Decode<CanonicalFrameContext>(literal);
    NR_CHECK("R01-closure-baseline", root.error == Error::None);
    Codec::MetadataCatalog duplicate;
    NR_CHECK("R01-closure-import", Codec::ImportMetadata(literal, duplicate) == Error::None);
    NR_CHECK("R01-closure-import-again", Codec::ImportMetadata(literal, duplicate) == Error::None);
    NR_CHECK("R01-duplicate-backing", Codec::EncodeWithMetadata(root.record->Value(), duplicate).error == Error::DuplicateEntry);
    Codec::MetadataCatalog absent;
    NR_CHECK("R01-no-silent-unresolved-reference", Codec::EncodeWithMetadata(root.record->Value(), absent).error == Error::MissingField);
    std::puts("COMPLETE construction/closure/boundary fixtures; fuzz and mutation execution remain separate evidence");
    return 0;
}
