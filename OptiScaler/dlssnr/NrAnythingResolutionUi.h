#pragma once
#include "../NrConfigState.h"
#include <algorithm>

namespace DlssNr::AnythingResolutionUi {
inline constexpr uint32_t Percentages[]={100,75,67,50};
inline constexpr int Manual=4;
template<class C> uint32_t Percent(const C& config){return std::clamp(config.DlssNrAnythingScale.value_or_default(),25u,100u);}
template<class C> int Selection(const C& config){
 if(config.DlssNrUiAnythingResolutionPreset.value_or_default()==Manual)return Manual;
 const auto percent=Percent(config);
 for(int i=0;i<Manual;++i)if(percent==Percentages[i])return i;
 return Manual;
}
template<class C> void Select(C& config,int choice){
 if(choice<0||choice>Manual)return;
 NrConfigSynchronization::Transaction transaction;
 if(Selection(config)==Manual)config.DlssNrUiAnythingManualScale=Percent(config);
 config.DlssNrUiAnythingResolutionPreset=uint32_t(choice);
 config.DlssNrAnythingScale=choice==Manual?
  std::clamp(config.DlssNrUiAnythingManualScale.value_or_default(),25u,100u):Percentages[choice];
}
template<class C> void SetManual(C& config,int percent){
 NrConfigSynchronization::Transaction transaction;
 const auto value=uint32_t(std::clamp(percent,25,100));
 config.DlssNrUiAnythingResolutionPreset=uint32_t(Manual);
 config.DlssNrUiAnythingManualScale=value;config.DlssNrAnythingScale=value;
}
}
