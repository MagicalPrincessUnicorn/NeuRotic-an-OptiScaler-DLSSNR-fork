#include "ConnectionSelection.h"
namespace DlssNr::Connections {
Selection Decide(const RequestedPolicy& p,std::span<const RouteCapability> routes,const OwnerState& owner) noexcept {
 if(owner.quarantined || (owner.submitted&&!owner.cleanlyRetired))
  return {owner.source,owner.transport,SelectionReason::UnsafeOwner,false,true};
 // Even a ready native source cannot steal a preparing/active episode.
 if(owner.active || !owner.cleanlyRetired)
  return {owner.source,owner.transport,SelectionReason::OwnerRetirementRequired,false,p.source!=owner.source};
 for(auto source:{Source::Native,Source::BuiltIn,Source::ReShade,Source::External}) {
  if(p.source!=Source::Automatic&&p.source!=source)continue;
  // Compare every qualified route for a source before considering CPU fallback.
  // Discovery/callback order is not a transport preference.
  for(auto transport:{Transport::GPUOnly,Transport::CPU}) {
   if(p.transport!=Transport::Automatic&&p.transport!=transport)continue;
   if(transport==Transport::CPU&&p.transport!=Transport::CPU&&!p.allowCpuFallback)continue;
   for(const auto& r:routes) {
   if(r.source!=source||!r.observed||!r.creationReady||!r.guideReady||!r.sameOutputSemantics)continue;
   if((source==Source::ReShade||source==Source::External)&&!r.configured)continue;
   if(transport==Transport::GPUOnly&&r.gpuQualified)return {source,Transport::GPUOnly,SelectionReason::Ready,true,false};
   if(transport==Transport::CPU&&r.cpuReady)
    return {source,Transport::CPU,p.transport==Transport::CPU?SelectionReason::Ready:SelectionReason::CpuFallback,true,false};
   }
  }
 }
 return {p.source,p.transport,p.source==Source::Automatic?SelectionReason::NoUsableSource:SelectionReason::ExplicitUnavailable,false,false};
}
const char* SourceName(Source s) noexcept {switch(s){case Source::Native:return "Native";case Source::BuiltIn:return "Built-in capture";case Source::ReShade:return "ReShade";case Source::External:return "External";default:return "Automatic";}}
const char* TransportName(Transport t) noexcept {switch(t){case Transport::GPUOnly:return "GPU";case Transport::CPU:return "CPU (readback cost)";default:return "Automatic";}}
}
