#pragma once
#include "InstallCompletionState.h"
#include "ActionFont.h"
#include <menu/Localization.h>
#include "imgui_internal.h"
#include <algorithm>
namespace nh::ui {
inline void CompletionCenteredText(const char* text,float width){
 const float left=ImGui::GetCursorPosX();const auto measured=ImGui::CalcTextSize(text,nullptr,false,width);
 ImGui::SetCursorPosX(left+(std::max)(0.f,(width-measured.x)*.5f));
 ImGui::PushTextWrapPos(left+width);ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();
}
inline void InstallationCompletionDialog(InstallCompletionState& state,bool light,float dpi,bool blocked=false){
 constexpr const char* popup="##InstallationComplete";
 const bool opened=ImGui::IsPopupOpen(popup);
 if(!opened){
  if(!state.Pending()||blocked||ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel))return;
  ImGui::OpenPopup(popup);
 }
 const auto* viewport=ImGui::GetMainViewport();const auto room=viewport->WorkSize;
 ImGui::SetNextWindowPos(viewport->GetWorkCenter(),ImGuiCond_Always,{.5f,.5f});
 ImGui::SetNextWindowSize({(std::max)(1.f,(std::min)(540.f*dpi,room.x-32.f*dpi)),0},ImGuiCond_Always);
 ImGui::SetNextWindowSizeConstraints({0,0},{(std::max)(1.f,room.x-32.f*dpi),(std::max)(1.f,room.y-32.f*dpi)});
 if(!ImGui::BeginPopupModal(popup,nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize))return;
 const auto& notices=state.PendingItems();if(notices.empty()){ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
 const auto serial=notices.back().serial;
 auto* window=ImGui::GetCurrentWindow();auto* storage=ImGui::GetStateStorage();
 const auto lowKey=ImGui::GetID("##NoticeSerialLow"),middleKey=ImGui::GetID("##NoticeSerialMiddle"),highKey=ImGui::GetID("##NoticeSerialHigh");
 const auto visibleFrameKey=ImGui::GetID("##NoticeVisibleFrame"),outsidePressKey=ImGui::GetID("##NoticeOutsidePress");
 // Positive 31/31/2-bit pieces preserve the full immutable serial in ImGui's
 // integer storage without truncation or signed integer conversion assumptions.
 const int low=int(serial&0x7fffffff),middle=int((serial>>31)&0x7fffffff),high=int(serial>>62);
 const bool sameNotice=storage->GetInt(lowKey,-1)==low&&storage->GetInt(middleKey,-1)==middle&&storage->GetInt(highKey,-1)==high;
 const int frame=ImGui::GetFrameCount();const bool visibleNow=!window->Hidden&&!ImGui::GetIO().AppFocusLost;
 // An Escape/Enter/click intended for the preceding page or modal cannot
 // acknowledge a notice that has not yet had a visible foreground frame.
 const bool previouslyVisible=sameNotice&&visibleNow&&storage->GetInt(visibleFrameKey,-1)==frame-1;
 storage->SetInt(lowKey,low);storage->SetInt(middleKey,middle);storage->SetInt(highKey,high);storage->SetInt(visibleFrameKey,visibleNow?frame:-1);
 if(!previouslyVisible)storage->SetBool(outsidePressKey,false);
 const bool validMouse=ImGui::IsMousePosValid();const bool inside=validMouse&&window->Rect().Contains(ImGui::GetIO().MousePos);
 if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))storage->SetBool(outsidePressKey,previouslyVisible&&validMouse&&!inside);
 const bool outsideClick=previouslyVisible&&storage->GetBool(outsidePressKey)&&ImGui::IsMouseReleased(ImGuiMouseButton_Left)&&validMouse&&!inside;
 if(!visibleNow||ImGui::IsMouseReleased(ImGuiMouseButton_Left))storage->SetBool(outsidePressKey,false);
 const float font=ImGui::GetFontSize(),width=ImGui::GetContentRegionAvail().x;
 const auto at=ImGui::GetCursorScreenPos();const float radius=font*1.8f;
 const ImVec2 center{at.x+width*.5f,at.y+radius+font*.5f};
 const auto green=ImGui::GetColorU32(light?ImVec4(0,.37f,.14f,1):ImVec4(.20f,.86f,.42f,1));
 auto* draw=ImGui::GetWindowDrawList();draw->AddCircle(center,radius,green,0,2.5f*dpi);
 draw->AddLine({center.x-radius*.48f,center.y},{center.x-radius*.10f,center.y+radius*.34f},green,3.f*dpi);
 draw->AddLine({center.x-radius*.10f,center.y+radius*.34f},{center.x+radius*.52f,center.y-radius*.38f},green,3.f*dpi);
 ImGui::Dummy({width,radius*2+font});
 ImGui::PushFont(ActionFont(),24);
 CompletionCenteredText(notices.size()>1?Neurotic::UiLiteral("desktop.installation.batch_complete","Installations complete"):Neurotic::UiLiteral("desktop.installation.complete","Installation Complete"),width);
 ImGui::PopFont();ImGui::Spacing();
 float namesHeight=0;for(const auto& notice:notices)namesHeight+=ImGui::CalcTextSize(notice.title.c_str(),nullptr,false,width).y+ImGui::GetStyle().ItemSpacing.y;
 const float maxNameHeight=std::max(font,std::min(font*7.f,room.y*.28f));
 if(notices.size()>1||namesHeight>maxNameHeight){
  if(ImGui::BeginChild("##CompletedGameNames",{width,std::min(namesHeight,maxNameHeight)},ImGuiChildFlags_None,ImGuiWindowFlags_NoSavedSettings)){
   ImGui::PushTextWrapPos(0);for(const auto& notice:notices)ImGui::TextUnformatted(notice.title.c_str());ImGui::PopTextWrapPos();
  }ImGui::EndChild();
 }else CompletionCenteredText(notices.front().title.c_str(),width);
 ImGui::Spacing();
 const auto* done=Neurotic::UiLiteral("desktop.anythingview.done_3598b18e","Done");
 const float buttonWidth=(std::min)(width,(std::max)(font*7,ImGui::CalcTextSize(done).x+ImGui::GetStyle().FramePadding.x*2));
 ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(width-buttonWidth)*.5f);
 const bool donePressed=ImGui::Button(done,{buttonWidth,0});
 // Dismiss only on release after an outside press on this visible notice. The
 // modal blocks underlying controls through this release frame; the gesture
 // cannot activate an install/uninstall/navigation action behind the dialog.
 const bool acknowledged=previouslyVisible&&(donePressed||ImGui::IsKeyPressed(ImGuiKey_Escape,false)||outsideClick);
 ImGui::SetItemDefaultFocus();
 if(acknowledged){state.AcknowledgeThrough(serial);ImGui::CloseCurrentPopup();}
 ImGui::EndPopup();
}
}
