#pragma once
#include <cstdint>
#include <span>
namespace DlssNr::Connections {
enum class Source : uint32_t { Automatic, Native, BuiltIn, ReShade, External };
enum class Transport : uint32_t { Automatic, GPUOnly, CPU };
struct RequestedPolicy { Source source=Source::Automatic; Transport transport=Transport::Automatic; bool allowCpuFallback=true; };
struct RouteCapability {
 Source source=Source::Automatic;
 bool observed=false,creationReady=false,guideReady=false,gpuQualified=false,cpuReady=false;
 bool sameOutputSemantics=false,configured=false;
};
struct OwnerState {
 Source source=Source::Automatic; Transport transport=Transport::Automatic;
 bool active=false,submitted=false,cleanlyRetired=true,quarantined=false;
};
enum class SelectionReason : uint32_t { Ready, NoUsableSource, ExplicitUnavailable, OwnerRetirementRequired, UnsafeOwner, CpuFallback };
struct Selection { Source source=Source::Automatic; Transport transport=Transport::Automatic; SelectionReason reason=SelectionReason::NoUsableSource; bool usable=false,transitionPending=false; };
// Pure requests only. The session owner alone may commit after its retirement proof.
Selection Decide(const RequestedPolicy&,std::span<const RouteCapability>,const OwnerState&) noexcept;
constexpr Source MigrateSource(int explicitSource,bool hasExplicitSource,bool legacyNative,bool legacyPrepared) noexcept {
 if(hasExplicitSource)return explicitSource>=0&&explicitSource<=4?static_cast<Source>(explicitSource):Source::Automatic;
 return legacyNative?Source::BuiltIn:legacyPrepared?Source::ReShade:Source::Automatic;
}
const char* SourceName(Source) noexcept;
const char* TransportName(Transport) noexcept;
}
