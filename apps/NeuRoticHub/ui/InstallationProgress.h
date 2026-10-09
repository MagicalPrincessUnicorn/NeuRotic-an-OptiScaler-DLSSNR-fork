#pragma once
#include "InstallationMark.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace nh::installation_progress {
// Clip cached official-mark triangles against a small piecewise-linear wave.
// All geometry is fixed-capacity; drawing never decodes or triangulates an asset.
inline void DrawMark(ImDrawList* draw,ImVec2 origin,float height,float fraction,
                     bool light,bool reduced,float dpi,double time){
 using installation_mark::Point;
 const float width=height*installation_mark::Aspect;
 if(height<=0)return;
 std::array<ImVec2,installation_mark::Points.size()> screen;
 for(size_t i=0;i<screen.size();++i){auto p=installation_mark::Points[i];screen[i]={origin.x+p.x*width,origin.y+p.y*height};}
 const bool known=std::isfinite(fraction)&&fraction>=0;
 const float amount=known?std::clamp(fraction,0.f,1.f):0.f;
 const auto fluid=light?IM_COL32(0,0,0,255):IM_COL32(240,242,246,255);
 const auto oldFlags=draw->Flags;
 // Adjacent strips share edges. Avoid antialiasing those interior seams; the
 // closed official outlines provide the visible antialiased boundary.
 draw->Flags&=~ImDrawListFlags_AntiAliasedFill;
 if(amount==1){
  for(auto t:installation_mark::Triangles){ImVec2 points[]={screen[t.a],screen[t.b],screen[t.c]};draw->AddConvexPolyFilled(points,3,fluid);}
 }else if(amount>0){
  constexpr int strips=12;
  const float amplitude=reduced?0.f:std::min(.035f,std::min(amount,1-amount)*.16f);
  const float phase=float(std::fmod(time*2.2,6.283185307179586));
  auto level=[&](float x){return 1-amount+amplitude*std::sin(x*6.2831853f+phase);};
  for(auto t:installation_mark::Triangles)for(int strip=0;strip<strips;++strip){
   const float left=float(strip)/strips,right=float(strip+1)/strips;
   const float y=level(left),slope=(level(right)-y)/(right-left);
   std::array<Point,8> polygon{},next{};int count=3;
   polygon[0]=installation_mark::Points[t.a];polygon[1]=installation_mark::Points[t.b];polygon[2]=installation_mark::Points[t.c];
   auto clip=[&](auto distance){
    int written=0;if(!count)return;
    auto previous=polygon[count-1];float before=distance(previous);
    for(int i=0;i<count;++i){auto current=polygon[i];float after=distance(current);
     if((before>=0)!=(after>=0)){float p=before/(before-after);next[written++]={previous.x+(current.x-previous.x)*p,previous.y+(current.y-previous.y)*p};}
     if(after>=0)next[written++]=current;
     previous=current;before=after;
    }
    polygon=next;count=written;
   };
   clip([&](Point p){return p.x-left;});clip([&](Point p){return right-p.x;});
   clip([&](Point p){return p.y-y-slope*(p.x-left);});
   if(count>=3){ImVec2 points[8];for(int i=0;i<count;++i)points[i]={origin.x+polygon[i].x*width,origin.y+polygon[i].y*height};draw->AddConvexPolyFilled(points,count,fluid);}
  }
 }
 draw->Flags=oldFlags;
 const auto outline=light?IM_COL32(76,79,86,255):IM_COL32(160,164,172,255);
 for(auto contour:installation_mark::Outlines)draw->AddPolyline(screen.data()+contour.first,contour.count,outline,ImDrawFlags_Closed,1.75f*dpi);
}

inline void Render(ImVec2 pos,ImVec2 size,float dpi,bool light,bool reduced,
                   float progress,const std::string& stage,const std::string& detail,float reservedBottom=0){
 if(size.x<=0||size.y<=0)return;
 ImGui::SetNextWindowPos(pos);ImGui::SetNextWindowSize(size);
 ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
 ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
 ImGui::Begin("InstallationProgress",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
              ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|
              ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoBackground);
 ImGui::PopStyleVar(2);
 // Closing a modal restores page focus and may move the page above this
 // non-focusing presentation. Keep it visible without taking input or focus.
 if(!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel))
  ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
 auto draw=ImGui::GetWindowDrawList();
 draw->AddRectFilled(pos,{pos.x+size.x,pos.y+size.y},light?IM_COL32(202,205,211,224):IM_COL32(56,61,68,215),8*dpi);
 const float width=std::max(1.f,std::min(560*dpi,size.x-48*dpi));
 const auto stageSize=ImGui::CalcTextSize(stage.c_str(),nullptr,false,width);
 const auto detailSize=ImGui::CalcTextSize(detail.c_str(),nullptr,false,width);
 const float contentHeight=size.y-std::clamp(reservedBottom,0.f,size.y);
 const float markHeight=std::max(0.f,std::min({180*dpi,width/installation_mark::Aspect,
                                           contentHeight-stageSize.y-detailSize.y-56*dpi}));
 const float textGap=8*dpi,markGap=20*dpi;
 const float total=markHeight+markGap+stageSize.y+textGap+detailSize.y;
 const float top=pos.y+std::max(8*dpi,(contentHeight-total)*.5f);
 DrawMark(draw,{pos.x+(size.x-markHeight*installation_mark::Aspect)*.5f,top},markHeight,progress,light,reduced,dpi,ImGui::GetTime());
 auto centered=[&](const std::string& text,ImVec2 measured,float y){
  ImGui::SetCursorScreenPos({pos.x+(size.x-std::min(width,measured.x))*.5f,y});
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width);ImGui::TextUnformatted(text.c_str());ImGui::PopTextWrapPos();
 };
 centered(stage,stageSize,top+markHeight+markGap);
 ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
 centered(detail,detailSize,top+markHeight+markGap+stageSize.y+textGap);
 ImGui::PopStyleColor();ImGui::End();
}
}
