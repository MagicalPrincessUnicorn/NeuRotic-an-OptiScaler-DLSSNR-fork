#include <menu/Localization.h>
#pragma once
#include <vector>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <cstring>
#include <string_view>
namespace nh {
struct NumericDraft {bool valid=false,automatic=false;double value=0;};
inline NumericDraft ParseNumericDraft(std::string_view text){
 while(!text.empty()&&(text.front()==' '||text.front()=='\t'))text.remove_prefix(1);
 while(!text.empty()&&(text.back()==' '||text.back()=='\t'))text.remove_suffix(1);
 if(text==Neurotic::UiLiteral("desktop.gamesettingsview.auto_d1f397f2", "auto")||text=="AUTO"||text==Neurotic::UiLiteral("desktop.numericsettings.auto_d2dcb07c", "Auto"))return {true,true,0};
 if(text.empty())return {};if(text.front()=='+')text.remove_prefix(1);
 double value=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
 return error==std::errc()&&end==text.data()+text.size()&&std::isfinite(value)?NumericDraft{true,false,value}:NumericDraft{};
}
struct NumericRange {bool enabled=false,integer=false;double minimum=0,maximum=0;};
template<class Field> NumericRange NumericSliderRange(const Field& field,const std::vector<Field>& fields){
 if(!field.hasRange||!field.choices.empty()||(field.type!="float"&&field.type!="integer")||!std::isfinite(field.minimum)||!std::isfinite(field.maximum)||field.minimum>=field.maximum)return {};
 NumericRange range{true,field.type=="integer",field.minimum,field.maximum};
 if(field.section==Neurotic::UiLiteral("desktop.objectrulehost.dlssnr_e4e7ac7d", "DlssNr")&&(field.key=="EnhancedCustomScale"||field.key=="PresentCustomScale")){
  const auto key=field.key=="EnhancedCustomScale"?"EnhancedResolution":"PresentResolution";
  auto mode=std::find_if(fields.begin(),fields.end(),[&](const auto& item){return item.section==field.section&&item.key==key;});
  if(mode==fields.end()||(std::string_view(mode->draft.data())!="3"&&std::string_view(mode->draft.data())!="4"))return {};
  range.minimum=25;
 }
 return range;
}
template<class Field> bool SetNumericDraft(Field& field,const std::vector<Field>& fields,double value){
 auto range=NumericSliderRange(field,fields);if(!range.enabled||!std::isfinite(value)||value<range.minimum||value>range.maximum||(range.integer&&std::trunc(value)!=value))return false;
 char text[128];std::to_chars_result result;
 if(range.integer)result=std::to_chars(text,text+sizeof(text)-1,static_cast<long long>(value));
 else result=std::to_chars(text,text+sizeof(text)-1,value,std::chars_format::general,std::numeric_limits<double>::max_digits10);
 if(result.ec!=std::errc()||size_t(result.ptr-text)>=field.draft.size())return false;*result.ptr=0;
 std::memcpy(field.draft.data(),text,size_t(result.ptr-text)+1);return true;
}
}
#ifndef NH_NUMERIC_CORE_ONLY
#include "imgui.h"
#include "../../../OptiScaler/menu/SleekContentCard.h"
namespace nh {
template<class Field> void RenderNumericSetting(Field& field,const std::vector<Field>& fields,bool card=false){
 auto range=NumericSliderRange(field,fields);auto parsed=ParseNumericDraft(field.draft.data());
 const float available=ImGui::GetContentRegionAvail().x;
 const float controlWidth=card?available:std::min(ImGui::GetFontSize()*20,available);
 const float actionsWidth=ImGui::CalcTextSize(Neurotic::UiLiteral("desktop.numericsettings.automaticexact_entry_3dbd510e", "AutomaticExact entry")).x+ImGui::GetStyle().FramePadding.x*4+ImGui::GetStyle().ItemSpacing.x*2;
 if(!range.enabled){ImGui::InputText("##Value",field.draft.data(),field.draft.size());return;}
 const bool editable=parsed.valid&&(parsed.automatic||(parsed.value>=range.minimum&&parsed.value<=range.maximum&&(!range.integer||std::trunc(parsed.value)==parsed.value)));
 if(editable){
  ImGui::SetNextItemWidth(controlWidth);
  double value=parsed.automatic?range.minimum:parsed.value;
  if(range.integer){int number=static_cast<int>(value),minimum=static_cast<int>(range.minimum),maximum=static_cast<int>(range.maximum);if(ImGui::SliderInt("##Value",&number,minimum,maximum,parsed.automatic?Neurotic::UiLiteral("desktop.gamesettingsview.automatic_1573651c", "Automatic"):"%d"))SetNumericDraft(field,fields,number);}
  else if(card?Neurotic::Sleek::CardSliderScalar("##Value",ImGuiDataType_Double,&value,&range.minimum,&range.maximum,parsed.automatic?Neurotic::UiLiteral("desktop.numericsettings.auto_d2dcb07c", "Auto"):"%.3f",ImGuiSliderFlags_NoRoundToFormat):ImGui::SliderScalar("##Value",ImGuiDataType_Double,&value,&range.minimum,&range.maximum,parsed.automatic?Neurotic::UiLiteral("desktop.gamesettingsview.automatic_1573651c", "Automatic"):"%.3f",ImGuiSliderFlags_NoRoundToFormat))SetNumericDraft(field,fields,value);
 }else ImGui::TextWrapped(Neurotic::UiLiteral("desktop.numericsettings.saved_manual_value_s_it_is_preserved_use_exact_e_39e60e19", "Saved/manual value: %s. It is preserved; use exact entry to edit it."),Neurotic::Translate(field.draft.data()).c_str());
 if(editable&&available>=controlWidth+actionsWidth)ImGui::SameLine();
 if(ImGui::SmallButton(Neurotic::UiLiteral("desktop.gamesettingsview.automatic_1573651c", "Automatic")))std::memcpy(field.draft.data(),Neurotic::UiLiteral("desktop.gamesettingsview.auto_d1f397f2", "auto"),5);
 ImGui::SameLine();if(ImGui::SmallButton(Neurotic::UiLiteral("desktop.numericsettings.exact_entry_398b69eb", "Exact entry")))ImGui::OpenPopup(Neurotic::UiLiteral("desktop.numericsettings.numericentry_9956de30", "NumericEntry"));
 if(ImGui::BeginPopup(Neurotic::UiLiteral("desktop.numericsettings.numericentry_9956de30", "NumericEntry"))){ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.numericsettings.exact_value_or_auto_8445a3c2", "Exact value (or auto)"));ImGui::SetNextItemWidth(ImGui::GetFontSize()*16);ImGui::InputText("##ExactValue",field.draft.data(),field.draft.size());if(ImGui::Button(Neurotic::UiLiteral("desktop.anythingview.done_3598b18e", "Done")))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
}
}
#endif
