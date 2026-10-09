#pragma once
#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
namespace Neurotic {
// The desktop per-game refresh mark, shared by every refresh button. Its caller
// owns the existing hit target; this function only fits art inside its square.
inline void DrawRefreshIcon(ImDrawList* draw,ImVec2 p,float side,ImU32 color,bool busy,bool reducedMotion=false){
 const auto point=[&](float x,float y){return ImVec2(p.x+x*side,p.y+y*side);};
 const float rotation=busy&&!reducedMotion?float(ImGui::GetTime())*3.f:0.f;
 for(int arrow=0;arrow<2;++arrow){
  const float begin=rotation+arrow*3.141593f-.523599f,end=begin+2.443461f;
  draw->PathArcTo(point(.5f,.5f),side*.32f,begin,end,20);draw->PathStroke(color,0,(std::max)(1.5f,side*.085f));
  const ImVec2 edge=point(.5f+.32f*std::cos(end),.5f+.32f*std::sin(end));
  const float tx=-std::sin(end),ty=std::cos(end),nx=std::cos(end),ny=std::sin(end);
  draw->AddTriangleFilled({edge.x+tx*side*.13f,edge.y+ty*side*.13f},{edge.x-tx*side*.07f+nx*side*.13f,edge.y-ty*side*.07f+ny*side*.13f},{edge.x-tx*side*.07f-nx*side*.13f,edge.y-ty*side*.07f-ny*side*.13f},color);
 }
}
}
