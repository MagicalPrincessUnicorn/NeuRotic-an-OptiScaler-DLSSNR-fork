#pragma once
// Literal wire inputs: authored independently of Codec::Encode. Not captured or executed evidence.
#include <string_view>
namespace NrTest002::Wire
{
inline constexpr std::string_view ExactId = R"json({"kind":"ResourceIncarnation","nameSpace":"wire.resource","issuer":"wire.owner","value":"9007199254740993"})json";
inline constexpr std::string_view Gen = R"json({"identity":{"kind":"ResourceGeneration","nameSpace":"wire.resource","issuer":"wire.owner","value":"31"}})json";
inline constexpr std::string_view GenDifferent = R"json({"identity":{"kind":"ResourceGeneration","nameSpace":"wire.resource","issuer":"wire.owner","value":"32"}})json";
inline constexpr std::string_view KnownZero = R"json({"known":true,"value":"0","evidence":{"record":{"nameSpace":"wire.evidence","issuer":"wire.owner","value":"7"}}})json";
inline constexpr std::string_view KnownFalse = R"json({"known":true,"value":false,"evidence":{"record":{"nameSpace":"wire.evidence","issuer":"wire.owner","value":"8"}}})json";
inline constexpr std::string_view KnownTrue = R"json({"known":true,"value":true,"evidence":{"record":{"nameSpace":"wire.evidence","issuer":"wire.owner","value":"9"}}})json";
inline constexpr std::string_view UnknownInteger = R"json({"known":false,"unknown":{"reason":{"version":1,"category":"Untested","code":"NotObserved","ownerCode":null},"scope":{"known":false},"detail":"missing, not zero"}})json";
inline constexpr std::string_view UnknownBoolean = R"json({"known":false,"unknown":{"reason":{"version":1,"category":"Untested","code":"NotObserved","ownerCode":null},"scope":{"known":false},"detail":"missing, not false"}})json";
}
