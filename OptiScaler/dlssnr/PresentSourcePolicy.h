#pragma once
#include "connections/ConnectionPolicy.h"
namespace DlssNr::PresentSources {
inline bool AcceptCompletion(const Connections::SignalStatusWireV2& receipt,uint64_t revision,
 uint64_t recordingGeneration,uint64_t currentGeneration,uint64_t lastCapture) noexcept {
 return receipt.capture&&receipt.revision==revision&&recordingGeneration==currentGeneration&&receipt.capture>=lastCapture;
}
// Policy over resources admitted by the Present owner. This does not acquire or
// reinterpret external resources. Built-in color means the final Present image;
// constant depth/zero motion are placeholders, never real Built-in guide offers.
// Preference eligibility only: this does not observe guides, grant a resource
// lease, or authorize a prepared output claim. Physical qualification is later.
inline bool AllowsGuidedSource(const Connections::Mixed::Policy& policy,
 Connections::Source guideSource) noexcept {
 using Connections::Source;
 const auto permitted=[&](Source preference,Source actual) noexcept {
  return !policy.strict||preference==Source::Automatic||preference==actual;
 };
 return permitted(policy.color,Source::BuiltIn)&&permitted(policy.depth,guideSource)&&
  permitted(policy.motion,guideSource);
}
struct Decision { Connections::SignalStatusWireV2 receipt; bool allowed=true,guidesOmitted=false; const char* reason=nullptr; };
inline Decision Choose(const Connections::Mixed::Policy& policy,bool guided,
 Connections::Source guideSource=Connections::Source::Native) noexcept {
 using namespace Connections;using namespace Mixed;
 Decision result;result.receipt.revision=policy.revision;
 const Source preferences[]={policy.color,policy.depth,policy.motion};
 uint32_t* selected[]={&result.receipt.color,&result.receipt.depth,&result.receipt.motion};
 uint32_t* reasons[]={&result.receipt.colorReason,&result.receipt.depthReason,&result.receipt.motionReason};
 for(unsigned i=0;i<3;++i){
  const auto actual=i==0?Source::BuiltIn:guided?guideSource:Source::Automatic;
  const auto preference=preferences[i];
  const bool matched=preference==Source::Automatic||preference==actual;
  const bool refused=policy.strict&&!matched;
  *selected[i]=refused?0:uint32_t(actual);
  *reasons[i]=uint32_t(refused?(preference==Source::None?Reason::IntentionallyDisabled:Reason::UnsupportedProvider):
    !matched?Reason::PreferenceFallback:actual==Source::Automatic?Reason::Unavailable:Reason::Ready);
  if(refused){
   if(i==0){result.allowed=false;result.reason="Color is required. Choose an available source or allow fallback.";}
   else result.guidesOmitted=true;
  }
 }
 // Each admitted guide adapter provides depth and motion as one compatible pair.
 // Refusing either omits that pair; it does not silently substitute a source.
 if(result.guidesOmitted){
  result.receipt.depth=result.receipt.motion=0;
  for(auto* reason:{&result.receipt.depthReason,&result.receipt.motionReason})
   if(*reason==uint32_t(Reason::Ready)||*reason==uint32_t(Reason::PreferenceFallback))*reason=uint32_t(Reason::Unavailable);
  if(result.allowed)result.reason="Image only; selected guide source refused. Depth and motion are not used.";
 }
 return result;
}
}
