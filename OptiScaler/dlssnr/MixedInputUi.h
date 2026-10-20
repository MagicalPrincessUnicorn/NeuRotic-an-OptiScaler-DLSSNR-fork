#include <menu/ItemHelp.h>
#pragma once
#include <optional>
#include "connections/ConnectionPolicy.h"
#include "NrPipelineUi.h"
#include "NrStatusPanel.h"
#include "SourceAvailability.h"
namespace DlssNr {
inline void InputHelp(const char* text){
 if(Neurotic::ItemHelpHovered()){ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*28.f);ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();}
}
inline const char* InputReasonText(Connections::Mixed::Reason reason){
 using R=Connections::Mixed::Reason;
 switch(reason){case R::MissingLease:return "Source cannot be used safely";case R::UnsupportedProvider:return "Source not supported";case R::IntentionallyDisabled:return "None selected; fallback disabled";case R::UnknownConvention:return "Source format not confirmed";case R::PreferenceFallback:return "Preferred source unavailable; using fallback";default:return Connections::Mixed::ReasonText(reason);}
}
inline std::string MixedSourceText(int s){switch(s){case 0:return "Auto";case 1:return "Native";case 2:return "Built-in";case 3:return "ReShade";case 4:return "External";case 5:return "None";default:return "Unknown source #"+std::to_string(s);}}
template<class C> bool ApplyMixedInputPreferences(C* c,int depth,int motion,bool strict,int color=0){
 if(!Connections::RequestSavedSignalPreferences(depth,motion,strict,color))return false;
 {NrConfigSynchronization::Transaction transaction;c->DlssNrColorSource=color;c->DlssNrDepthSource=depth;c->DlssNrMotionSource=motion;c->DlssNrSignalStrict=strict;}
 c->SaveIni();return true;
}
inline float InputActionWidth(const char* text){return ObservationRefreshButtonWidth(text);}
inline bool InputAction(const char* id,const char* idle,const char* done,bool busy,bool complete,bool automatic=false){
 const float font=ImGui::GetFontSize();
 const float width=(std::max)(InputActionWidth(idle),InputActionWidth(done));
 ImGui::BeginDisabled(busy);const bool pressed=ImGui::Button(id,{width,0});ImGui::EndDisabled();
 const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();auto* draw=ImGui::GetWindowDrawList();
 const ImVec2 p(lo.x+ImGui::GetStyle().FramePadding.x,lo.y+(hi.y-lo.y-font)*.5f);
 const auto ink=ImGui::GetColorU32(complete?PipelineUi::Color(PipelineView::State::Used):ImGui::GetStyleColorVec4(ImGuiCol_Text));
 if(complete)ImGui::RenderCheckMark(draw,p,ink,font);
 else if(automatic){
  // Three incoming lines joining one automatic selection.
  for(float y:{.2f,.5f,.8f})draw->AddLine({p.x,p.y+font*y},{p.x+font*.55f,p.y+font*.5f},ink,1.5f);
  Neurotic::Sleek::Chevron(draw,{p.x+font*.8f,p.y+font*.5f},font*.6f,ink,0);
 }else Neurotic::DrawRefreshIcon(draw,p,font,ink,busy,Neurotic::Sleek::reducedMotion);
 const char* text=complete?done:busy?"Refreshing":idle;
 draw->AddText({p.x+font*1.5f,p.y},ink,text);
 return pressed;
}
template<class C> bool UseAutoInputs(C* c){
 auto* storage=ImGui::GetStateStorage();const auto key=ImGui::GetID("##AutoInputsApplied");
 const float now=float(ImGui::GetTime()),completed=storage->GetFloat(key,-100.f);
 const bool pressed=InputAction("##NrAutoInputs","Auto-select all inputs","Auto-selected",false,now>=completed&&now-completed<1.8f,true);
 InputHelp("Choose sources for all three inputs automatically. Clears manual preferences and turns off Require selected sources.");
 const bool applied=pressed&&ApplyMixedInputPreferences(c,0,0,false);
 if(applied)storage->SetFloat(key,now);return applied;
}
struct InputSourceCache {
 SourceAvailability::Refresh refresh;SourceAvailability::Inventory inventory{};
 SourceAvailability::Context context{};Connections::SignalStatusSnapshot current{};uint64_t probe=0,updatedAt=0,completedAt=0;bool manual=false;
};
inline InputSourceCache& InputSources(){static InputSourceCache cache;return cache;}
inline void RefreshSourceButton(){
 auto& cache=InputSources();
 const auto now=GetTickCount64();const bool busy=cache.manual||cache.refresh.Pending();
 const bool complete=!busy&&cache.completedAt&&now>=cache.completedAt&&now-cache.completedAt<1800;
 if(InputAction("##RefreshSources","Refresh sources","Complete",busy,complete))cache.manual=true;
 InputHelp("Check available sources using existing frame data. Does not start capture or activate sources.");
}
inline void UpdateSourceAvailability(const SourceAvailability::Context& context,
 const Connections::SignalStatusSnapshot& signals,bool directPresent,bool nativeGuides,uint64_t now){
 using namespace SourceAvailability;auto& cache=InputSources();cache.current=signals;cache.updatedAt=now;
 const bool changed=cache.refresh.Update(context,now);
 if(changed||cache.manual){cache.context=context;cache.inventory={};cache.completedAt=0;cache.refresh.Request(now);
  if(cache.probe)NativeProbe().Complete(cache.probe,0);cache.probe=0;cache.manual=false;}
 if(cache.refresh.StartProbe(now))cache.probe=NativeProbe().Request();
 if(!cache.refresh.Ready(now))return;
 const auto mask=NativeProbe().Read(cache.probe);
 // Raw callback presence proves detection only, never binding compatibility.
 for(unsigned i=0;i<3;++i)if(mask&(1u<<i))cache.inventory[i][0]=State::Detected;
 if(directPresent){
  cache.inventory[0][0]=State::Unsupported; // Native scene color is a different stage.
  if(context.presentLive)cache.inventory[0][1]=State::Detected;
  if(signals.fresh){cache.inventory[0][1]=State::Available;
   if(nativeGuides)cache.inventory[1][0]=cache.inventory[2][0]=State::Available;}
 }
 if(signals.fresh){const unsigned selected[]={signals.status.color,signals.status.depth,signals.status.motion};
  for(unsigned i=0;i<3;++i)if(selected[i]>=1&&selected[i]<=4)cache.inventory[i][selected[i]-1]=State::Available;}
 NativeProbe().Complete(cache.probe,0); // Close an unanswered request; no perpetual probing.
 cache.refresh.Complete();cache.completedAt=now;
}
inline std::string InputSourcesReport(){
 const auto& cache=InputSources();const auto policy=Connections::SignalPreferences();
 const auto now=GetTickCount64();const bool live=cache.updatedAt&&now>=cache.updatedAt&&now-cache.updatedAt<500&&cache.context.policy==policy.revision;
 std::string report="\n\nInput source preferences (current menu snapshot)\n";
 report+="Policy revision: "+std::to_string(policy.revision)+"; require selected sources: "+(policy.strict?"yes":"no")+"\n";
 report+=cache.refresh.Pending()?"Availability refresh pending\n":"Availability cached; discovery is not continuous\n";
 const unsigned preferences[]={unsigned(policy.color),unsigned(policy.depth),unsigned(policy.motion)};
 const unsigned selected[]={cache.current.status.color,cache.current.status.depth,cache.current.status.motion};
 const unsigned used[]={cache.current.used.color,cache.current.used.depth,cache.current.used.motion};
 const char* names[]={"Color","Depth","Motion"};
 for(unsigned i=0;i<3;++i){report+=std::string(names[i])+": preferred "+MixedSourceText(preferences[i])+"; selected "+
  (live&&cache.current.fresh&&selected[i]?MixedSourceText(selected[i]):"not reported")+"; completed use "+
  (live&&cache.context.enabled&&cache.current.usedFresh&&used[i]?MixedSourceText(used[i]):"not reported")+"\n";
  for(unsigned provider=0;provider<4;++provider){const auto state=cache.inventory[i][provider];
   report+="  "+MixedSourceText(provider+1)+": "+(cache.refresh.Pending()?"needs refresh":state==SourceAvailability::State::Available?"available at refresh":
    state==SourceAvailability::State::Detected?"detected at refresh":state==SourceAvailability::State::Unsupported?"different processing stage":"not observed")+"\n";}
 }
 return report;
}
struct InputPanelContext {
 bool supported=true,enabled=true,outputActive=false,useOwnerSignals=false,directPresent=false,observationsSupported=false;
 Connections::SignalStatusSnapshot ownerSignals{};
 int colorSource=0;bool colorUsed=false,jitterDetected=false,exposureDetected=false;std::string colorName;
};
inline void InputStateText(PipelineView::State state,const std::string& label){
 ImGui::PushStyleColor(ImGuiCol_Text,PipelineUi::Color(state));ImGui::TextWrapped("%s %s",PipelineUi::Mark(state),label.c_str());ImGui::PopStyleColor();
}
template<class C> bool RequireSelectedSources(C* c){
 bool strict=c->DlssNrSignalStrict.value_or_default();
 const bool changed=ImGui::Checkbox(Neurotic::UiLiteral("ingame.mixed-input.require_selected_sources_68964ea5","Require selected sources"),&strict);
 InputHelp(Neurotic::UiLiteral("ingame.mixed-input.require_sources_help","Prevent source substitution. Required inputs pause NR when unavailable."));
 return changed&&ApplyMixedInputPreferences(c,c->DlssNrDepthSource.value_or_default(),c->DlssNrMotionSource.value_or_default(),strict,c->DlssNrColorSource.value_or_default());
}
inline void InputSectionTitle(const char* title,float width=0.f,float scale=1.2f){
 const float font=ImGui::GetFontSize();const auto pos=ImGui::GetCursorScreenPos();
 ImGui::Dummy({width>0.f?width:ImGui::GetContentRegionAvail().x,(std::max)(ImGui::GetFrameHeight(),font*1.25f)});
 ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),font*scale,pos,ImGui::GetColorU32(ImGuiCol_Text),title);
}
inline bool BeginInputCard(const char* id,const char* title,float height){
 const float font=ImGui::GetFontSize();
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(font*.65f,font*.55f));
 ImGui::PushStyleColor(ImGuiCol_ChildBg,Neurotic::Sleek::Mix(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg),ImGui::GetStyleColorVec4(ImGuiCol_FrameBg),.35f));
 const bool visible=ImGui::BeginChild(id,{0,height},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding);
 if(visible){InputSectionTitle(title);ImGui::Separator();}
 return visible;
}
inline void EndInputCard(){ImGui::EndChild();ImGui::PopStyleColor();ImGui::PopStyleVar();}
template<class C> bool DrawMixedInputPreferences(C* config,bool outputActive,InputPanelContext context={},int focus=-1,void (*depthOptions)(C*)=nullptr){
 namespace P=Connections;namespace V=PipelineView;bool changed=false;
 context.outputActive=outputActive;context.enabled=context.enabled&&config->DlssNrEnabled.value_or_default();
 const auto liveSnapshot=context.useOwnerSignals?context.ownerSignals:P::QueryInputSignals(GetTickCount64());
 const auto snapshot=context.useOwnerSignals||liveSnapshot.fresh?liveSnapshot:P::QueryCompletedInputSignals(GetTickCount64());
 const auto save=[&](int c,int d,int m,bool s){changed|=ApplyMixedInputPreferences(config,d,m,s,c);};
 if(focus<0)ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.mixed-input.source_preference_44bff705","Source preference"));
 const int color=config->DlssNrColorSource.value_or_default(),depth=config->DlssNrDepthSource.value_or_default(),motion=config->DlssNrMotionSource.value_or_default();
 if(focus>=0&&focus<=2){
  const float font=ImGui::GetFontSize();const bool isColor=focus==0,isMotion=focus==2;
  const int preference=isColor?color:isMotion?motion:depth;
  const auto selected=isColor?snapshot.status.color:isMotion?snapshot.status.motion:snapshot.status.depth;
  const auto used=isColor?snapshot.used.color:isMotion?snapshot.used.motion:snapshot.used.depth;
  const auto reason=static_cast<P::Mixed::Reason>(isColor?snapshot.status.colorReason:isMotion?snapshot.status.motionReason:snapshot.status.depthReason);
  bool strict=config->DlssNrSignalStrict.value_or_default();
  const bool fresh=(context.supported||context.observationsSupported)&&snapshot.fresh;
  const bool refused=fresh&&!selected&&(strict&&preference!=0||reason!=P::Mixed::Reason::Ready&&reason!=P::Mixed::Reason::Unavailable);
  const auto view=V::Project({context.supported?preference:0,selected,used,fresh,(context.supported||context.observationsSupported)&&snapshot.usedFresh,context.enabled,outputActive,refused&&strict&&!outputActive,refused});
  const bool directColor=isColor&&view.used&&context.enabled&&outputActive&&context.colorUsed&&!context.colorName.empty();
  const bool columns=ImGui::GetContentRegionAvail().x>=font*32&&ImGui::BeginTable("##FocusedSignal",2,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX);
  if(columns)ImGui::TableNextColumn();
  const float cardHeight=font*(focus==1?17.f:13.f);
  if(BeginInputCard("##InputConnection","Connection",cardHeight)){
   if(ImGui::BeginTable("##ConnectionReadouts",2,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_NoPadOuterX)){
    ImGui::TableSetupColumn("Label",ImGuiTableColumnFlags_WidthStretch,.85f);ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch,1.15f);
    const auto readout=[&](const char* label,const std::string& value,const ImVec4& color){
     ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextDisabled("%s",label);ImGui::TableNextColumn();StatusPanel::Line(label,value,color);
    };
    readout("Currently used",!context.enabled?"NR off":directColor?context.colorName:view.used?MixedSourceText(int(view.used)):"Not reported",PipelineUi::Color(directColor||view.used?V::State::Used:V::State::Unknown));
    readout("Your preference",MixedSourceText(preference),ImGui::GetStyleColorVec4(ImGuiCol_Text));
    const auto state=view.state==V::State::Fallback||view.state==V::State::Blocked?view.state:directColor?V::State::Used:view.state;
    readout("Status",std::string(PipelineUi::Mark(state))+" "+(refused&&strict&&outputActive?"Input omitted":PipelineUi::StateText(state)),PipelineUi::Color(state));
    const int captureOverride=config->DlssNrInputSource.value_or_default();
    if(captureOverride!=0)readout("Capture override",MixedSourceText(captureOverride),PipelineUi::Color(V::State::Fallback));
    ImGui::EndTable();
   }

   if(focus==1&&depthOptions)depthOptions(config);
   if(!context.supported)ImGui::TextWrapped(Neurotic::UiLiteral("ingame.mixed-input.this_method_uses_fixed_input_sources_saved_preferenc_c1f66ed7","This method uses fixed input sources. Saved preferences are not applied."));
   else if(fresh&&reason==P::Mixed::Reason::PreferenceFallback)StatusPanel::Line("##SourceFallback","Using a fallback source",PipelineUi::Color(V::State::Fallback));
   else if(context.directPresent&&!isColor&&fresh&&!selected)StatusPanel::Line("##NeutralGuides","Image only; depth and motion are not used",PipelineUi::Color(V::State::Unknown));
   else if(refused)StatusPanel::Line("##SourceRefusal",InputReasonText(reason),PipelineUi::Color(strict?V::State::Blocked:V::State::Fallback));
  }
  EndInputCard();
  if(columns)ImGui::TableNextColumn();else ImGui::Spacing();
  if(BeginInputCard("##InputSources","Source preferences",cardHeight)){
  const float gap=font*.3f;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,ImVec2(gap*.5f,gap*.5f));
  if(ImGui::BeginTable("##SourceButtons",2,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX)){
   for(int provider=0;provider<=5;++provider){
    ImGui::TableNextColumn();ImGui::PushID(provider);
    const auto cached=provider>=1&&provider<=4?InputSources().inventory[focus][provider-1]:SourceAvailability::State::NotObserved;
    auto state=provider&&view.used==uint32_t(provider)?V::State::Used:provider&&view.selected==uint32_t(provider)?V::State::Detected:
        cached==SourceAvailability::State::Available||cached==SourceAvailability::State::Detected?V::State::Detected:V::State::Unknown;
    const bool rejected=provider==preference&&provider!=0&&context.supported&&snapshot.fresh&&view.selected!=uint32_t(provider);
    if(rejected)state=strict&&!outputActive?V::State::Blocked:V::State::Fallback;
    const char* caption=provider==0?(context.supported?"Best available":"Fixed sources"):!context.supported?(view.used==uint32_t(provider)?"In use":"Not selectable"):rejected?(strict?"Refused":"Fallback"):
        provider==5?(strict?"Withhold input":"Prefer none"):view.used==uint32_t(provider)?"In use":view.selected==uint32_t(provider)?"Available":
        InputSources().refresh.Pending()?"Refreshing":cached==SourceAvailability::State::Available?"Available":
        cached==SourceAvailability::State::Detected?"Detected":cached==SourceAvailability::State::Unsupported?"Other stage":"Not observed";
    ImGui::PushStyleColor(ImGuiCol_Button,PipelineUi::SelectedFill(provider==preference));
    ImGui::BeginDisabled(!context.supported);
    if(ImGui::Button("##Source",{ImGui::GetContentRegionAvail().x,ImGui::GetFrameHeight()*1.15f}))save(isColor?provider:color,focus==1?provider:depth,isMotion?provider:motion,strict);
    ImGui::EndDisabled();
    ImGui::PopStyleColor();const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();auto* draw=ImGui::GetWindowDrawList();
    PipelineUi::Attention(context.enabled?state:V::State::Unknown,config->MenuReduceMotion.value_or_default());
    const auto label=MixedSourceText(provider);
    const float middleY=lo.y+(hi.y-lo.y-font)*.5f;
    const float labelRight=lo.x+(hi.x-lo.x)*.46f;
    ImGui::RenderTextEllipsis(draw,{lo.x+font*.35f,middleY},{labelRight,hi.y},labelRight,label.c_str(),nullptr,nullptr);
    ImGui::PushStyleColor(ImGuiCol_Text,PipelineUi::Color(state));
    const float captionLeft=(std::max)(labelRight+font*.2f,hi.x-font*.3f-ImGui::CalcTextSize(caption).x);
    ImGui::RenderTextEllipsis(draw,{captionLeft,middleY},{hi.x-font*.3f,hi.y},hi.x-font*.3f,caption,nullptr,nullptr);ImGui::PopStyleColor();
    if(Neurotic::ItemHelpHovered()){ImGui::BeginTooltip();ImGui::PushTextWrapPos(font*26);ImGui::TextUnformatted(label.c_str());ImGui::TextUnformatted(caption);ImGui::TextWrapped("%s",provider==5?Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.prefer_no_input_enable_require_selected_sources_to_p_4962e811","Prefer no input. Enable Require selected sources to prevent fallback. NR pauses if a required input is missing.")).c_str():rejected?InputReasonText(reason):cached==SourceAvailability::State::Unsupported?Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.native_color_is_from_an_earlier_stage_present_uses_t_95b687ba","Native color is from an earlier stage. Present uses the final game image.")).c_str():Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.availability_reflects_the_last_refresh_in_use_means__45341090","Availability reflects the last refresh. In use means recent processing used this source. Not observed means no evidence yet.")).c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();}
    ImGui::PopID();
   }
   ImGui::EndTable();
  }
  ImGui::PopStyleVar();
  ImGui::TextWrapped(context.supported?Neurotic::UiLiteral("ingame.mixed-input.auto_chooses_compatible_inputs_refresh_sources_updat_4ac536d4","Auto chooses compatible inputs. Refresh sources updates availability."):Neurotic::UiLiteral("ingame.mixed-input.input_choices_are_unavailable_for_this_method_22174bd1","Input choices are unavailable for this method."));
  }
  EndInputCard();if(columns)ImGui::EndTable();
  return changed;
 }
 int preset=color==depth&&depth==motion&&depth>=0&&depth<=5?depth:6;
 const float font=ImGui::GetFontSize();
 const bool wide=focus<0&&ImGui::GetContentRegionAvail().x>=font*31;
 ImGui::SetNextItemWidth((std::min)(ImGui::GetContentRegionAvail().x,font*12));
 const char* presets[]={"Auto","Native","Built-in","ReShade","External","None","Custom / Mixed"};
 if(focus<0&&ImGui::Combo("##InputSourcePreset",&preset,presets,7)&&preset<6)save(preset,preset,preset,config->DlssNrSignalStrict.value_or_default());
 bool strict=config->DlssNrSignalStrict.value_or_default();
 if(wide)ImGui::SameLine(0,font);
 if(ImGui::Checkbox(Neurotic::UiLiteral("ingame.mixed-input.require_selected_sources_68964ea5","Require selected sources"),&strict))save(color,depth,motion,strict);
 ImGui::TextWrapped("%s",context.supported?Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.choose_sources_for_color_depth_and_motion_a0f729f5","Choose sources for color, depth and motion.")).c_str():Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.preferences_saved_not_supported_by_this_method_77946faa","Preferences saved; not supported by this method.")).c_str());
 ImGui::Spacing();
 ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{font*.45f,font*.4f});
 if(!wide||ImGui::BeginTable("##SignalPreferences",3,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_BordersInnerH)){
  // ImGui copies column names. Bind those copies for the whole table lifetime,
  // including its header/context-menu consumers, without changing label bytes/IDs.
  std::optional<Neurotic::ScopedUiLiteral> signalHeader,preferredHeader,effectiveHeader;
  if(wide){
  ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.mixed-input.signal_1e9806e4","Signal"),ImGuiTableColumnFlags_WidthStretch,.95f);
  ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.mixed-input.preferred_source_185bfa90","Preferred source"),ImGuiTableColumnFlags_WidthStretch,1);
  ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.mixed-input.effective_source_5d4211c5","Effective source"),ImGuiTableColumnFlags_WidthStretch,1.25f);
  signalHeader.emplace("ingame.mixed-input.signal_1e9806e4",ImGui::TableGetColumnName(0));
  preferredHeader.emplace("ingame.mixed-input.preferred_source_185bfa90",ImGui::TableGetColumnName(1));
  effectiveHeader.emplace("ingame.mixed-input.effective_source_5d4211c5",ImGui::TableGetColumnName(2));
  ImGui::TableHeadersRow();}
  const char* names[]={"Scene color","Depth guide","Motion vectors","Jitter","Exposure"};
  for(int row=0;row<5;++row){
   if(focus>=0&&row!=focus)continue;
   ImGui::PushID(row);
   if(!wide){ImGui::Separator();ImGui::TextUnformatted(names[row]);ImGui::BeginTable("##SignalChoice",2,ImGuiTableFlags_SizingStretchProp);
    ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.mixed-input.preferred_source_185bfa90","Preferred source"),ImGuiTableColumnFlags_WidthStretch,1);
    ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.mixed-input.effective_source_5d4211c5","Effective source"),ImGuiTableColumnFlags_WidthStretch,1.25f);}
   ImGui::TableNextRow();ImGui::TableNextColumn();if(wide){ImGui::AlignTextToFramePadding();ImGui::TextWrapped("%s",names[row]);ImGui::TableNextColumn();}
   const bool editable=row<=2,isColor=row==0,isMotion=row==2;V::SignalView view;P::Mixed::Reason reason=P::Mixed::Reason::Unavailable;
   if(editable){
    const auto selected=isColor?snapshot.status.color:isMotion?snapshot.status.motion:snapshot.status.depth,used=isColor?snapshot.used.color:isMotion?snapshot.used.motion:snapshot.used.depth;
    reason=static_cast<P::Mixed::Reason>(isColor?snapshot.status.colorReason:isMotion?snapshot.status.motionReason:snapshot.status.depthReason);
    const bool fresh=(context.supported||context.observationsSupported)&&snapshot.fresh;
    const int preference=isColor?color:isMotion?motion:depth;
    const bool refused=fresh&&!selected&&(config->DlssNrSignalStrict.value_or_default()&&preference!=0||
        reason!=P::Mixed::Reason::Ready&&reason!=P::Mixed::Reason::Unavailable);
    view=V::Project({context.supported?preference:0,selected,used,fresh,(context.supported||context.observationsSupported)&&snapshot.usedFresh,context.enabled,outputActive,refused&&config->DlssNrSignalStrict.value_or_default(),refused});
    const float limit=(std::max)(1.f,ImGui::GetMainViewport()->WorkSize.x-ImGui::GetStyle().WindowPadding.x*2);
    ImGui::SetNextWindowSizeConstraints({(std::min)(limit,ImGui::GetFontSize()*20),0},{limit,ImGui::GetFontSize()*21});ImGui::SetNextItemWidth(-1);
    if(focus>=0)ImGui::TextDisabled(Neurotic::UiLiteral("ingame.mixed-input.source_preference_44bff705","Source preference"));
    if(ImGui::BeginCombo("##SourcePreference",MixedSourceText(preference).c_str())){
     if(preference<0||preference>5){ImGui::TextWrapped("%s",MixedSourceText(preference).c_str());ImGui::Separator();}
     for(int p=0;p<=5;++p){
      const auto label=MixedSourceText(p)+"###Provider"+std::to_string(p);
      const auto rowMin=ImGui::GetCursorScreenPos();const float rowWidth=ImGui::GetContentRegionAvail().x;
      if(ImGui::Selectable(label.c_str(),preference==p,0,{0,font*1.65f}))save(isColor?p:color,row==1?p:depth,isMotion?p:motion,config->DlssNrSignalStrict.value_or_default());
      if(preference==p)ImGui::SetItemDefaultFocus();
      const auto cached=p>=1&&p<=4?InputSources().inventory[row][p-1]:SourceAvailability::State::NotObserved;
      auto state=p&&view.used==uint32_t(p)?V::State::Used:p&&view.selected==uint32_t(p)?V::State::Detected:
        cached==SourceAvailability::State::Available||cached==SourceAvailability::State::Detected?V::State::Detected:V::State::Unknown;
      const bool rejected=p==preference&&preference!=0&&context.supported&&snapshot.fresh&&view.selected!=uint32_t(p);
      if(rejected)state=strict&&!outputActive?V::State::Blocked:V::State::Fallback;
      const char* caption=p==0?"Compatible choice":!context.supported?"Not applied":rejected?(strict?"Refused":"Fallback"):
       p==5?(strict?"Withheld":"Prefer none"):view.used==uint32_t(p)?"In use":view.selected==uint32_t(p)?"Available":InputSources().refresh.Pending()?"Refreshing":
       cached==SourceAvailability::State::Available?"Available":cached==SourceAvailability::State::Detected?"Detected":cached==SourceAvailability::State::Unsupported?"Other stage":"Not observed";
      const auto status=std::string(PipelineUi::Mark(state))+" "+caption;
      const float size=font*.8f,tw=ImGui::CalcTextSize(status.c_str()).x*.8f;
      ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),size,{rowMin.x+rowWidth-tw-font*.2f,rowMin.y+font*.2f},ImGui::GetColorU32(PipelineUi::Color(state)),status.c_str());
      if(Neurotic::ItemHelpHovered(0,ImGuiHoveredFlags_DelayNormal)){
       ImGui::BeginTooltip();ImGui::PushTextWrapPos(font*22);
       ImGui::TextWrapped("%s",p==0?Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.choose_a_compatible_source_cbdf2a1b","Choose a compatible source.")).c_str():p==5?Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.prefer_no_input_require_selected_sources_prevents_fa_c5be3e82","Prefer no input. Require selected sources prevents fallback.")).c_str():rejected?InputReasonText(reason):Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.availability_reflects_the_last_refresh_in_use_confir_ff2160ab","Availability reflects the last refresh. In use confirms recent processing.")).c_str());
       ImGui::PopTextWrapPos();ImGui::EndTooltip();}
     }
     ImGui::EndCombo();
    }
   }else {ImGui::AlignTextToFramePadding();ImGui::TextDisabled(Neurotic::UiLiteral("ingame.mixed-input.automatic_d461a493","Automatic"));}
   ImGui::TableNextColumn();
   if(editable){
    const auto provider=view.used?view.used:view.selected;const auto label=provider?MixedSourceText(int(provider)):std::string("Unknown");
    InputStateText(view.state,label+" - "+PipelineUi::StateText(view.state));
    ImGui::TextDisabled("%s",view.used?Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.used_by_nr_dadc94fa","Used by NR")).c_str():view.selected?Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.not_in_use_e5c557a8","Not in use")).c_str():Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.no_recent_use_1890559d","No recent use")).c_str());
    if(Neurotic::ItemHelpHovered()){
     ImGui::BeginTooltip();ImGui::Text(Neurotic::UiLiteral("ingame.mixed-input.preferred_s_c6377036","Preferred: %s"),MixedSourceText(view.preferred).c_str());ImGui::Text(Neurotic::UiLiteral("ingame.mixed-input.selected_s_6f1df5fc","Selected: %s"),view.selected?MixedSourceText(int(view.selected)).c_str():Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.unknown_b764cdc0","Unknown")).c_str());
     ImGui::Text(Neurotic::UiLiteral("ingame.mixed-input.used_s_0ce6f0f7","Used: %s"),view.used?MixedSourceText(int(view.used)).c_str():Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.no_recent_use_1890559d","No recent use")).c_str());ImGui::Text(Neurotic::UiLiteral("ingame.mixed-input.policy_llu_capture_llu_7cd5a6e9","Policy %llu / capture %llu"),snapshot.status.revision,snapshot.status.capture);ImGui::EndTooltip();
    }
   }else if(row==0){const bool used=context.enabled&&context.colorUsed&&outputActive;
    const auto name=!context.colorName.empty()?context.colorName:context.colorSource?MixedSourceText(context.colorSource):std::string("Unknown");
    InputStateText(used?V::State::Used:V::State::Unknown,name+(used?" - In use":" - Not in use"));}
   else{const bool detected=row==3?context.jitterDetected:context.exposureDetected;InputStateText(detected?V::State::Detected:V::State::Unknown,detected?"Native - Detected":"Unknown");}
   if(!wide)ImGui::EndTable();ImGui::PopID();
  }
  if(wide)ImGui::EndTable();
 }
 ImGui::PopStyleVar();
 ImGui::Separator();
 if(ImGui::TreeNodeEx("Source details",ImGuiTreeNodeFlags_SpanAvailWidth)){
  ImGui::TextWrapped(Neurotic::UiLiteral("ingame.mixed-input.preferences_allow_fallback_unless_require_selected_s_e75a0595","Preferences allow fallback unless Require selected sources is enabled. This also applies to None. Jitter and exposure are automatic. Sources must match the current frame."));
  for(int row=0;row<3;++row){const auto selected=row==0?snapshot.status.color:row==1?snapshot.status.depth:snapshot.status.motion;
   const auto used=row==0?snapshot.used.color:row==1?snapshot.used.depth:snapshot.used.motion;
   const auto reason=static_cast<P::Mixed::Reason>(row==0?snapshot.status.colorReason:row==1?snapshot.status.depthReason:snapshot.status.motionReason);
   ImGui::TextWrapped(Neurotic::UiLiteral("ingame.mixed-input.s_preferred_s_selected_s_used_s_s_59b880dd","%s: preferred %s; selected %s; used %s. %s"),row==0?Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.color_6b73191a","Color")).c_str():row==1?Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.depth_f1dbc339","Depth")).c_str():Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.motion_8ca34424","Motion")).c_str(),MixedSourceText(row==0?color:row==1?depth:motion).c_str(),context.supported&&snapshot.fresh&&selected?MixedSourceText(int(selected)).c_str():Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.unknown_b764cdc0","Unknown")).c_str(),context.enabled&&outputActive&&context.supported&&snapshot.usedFresh&&used?MixedSourceText(int(used)).c_str():Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.no_recent_use_1890559d","No recent use")).c_str(),context.supported&&snapshot.fresh?InputReasonText(reason):Neurotic::Translate(Neurotic::UiLiteral("ingame.mixed-input.no_recent_source_selection_59b2335e","No recent source selection.")).c_str());}
  ImGui::TreePop();
 }
 ImGui::TextWrapped(Neurotic::UiLiteral("ingame.mixed-input.in_use_detected_fallback_waiting_refused_off_unknown_93f6196b","In use | Detected | Fallback / waiting | Refused | Off / unknown"));
 return changed;
}
}
