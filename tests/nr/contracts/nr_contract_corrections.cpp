#include "ContractTestSupport.h"
#include <cmath>
#include <limits>
using namespace Neurotic::Contracts;
using namespace Neurotic::Contracts::Test;

int main()
{
    Text<8> text;
    NR_CHECK("R03-initial", text.Assign("alpha"));
    NR_CHECK("R03-self", text.Assign(text.View()) && text.View() == "alpha");
    NR_CHECK("R03-overlap", text.Assign(text.View().substr(1, 3)) && text.View() == "lph");
    NR_CHECK("R03-reject-length", !text.Assign("123456789") && text.View() == "lph");
    NR_CHECK("R03-reject-nul", !text.Assign(std::string_view("a\0b", 3)) && text.View() == "lph");
    Symbol symbol = Sym("owner-alpha");
    NR_CHECK("R03-symbol-self", symbol.Assign(symbol.View()) && symbol.View() == "owner-alpha");
    NR_CHECK("R03-symbol-reject", !symbol.Assign("not a symbol") && symbol.View() == "owner-alpha");

    EvaluationResult output;
    output.header.owner = OwnerDomain::Strategy;
    NR_CHECK("R02-strategy", output.Check() == Error::None);
    output.header.owner = OwnerDomain::Multipass;
    NR_CHECK("R02-multipass", output.Check() == Error::None);
    IdentityRecord identity;
    identity.header.owner = OwnerDomain::Multipass;
    NR_CHECK("R02-evaluation-publication", identity.Check() == Error::None);
    for (const auto wrong : {OwnerDomain::Diagnostics, OwnerDomain::Configuration, OwnerDomain::Unspecified})
    {
        output.header.owner = wrong; identity.header.owner = wrong;
        NR_CHECK("R02-wrong-C07", output.Check() == Error::WrongOwner);
        NR_CHECK("R02-wrong-C14", identity.Check() == Error::WrongOwner);
    }
    FinalRealFramePacket finalPacket;
    finalPacket.header.owner = OwnerDomain::Multipass;
    NR_CHECK("R02-no-final-seal-authority", finalPacket.Check() == Error::WrongOwner);

    DiagnosticEvent event;
    event.header = Header(ContractId::C11, OwnerDomain::Diagnostics);
    event.eventType = Sym("reference-role");
    RecordReference receipt {ContractId::C11, Sym("OwnerReceipt"), Key(), 0};
    event.ownerReceipt = OptionalFact<RecordReference>::FromKnown(receipt, Evidence());
    NR_CHECK("R04-owner-receipt", event.Check() == Error::None);
    receipt.recordType = Sym("DiagnosticEvent");
    event.ownerReceipt = OptionalFact<RecordReference>::FromKnown(receipt, Evidence());
    NR_CHECK("R04-mirror-not-receipt", event.Check() == Error::WrongRecordType);
    receipt.recordType = Sym("OwnerReceipt"); receipt.contract = ContractId::C07;
    event.ownerReceipt = OptionalFact<RecordReference>::FromKnown(receipt, Evidence());
    NR_CHECK("R04-wrong-contract", event.Check() == Error::WrongRecordType);
    event.ownerReceipt = OptionalFact<RecordReference>::FromUnknown(Missing(UnknownReason::OwnerUnpublished));
    NR_CHECK("R04-unknown", event.Check() == Error::None);

    MetadataFixture eventMetadata;
    eventMetadata.Prepare(event);
    receipt.contract = ContractId::C11; receipt.recordType = Sym("OwnerReceipt");
    event.ownerReceipt = OptionalFact<RecordReference>::FromKnown(receipt, Evidence());
    const auto eventWire = Codec::EncodeWithMetadata(event, eventMetadata.catalog);
    NR_CHECK("R04-valid-wire", eventWire.error == Error::None);
    NR_CHECK("R04-role-mutation-wire", Codec::Decode<DiagnosticEvent>(ReplaceOnce(eventWire.text,
        "\"recordType\":\"OwnerReceipt\"", "\"recordType\":\"DiagnosticEvent\"")).error == Error::WrongRecordType);
    NR_CHECK("R04-contract-mutation-wire", Codec::Decode<DiagnosticEvent>(ReplaceOnce(eventWire.text,
        "\"ownerReceipt\":{\"known\":true,\"value\":{\"contract\":\"C11\"",
        "\"ownerReceipt\":{\"known\":true,\"value\":{\"contract\":\"C07\"")).error == Error::WrongRecordType);

    ResourceView view;
    view.descriptor.mipLevels = OptionalFact<std::uint32_t>::FromKnown(3, Evidence());
    view.descriptor.arrayLayers = OptionalFact<std::uint32_t>::FromKnown(2, Evidence());
    for (const auto mip : {0u, 2u})
    {
        view.mip = OptionalFact<std::uint32_t>::FromKnown(mip, Evidence());
        view.arrayLayer = OptionalFact<std::uint32_t>::FromKnown(1, Evidence());
        NR_CHECK("R05-valid", view.Check() == Error::None);
        RoundTrip("R05-valid-wire", view);
    }
    const auto validWire = Codec::Encode(view);
    view.mip = OptionalFact<std::uint32_t>::FromKnown(3, Evidence());
    NR_CHECK("R05-equal-count", Codec::Encode(view).error == Error::Malformed);
    view.mip = OptionalFact<std::uint32_t>::FromKnown((std::numeric_limits<std::uint32_t>::max)(), Evidence());
    NR_CHECK("R05-max-index", Codec::Encode(view).error == Error::Malformed);
    view.mip = OptionalFact<std::uint32_t>::FromUnknown(Missing());
    view.arrayLayer = OptionalFact<std::uint32_t>::FromKnown(2, Evidence());
    NR_CHECK("R05-layer-equal-count", Codec::Encode(view).error == Error::Malformed);
    view.descriptor.arrayLayers = OptionalFact<std::uint32_t>::FromUnknown(Missing());
    NR_CHECK("R05-count-unknown", Codec::Encode(view).error == Error::None);
    view.arrayLayer = OptionalFact<std::uint32_t>::FromUnknown(Missing());
    NR_CHECK("R05-both-unknown", Codec::Encode(view).error == Error::None);
    view.descriptor.mipLevels = OptionalFact<std::uint32_t>::FromKnown(0, Evidence());
    NR_CHECK("R05-zero-count", Codec::Encode(view).error == Error::Malformed);
    NR_CHECK("R05-decode-mutation", Codec::Decode<ResourceView>(ReplaceOnce(validWire.text,
        "\"mip\":{\"known\":true,\"value\":2", "\"mip\":{\"known\":true,\"value\":3")).error == Error::Malformed);

    MultipassNode node;
    node.settings.owner = OwnerDomain::Configuration;
    NR_CHECK("CP01-cfg-settings", node.Check() == Error::None);
    for (const auto wrong : {OwnerDomain::Multipass, OwnerDomain::Diagnostics, OwnerDomain::Unspecified})
    {
        node.settings.owner = wrong;
        NR_CHECK("CP01-not-C09-owner", node.Check() == Error::WrongOwner);
    }
    std::puts("COMPLETE correction-shape cases; source claims and runtime authority remain distinct");
    return 0;
}
