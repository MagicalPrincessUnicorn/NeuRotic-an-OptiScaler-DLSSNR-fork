#pragma once
#include "SleekWidgets.h"
#include <menu/Localization.h>
#include <algorithm>
#include <string>
namespace nh {
struct SavedReviewAction {std::string caption;float width,height;};
inline float MeasureSavedChoiceWidth(float available,float widestCaption){
 const auto padding=ImGui::GetStyle().FramePadding;
 const float preferred=(std::max)(ImGui::GetFontSize()*6.f,widestCaption+2*padding.x+ImGui::GetFrameHeight());
 return (std::min)((std::max)(1.f,available),preferred);
}
inline float MeasureSavedChoiceWidth(float available,const char* const* labels,int count){
 float widest=0;
 for(int i=0;i<count;++i)widest=(std::max)(widest,ImGui::CalcTextSize(Neurotic::Translate(labels[i]).c_str()).x);
 return MeasureSavedChoiceWidth(available,widest);
}
inline SavedReviewAction MeasureSavedReviewAction(const char* label,float available){
 const auto padding=ImGui::GetStyle().FramePadding;const auto caption=Neurotic::Translate(label);
 const float width=(std::min)(available,ImGui::CalcTextSize(caption.c_str()).x+2*padding.x);
 return {caption,width,ImGui::CalcTextSize(caption.c_str(),nullptr,false,(std::max)(1.f,width-2*padding.x)).y+2*padding.y};
}
inline bool SavedReviewButton(const char* label,const SavedReviewAction& action){
 // Preserve the native label/ID, hit box, disabled state and keyboard behavior.
 // Only the caption wraps when its natural width cannot fit the current pane.
 ImGui::PushStyleColor(ImGuiCol_Text,{0,0,0,0});const bool pressed=ui::Button(label,{action.width,action.height});ImGui::PopStyleColor();
 const auto at=ImGui::GetItemRectMin();const auto padding=ImGui::GetStyle().FramePadding;
 ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{at.x+padding.x,at.y+padding.y},ImGui::GetColorU32(ImGuiCol_Text),action.caption.c_str(),nullptr,(std::max)(1.f,action.width-2*padding.x));
#ifdef NEUROTIC_SAVED_CONTROL_BUTTON_PROBE
 NEUROTIC_SAVED_CONTROL_BUTTON_PROBE(label,ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
#endif
 return pressed;
}
}
