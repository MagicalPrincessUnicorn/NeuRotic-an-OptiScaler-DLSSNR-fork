#include <menu/Localization.h>
#pragma once
#include "imgui.h"
#include <menu/RefreshIcon.h>
#include "imgui_internal.h"
#include "HubViewModel.h"
#include <algorithm>
#include <cmath>
namespace nh::ui {
inline thread_local bool reduced=false;
inline float Animate(ImGuiID id,float target,unsigned salt=0){
 auto& storage=ImGui::GetCurrentWindow()->StateStorage;id^=0x534c454b+salt;auto frameKey=id^0x454d4941;int frame=ImGui::GetFrameCount(),last=storage.GetInt(frameKey,-1000);float value=storage.GetFloat(id,target);
 if(last!=frame){float dt=ImGui::GetIO().DeltaTime;if(reduced||last<frame-1||!std::isfinite(dt)||dt>=.25f)value=target;else value+=(target-value)*(1.f-std::exp(-18.f*std::max(0.f,dt)));storage.SetFloat(id,value);storage.SetInt(frameKey,frame);}return value;
}
inline ImVec4 Mix(ImVec4 a,ImVec4 b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};}
inline bool Button(const char* label,ImVec2 size={}){
 auto id=ImGui::GetID(label);auto& storage=ImGui::GetCurrentWindow()->StateStorage;float t=Animate(id,storage.GetFloat(id^0x484f5645,0));
 auto base=ImGui::GetStyleColorVec4(ImGuiCol_Button);auto hover=ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);auto color=Mix(base,hover,t);
 ImGui::PushStyleColor(ImGuiCol_Button,color);ImGui::PushStyleColor(ImGuiCol_ButtonHovered,color);bool pressed=ImGui::Button(label,size);storage.SetFloat(id^0x484f5645,ImGui::IsItemHovered()?1.f:0.f);ImGui::PopStyleColor(2);return pressed;
}
inline bool ActionButton(const char* label,ImVec4 color,ImVec2 size={}){
 ImGui::PushStyleColor(ImGuiCol_Text,{1,1,1,1});ImGui::PushStyleColor(ImGuiCol_Button,color);ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Mix(color,{1,1,1,1},.12f));ImGui::PushStyleColor(ImGuiCol_ButtonActive,Mix(color,{0,0,0,1},.16f));bool clicked=Button(label,size);ImGui::PopStyleColor(4);return clicked;
}
inline void AnythingGlyph(ImDrawList* draw,ImVec2 p,float s,ImU32 color){
 ImVec2 c{p.x+s*.5f,p.y+s*.5f};float stroke=std::max(1.f,s*.075f);
 draw->AddCircle(c,s*.3f,color,24,stroke);draw->AddCircleFilled(c,s*.075f,color);
 for(float direction:{-1.f,1.f}){draw->AddLine({c.x+direction*s*.2f,c.y},{c.x+direction*s*.5f,c.y},color,stroke);draw->AddLine({c.x,c.y+direction*s*.2f},{c.x,c.y+direction*s*.5f},color,stroke);}
}
inline void NavigationGlyph(ImDrawList* draw,int icon,ImVec2 p,float s,ImU32 color){
 auto point=[&](float x,float y){return ImVec2(p.x+x*s,p.y+y*s);};float stroke=std::max(1.f,s*.07f);
 auto line=[&](float x,float y,float a,float b){draw->AddLine(point(x,y),point(a,b),color,stroke);};
 switch(icon){
 case 0:line(.08f,.45f,.5f,.08f);line(.5f,.08f,.92f,.45f);line(.2f,.35f,.2f,.9f);line(.2f,.9f,.8f,.9f);line(.8f,.9f,.8f,.35f);line(.4f,.9f,.4f,.6f);line(.4f,.6f,.6f,.6f);line(.6f,.6f,.6f,.9f);break;
 case 1:for(int i=0;i<3;i++){float y=.14f+i*.27f;draw->AddRect(point(.1f,y),point(.78f,y+.2f),color,s*.035f,0,stroke);line(.23f,y,.23f,y+.2f);}break;
 case 2:AnythingGlyph(draw,p,s,color);break;
 case 3:for(int i=0;i<3;i++){float x=i==0?.32f:i==1?.08f:.56f,y=i==0?.06f:.51f;draw->AddRect(point(x,y),point(x+.36f,y+.36f),color,s*.025f,0,stroke);line(x,y+.1f,x+.36f,y+.1f);line(x+.18f,y,x+.18f,y+.2f);}break;
 case 4:{draw->AddCircle(point(.5f,.5f),s*.29f,color,24,stroke);draw->AddCircle(point(.5f,.5f),s*.1f,color,16,stroke);for(int i=0;i<8;i++){float a=i*.785398f;line(.5f+.29f*std::cos(a),.5f+.29f*std::sin(a),.5f+.43f*std::cos(a),.5f+.43f*std::sin(a));}break;}
 case 5:line(.03f,.53f,.23f,.53f);line(.23f,.53f,.36f,.15f);line(.36f,.15f,.53f,.87f);line(.53f,.87f,.67f,.38f);line(.67f,.38f,.77f,.53f);line(.77f,.53f,.97f,.53f);break;
 }
}
inline void Glyph(ImDrawList* draw,int icon,ImVec2 p,float s,ImU32 color,bool spinning=false){
 auto point=[&](float x,float y){return ImVec2(p.x+x*s,p.y+y*s);};auto line=[&](float x,float y,float a,float b){draw->AddLine(point(x,y),point(a,b),color,1.5f);};
 switch(icon){
 case 0:line(.12f,.45f,.5f,.12f);line(.5f,.12f,.88f,.45f);draw->AddRect(point(.22f,.42f),point(.78f,.87f),color,2,0,1.5f);line(.44f,.87f,.44f,.6f);line(.44f,.6f,.6f,.6f);break;
 case 1:draw->AddRect(point(.08f,.26f),point(.92f,.77f),color,s*.15f,0,1.5f);line(.21f,.51f,.43f,.51f);line(.32f,.4f,.32f,.62f);draw->AddCircleFilled(point(.68f,.43f),s*.05f,color);draw->AddCircleFilled(point(.8f,.57f),s*.05f,color);break;
 case 2:draw->AddRect(point(.12f,.16f),point(.88f,.84f),color,2,0,1.5f);line(.12f,.35f,.88f,.35f);line(.25f,.25f,.27f,.25f);break;
 case 3:draw->AddRect(point(.2f,.2f),point(.8f,.8f),color,3,0,1.5f);for(int i=0;i<3;i++){float x=.3f+i*.2f;line(x,.05f,x,.2f);line(x,.8f,x,.95f);line(.05f,x,.2f,x);line(.8f,x,.95f,x);}break;
 case 4:for(int i=0;i<3;i++){float y=.25f+i*.25f;line(.1f,y,.9f,y);draw->AddCircleFilled(point(i==1?.35f:.65f,y),s*.08f,color);}break;
 case 5:line(.12f,.82f,.12f,.15f);line(.12f,.82f,.92f,.82f);line(.22f,.6f,.4f,.4f);line(.4f,.4f,.62f,.55f);line(.62f,.55f,.85f,.22f);break;
 case 6:{if(spinning){float angle=!reduced?(float)ImGui::GetTime()*4.f:0.f;draw->PathArcTo(point(.5f,.5f),s*.33f,angle,angle+4.8f,22);draw->PathStroke(color,0,1.7f);}else{draw->AddCircle(point(.5f,.5f),s*.4f,color,28,1.5f);draw->AddCircle(point(.5f,.5f),s*.22f,color,22,1.f);line(.5f,.5f,.77f,.23f);draw->AddCircleFilled(point(.30f,.62f),s*.055f,color);draw->AddCircleFilled(point(.64f,.73f),s*.045f,color);}break;}
 case 9:{
  // Flame outline is font-independent and scales with the button icon.
  draw->PathLineTo(point(.55f,.04f));
  draw->PathBezierCubicCurveTo(point(.61f,.34f),point(.22f,.37f),point(.31f,.58f));
  draw->PathLineTo(point(.18f,.43f));
  draw->PathBezierCubicCurveTo(point(-.02f,.77f),point(.29f,.98f),point(.5f,.96f));
  draw->PathBezierCubicCurveTo(point(.89f,.97f),point(.98f,.54f),point(.75f,.35f));
  draw->PathLineTo(point(.7f,.54f));
  draw->PathBezierCubicCurveTo(point(.76f,.28f),point(.62f,.17f),point(.55f,.04f));
  draw->PathStroke(color,ImDrawFlags_Closed,std::max(1.5f,s*.075f));
  draw->PathLineTo(point(.51f,.56f));draw->PathBezierCubicCurveTo(point(.67f,.77f),point(.67f,.87f),point(.5f,.88f));draw->PathBezierCubicCurveTo(point(.35f,.87f),point(.35f,.76f),point(.51f,.56f));draw->PathFillConvex(color);break;}
 case 8:Neurotic::DrawRefreshIcon(draw,p,s,color,spinning,reduced);break;
 default:draw->AddRect(point(.1f,.27f),point(.9f,.82f),color,2,0,1.5f);line(.1f,.27f,.1f,.12f);line(.1f,.12f,.42f,.12f);line(.42f,.12f,.56f,.27f);break;
 }
}
inline const char* IconLabel(const char* label){while(*label==' ')++label;return label;}
inline float IconButtonWidth(const char* label,float dpi){return ImGui::CalcTextSize(IconLabel(label),nullptr,true).x+45*dpi;}
inline bool IconButton(const char* label,int icon,float width,float dpi,bool spinning=false){
 auto color=ImGui::GetColorU32(ImGuiCol_Text);ImGui::PushStyleColor(ImGuiCol_Text,{0,0,0,0});bool pressed=Button(label,{std::max(width,IconButtonWidth(label,dpi)),0});ImGui::PopStyleColor();
 auto p=ImGui::GetItemRectMin();auto size=ImGui::GetItemRectSize();auto draw=ImGui::GetWindowDrawList();Glyph(draw,icon,{p.x+10*dpi,p.y+(size.y-17*dpi)*.5f},17*dpi,color,spinning);
 auto text=IconLabel(label);auto end=strstr(text,"##");draw->AddText({p.x+35*dpi,p.y+(size.y-ImGui::GetTextLineHeight())*.5f},color,text,end);return pressed;
}
inline bool ThemeButton(bool& light,float requestedSide=0){
 float side=requestedSide>0?requestedSide:ImGui::GetFrameHeight()*1.25f;bool pressed=Button("##ThemeToggle",{side,side});if(pressed)light=!light;
 auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();ImVec2 c{(min.x+max.x)*.5f,(min.y+max.y)*.5f};auto draw=ImGui::GetWindowDrawList();float t=Animate(ImGui::GetID("##ThemeToggle"),light?1.f:0.f,23),f=side*.60f;auto text=ImGui::GetStyleColorVec4(ImGuiCol_Text);
 auto sun=text;sun.w*=1-t;auto moon=text;moon.w*=t;
 draw->AddCircle(c,f*.25f,ImGui::GetColorU32(sun),24,1.8f);
 for(int i=0;i<8;i++){float angle=i*.785398f+t*.4f;draw->AddLine({c.x+cosf(angle)*f*.38f,c.y+sinf(angle)*f*.38f},{c.x+cosf(angle)*f*.53f,c.y+sinf(angle)*f*.53f},ImGui::GetColorU32(sun),1.8f);}
 // Closed crescent geometry, so the hollow stays transparent on any surface.
 // Upright, generous crescent: aligned tips and a large open bite remain clear
 // at normal scale. Size from the button, rather than the small caption font.
 f=side*.60f;constexpr float radius=.48f,cutRadius=.4896f,cutX=.2784f,cutY=0,pi=3.14159265f;
 float distance=sqrtf(cutX*cutX+cutY*cutY),axis=atan2f(cutY,cutX),along=(radius*radius-cutRadius*cutRadius+distance*distance)/(2*distance),half=acosf(along/radius);
 float begin=axis+half,end=axis-half+2*pi;
 for(int i=0;i<=28;i++){float angle=begin+(end-begin)*i/28;draw->PathLineTo({c.x+cosf(angle)*radius*f,c.y+sinf(angle)*radius*f});}
 float innerBegin=atan2f(sinf(end)*radius-cutY,cosf(end)*radius-cutX),innerEnd=atan2f(sinf(begin)*radius-cutY,cosf(begin)*radius-cutX);while(innerEnd>innerBegin)innerEnd-=2*pi;
 // The two arc tips already belong to the outer arc. Duplicate tips can
 // prevent the concave triangulator from filling the hollow correctly.
 for(int i=1;i<24;i++){float angle=innerBegin+(innerEnd-innerBegin)*i/24;draw->PathLineTo({c.x+(cutX+cosf(angle)*cutRadius)*f,c.y+(cutY+sinf(angle)*cutRadius)*f});}draw->PathFillConcave(ImGui::GetColorU32(moon));
 if(ImGui::IsItemHovered())ImGui::SetTooltip(light?Neurotic::UiLiteral("desktop.sleekwidgets.switch_to_dark_theme_7f3a4ce3", "Switch to dark theme"):Neurotic::UiLiteral("desktop.sleekwidgets.switch_to_light_theme_2e77768d", "Switch to light theme"));return pressed;
}
inline bool CopyButton(){float side=ImGui::GetFrameHeight();bool pressed=Button("##Copy",{side,side});auto p=ImGui::GetItemRectMin();auto draw=ImGui::GetWindowDrawList();auto color=ImGui::GetColorU32(ImGuiCol_Text);draw->AddRect({p.x+side*.24f,p.y+side*.2f},{p.x+side*.64f,p.y+side*.68f},color,2,0,1.5f);draw->AddRect({p.x+side*.36f,p.y+side*.32f},{p.x+side*.76f,p.y+side*.8f},color,2,0,1.5f);if(ImGui::IsItemHovered())ImGui::SetTooltip(Neurotic::UiLiteral("desktop.sleekwidgets.copy_log_5919e0c4", "Copy log"));return pressed;}
inline bool Toggle(const char* label,bool* value){
 auto id=ImGui::GetID(label);float h=ImGui::GetFrameHeight(),w=h*1.72f,gap=ImGui::GetStyle().ItemSpacing.x;auto pos=ImGui::GetCursorScreenPos();
 ImGui::PushStyleColor(ImGuiCol_Button,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_ButtonActive,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_Text,{0,0,0,0});
 bool changed=ImGui::Button(label,{w+gap+ImGui::CalcTextSize(label).x,h});ImGui::PopStyleColor(4);if(changed){*value=!*value;ImGui::MarkItemEdited(id);}
 float t=Animate(id,*value?1.f:0.f,7);auto draw=ImGui::GetWindowDrawList();auto color=Mix(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg),ImVec4(.28f,.60f,1.f,1),t);color.w*=ImGui::GetStyle().Alpha;
 draw->AddRectFilled(pos,{pos.x+w,pos.y+h},ImGui::GetColorU32(color),h*.5f);draw->AddCircleFilled({pos.x+h*.5f+(w-h)*t,pos.y+h*.5f},h*.39f,IM_COL32(245,248,255,(int)(255*ImGui::GetStyle().Alpha)));
 draw->AddText({pos.x+w+gap,pos.y+(h-ImGui::GetFontSize())*.5f},ImGui::GetColorU32(ImGuiCol_Text),label);return changed;
}
enum class ReadyState {Checking,Ready,Attention,Error};
inline ReadyState RuntimeState(const HubModel& model){if(model.readiness.busy)return ReadyState::Checking;if(!model.preflightError.empty())return ReadyState::Attention;if(!model.preflight.is_object()||!model.preflight.contains(Neurotic::UiLiteral("desktop.hubshell.runtime_d63852ad", "runtime"))||!model.preflight[Neurotic::UiLiteral("desktop.hubshell.runtime_d63852ad", "runtime")].is_object())return ReadyState::Checking;auto& r=model.preflight[Neurotic::UiLiteral("desktop.hubshell.runtime_d63852ad", "runtime")];return r.contains("ready")&&r["ready"].is_boolean()&&r["ready"].get<bool>()?ReadyState::Ready:ReadyState::Attention;}
inline ReadyState ComponentState(const HubModel& model,const char* key){if(model.readiness.busy)return ReadyState::Checking;if(!model.preflight.is_object()||!model.preflight.contains("components")||!model.preflight["components"].is_object()||!model.preflight["components"].contains(key))return model.preflightError.empty()?ReadyState::Checking:ReadyState::Attention;auto& c=model.preflight["components"][key];return c.is_object()&&c.contains("status")&&c["status"].is_string()&&c["status"]==Neurotic::UiLiteral("desktop.settings.dlssnr/route/value.43f9b89c0b", "Present")?ReadyState::Ready:ReadyState::Attention;}
inline const char* FileStatus(ReadyState state){return state==ReadyState::Ready?Neurotic::UiLiteral("desktop.sleekwidgets.files_verified_6b1ba39e", "Files verified"):state==ReadyState::Checking?Neurotic::UiLiteral("desktop.sleekwidgets.checking_files_93d27895", "Checking files..."):state==ReadyState::Error?Neurotic::UiLiteral("desktop.sleekwidgets.files_could_not_be_verified_38f36957", "Files could not be verified"):Neurotic::UiLiteral("desktop.sleekwidgets.files_needed_4dedf07a", "Files needed");}
inline ImVec4 StatusColor(ReadyState state,bool light){return state==ReadyState::Ready?(light?ImVec4(0,.37f,.14f,1):ImVec4(.20f,.86f,.42f,1)):state==ReadyState::Attention?(light?ImVec4(.61f,.29f,0,1):ImVec4(1.f,.68f,.16f,1)):state==ReadyState::Error?(light?ImVec4(.72f,.08f,.13f,1):ImVec4(1.f,.31f,.38f,1)):ImVec4(light?.30f:.64f,light?.32f:.67f,light?.36f:.72f,1);}
inline void StatusDot(ReadyState state,bool light,float dpi){auto p=ImGui::GetCursorScreenPos();ImGui::Dummy({12*dpi,ImGui::GetTextLineHeight()});ImGui::GetWindowDrawList()->AddCircleFilled({p.x+5*dpi,p.y+ImGui::GetTextLineHeight()*.5f},4*dpi,ImGui::GetColorU32(StatusColor(state,light)));ImGui::SameLine(0,6*dpi);}
}
