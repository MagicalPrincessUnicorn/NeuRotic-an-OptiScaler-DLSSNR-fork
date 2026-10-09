#include <menu/Localization.h>
#pragma once
#include <cstdint>
namespace DlssNr::NativeFg {
enum class PresentationState { Off, RestartRequired, Unavailable, Waiting, Generating, Stale };
class PresentationObservation {
 uint64_t identity_=0,count_=0;double last_=0,previous_=0;bool observed_=false;
public:
 PresentationState Update(bool requested,bool selected,bool admitted,uint64_t identity,uint64_t retired,double now){
  if(identity_!=identity||retired<count_||now<previous_){identity_=identity;count_=retired;last_=now;observed_=false;}
  else if(retired>count_){last_=now;observed_=true;count_=retired;}
  previous_=now;
  if(requested!=selected)return PresentationState::RestartRequired;
  if(!requested)return PresentationState::Off;
  if(!admitted){observed_=false;return PresentationState::Unavailable;}
  if(!observed_)return retired?PresentationState::Stale:PresentationState::Waiting;
  return now-last_<=1.5?PresentationState::Generating:PresentationState::Stale;
 }
};
inline const char* PresentationLabel(PresentationState state){switch(state){
 case PresentationState::Off:return Neurotic::UiLiteral("ingame.objectruleeditor.off_dc516be5", "Off");case PresentationState::RestartRequired:return Neurotic::UiLiteral("ingame.dlssnr-menustatus.restart_required_e1c4df74", "Restart required");
 case PresentationState::Unavailable:return Neurotic::UiLiteral("ingame.connection.ca1844969742", "Unavailable");case PresentationState::Waiting:return Neurotic::UiLiteral("ingame.nativefgpresentation.waiting_for_generated_completion_f806aaa8", "Waiting for generated completion");
 case PresentationState::Generating:return Neurotic::UiLiteral("ingame.nativefgpresentation.generating_presentation_progress_observed_25e095b9", "Generating (presentation progress observed)");default:return Neurotic::UiLiteral("ingame.nativefgpresentation.no_recent_presentation_progress_57a94dc5", "No recent presentation progress");}}
}
