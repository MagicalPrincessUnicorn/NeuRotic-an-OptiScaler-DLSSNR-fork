#include <menu/Localization.h>
#pragma once
#include "SleekMotion.h"
#include "SleekWaterColors.h"
#include "Localization.h"
#include <algorithm>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace Neurotic::Sleek
{
// Presentation is opt-in. Other overlays and third-party ImGui users retain native drawing.
inline thread_local bool active = false;
inline thread_local bool reducedMotion = false;
// Used only by the compact card controls; legacy and Basic Multi Pass sliders retain their presentation.
inline thread_local bool railSlider = false;
struct Scope
{
    bool oldActive = active, oldReduced = reducedMotion;
    explicit Scope(bool reduced = false) { active = true; reducedMotion = reduced; }
    ~Scope() { active = oldActive; reducedMotion = oldReduced; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
inline bool Enabled() { return active; }
inline bool HintHovered(ImGuiHoveredFlags flags=ImGuiHoveredFlags_None)
{
    // A retained navigation ID is not a pointer hover. Keep hints on visible items.
    if(active) flags |= ImGuiHoveredFlags_NoNavOverride | ImGuiHoveredFlags_ForTooltip;
    return ImGui::IsItemHovered(flags);
}
inline float ToggleWidth() { return ImGui::GetFrameHeight() * (active ? 1.72f : 1.0f); }
inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
    const ImVec4 result{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};
    if(waterColorCapture) waterColorCapture->Blend(a,b,t,result);
    return result;
}
inline ImGuiID Key(ImGuiID id, unsigned channel, unsigned field)
{
    const unsigned salt[] = {0x534c454b,channel,field};
    return ImHashData(salt,sizeof(salt),id);
}
inline float Animate(ImGuiID id, unsigned channel, float target, float rate = 18.0f)
{
    if (!std::isfinite(target)) target = 0;
    if (!active) return target;
    auto* window = ImGui::GetCurrentWindow();
    auto& storage = window->StateStorage;
    const auto valueKey = Key(id,channel,0), frameKey = Key(id,channel,1);
    const int frame = ImGui::GetFrameCount(), previous = storage.GetInt(frameKey,-1000);
    if (reducedMotion)
    {
        storage.SetFloat(valueKey,target);
        storage.SetInt(frameKey,frame);
        return target;
    }
    float value = storage.GetFloat(valueKey,target);
    if (previous != frame)
    {
        // Hidden pages and newly created controls start from their real state.
        value = previous < frame-1 ? target : Approach(value,target,ImGui::GetIO().DeltaTime,rate);
        storage.SetFloat(valueKey,value);
        storage.SetInt(frameKey,frame);
    }
    return value;
}
inline float AnimateLinear(ImGuiID id, unsigned channel, float target)
{
    if(!std::isfinite(target)) target=0;
    if(!active) return target;
    auto& storage=ImGui::GetCurrentWindow()->StateStorage;
    const auto valueKey=Key(id,channel,0),frameKey=Key(id,channel,1);
    const auto targetKey=Key(id,channel,3),speedKey=Key(id,channel,4);
    const int frame=ImGui::GetFrameCount(),previous=storage.GetInt(frameKey,-1000);
    float value=storage.GetFloat(valueKey,target);
    if(reducedMotion || previous<frame-1)
    {
        value=target;storage.SetFloat(targetKey,target);storage.SetFloat(speedKey,0);
    }
    else if(previous!=frame)
    {
        const float oldTarget=storage.GetFloat(targetKey,value);
        float speed=storage.GetFloat(speedKey,0);
        if(oldTarget!=target)
        {
            // Retain the full-range velocity while nested content changes its
            // measured height. Tiny measurement deltas must not slow its parent.
            const float span=(std::max)(std::abs(target),std::abs(oldTarget));
            speed=(std::max)(speed,span/RevealDuration);
        }
        else if(value==target) speed=0;
        else if(speed<=0) speed=std::abs(target-value)/RevealDuration;
        storage.SetFloat(speedKey,speed);
        value=LinearStep(value,target,ImGui::GetIO().DeltaTime,speed);
        storage.SetFloat(targetKey,target);
    }
    storage.SetFloat(valueKey,value);storage.SetInt(frameKey,frame);
    return value;
}
inline ImU32 ControlColor(ImGuiID id, bool hovered, bool held,
                         ImGuiCol normal, ImGuiCol hover, ImGuiCol pressed)
{
    if (!active) return ImGui::GetColorU32(held && hovered ? pressed : hovered ? hover : normal);
    const float h = Animate(id,0,hovered ? 1.0f : 0.0f,22);
    const float p = Animate(id,1,held && hovered ? 1.0f : 0.0f,28);
    return ImGui::GetColorU32(Mix(Mix(ImGui::GetStyleColorVec4(normal),
        ImGui::GetStyleColorVec4(hover),h),ImGui::GetStyleColorVec4(pressed),p));
}
inline void DrawSwitch(ImDrawList* draw, const ImRect& bounds, ImGuiID id,
                       bool checked, bool mixed, bool hovered, bool held)
{
    const float t = Animate(id,2,checked ? 1.0f : 0.0f,22);
    const float h = bounds.GetHeight(), trackH = h*0.78f;
    const ImVec2 a(bounds.Min.x,bounds.GetCenter().y-trackH*0.5f);
    const ImVec2 b(bounds.Max.x,bounds.GetCenter().y+trackH*0.5f);
    const float hover = Animate(id,5,hovered ? 1.0f : 0.0f,22);
    const ImVec4 neutral = Mix(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg),ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered),hover);
    draw->AddRectFilled(a,b,ImGui::GetColorU32(Mix(neutral,ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),t)),trackH*0.5f);
    draw->AddRect(a,b,ImGui::GetColorU32(ImGuiCol_Border),trackH*0.5f);
    const float radius = trackH*0.5f-2.5f*(h/28.0f);
    const float press = Animate(id,3,held && hovered ? 1.0f : 0.0f,28);
    const ImVec2 center(a.x+trackH*0.5f+(b.x-a.x-trackH)*t,bounds.GetCenter().y);
    draw->AddCircleFilled({center.x,center.y+1.5f},radius,IM_COL32(0,0,0,35));
    draw->AddCircleFilled(center,radius+press*(h*0.025f),ImGui::GetColorU32(ImVec4(0.98f,0.99f,1.0f,1)));
    if (mixed)
        draw->AddLine({center.x-radius*.4f,center.y},{center.x+radius*.4f,center.y},ImGui::GetColorU32(ImGuiCol_FrameBgActive),2);
}
inline void Chevron(ImDrawList* draw, ImVec2 center, float size, ImU32 color, float turn)
{
    const float angle = turn*1.570796327f;
    const float c = std::cos(angle), s = std::sin(angle);
    auto rotate = [&](float x,float y) { return ImVec2(center.x+x*c-y*s,center.y+x*s+y*c); };
    draw->AddLine(rotate(-size*.25f,-size*.35f),rotate(size*.15f,0),color,1.6f);
    draw->AddLine(rotate(size*.15f,0),rotate(-size*.25f,size*.35f),color,1.6f);
}
inline void ButtonFeedback(ImDrawList* draw, const ImRect& bounds, ImGuiID id, bool pressed, float rounding)
{
    if (!active || reducedMotion) return;
    auto& storage = ImGui::GetCurrentWindow()->StateStorage;
    const auto key = Key(id,4,0);
    if (pressed) storage.SetFloat(key,static_cast<float>(ImGui::GetTime()));
    const float age = static_cast<float>(ImGui::GetTime())-storage.GetFloat(key,-10);
    if (age < 0 || age > .32f) return;
    const float t = age/.32f;
    ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    color.w *= (1-t)*.4f;
    draw->AddRect(bounds.Min,bounds.Max,ImGui::GetColorU32(color),rounding,0,1.0f+(1-t)*1.5f);
}
inline bool ThemeButton(bool& light, bool anchored = true)
{
    const auto saved=ImGui::GetCursorScreenPos();
    const float f=ImGui::GetFontSize(), side=ImGui::GetFrameHeight()*1.25f;
    if(anchored) ImGui::SetCursorScreenPos({saved.x+ImGui::GetContentRegionAvail().x-side,saved.y});
    const bool clicked=ImGui::Button("##ThemeToggle",{side,side});
    if(clicked) light=!light;
    const auto id=ImGui::GetItemID();
    const auto bounds=ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
    const auto c=bounds.GetCenter();
    const float t=Animate(id,23,light?1.0f:0.0f,20);
    auto* draw=ImGui::GetWindowDrawList();
    auto sun=ImGui::GetStyleColorVec4(ImGuiCol_Text); sun.w*=1-t;
    auto moon=sun; moon.w=ImGui::GetStyleColorVec4(ImGuiCol_Text).w*t;
    const auto sunInk=ImGui::GetColorU32(sun),moonInk=ImGui::GetColorU32(moon);
    draw->AddCircle(c,f*.25f,sunInk,24,1.6f);
    for(int i=0;i<8;++i) {
        const float angle=i*.78539816f+t*.4f;
        draw->AddLine({c.x+std::cos(angle)*f*.38f,c.y+std::sin(angle)*f*.38f},
                      {c.x+std::cos(angle)*f*.53f,c.y+std::sin(angle)*f*.53f},sunInk,1.6f);
    }
    // Two connected cubic arcs form a crescent without painting over the background.
    draw->PathLineTo({c.x+f*.14f,c.y-f*.48f});
    draw->PathBezierCubicCurveTo({c.x-f*.55f,c.y-f*.40f},{c.x-f*.54f,c.y+f*.49f},{c.x+f*.15f,c.y+f*.48f});
    draw->PathBezierCubicCurveTo({c.x-f*.20f,c.y+f*.20f},{c.x-f*.20f,c.y-f*.22f},{c.x+f*.14f,c.y-f*.48f});
    draw->PathStroke(moonInk,ImDrawFlags_Closed,1.8f);
    ImGui::SetCursorScreenPos(saved);
    return clicked;
}
inline void CompleteButtonFeedback(ImGuiID id, bool success)
{
    auto& state=ImGui::GetCurrentWindow()->StateStorage;
    state.SetFloat(Key(id,24,0),static_cast<float>(ImGui::GetTime()));
    state.SetInt(Key(id,24,1),success?1:0);
}
inline void BeginButtonFeedback(ImGuiID id)
{
    CompleteButtonFeedback(id,false);
    ImGui::GetCurrentWindow()->StateStorage.SetInt(Key(id,24,1),2);
}
inline void CancelButtonFeedback(ImGuiID id)
{
    ImGui::GetCurrentWindow()->StateStorage.SetFloat(Key(id,24,0),-10);
    ImGui::GetCurrentWindow()->StateStorage.SetInt(Key(id,24,1),0);
}
inline bool ButtonFeedbackPending(ImGuiID id)
{
    return ImGui::GetCurrentWindow()->StateStorage.GetInt(Key(id,24,1),0)==2;
}
enum class FeedbackKind { Copy, Save };
inline bool FeedbackButton(const char* label,FeedbackKind kind=FeedbackKind::Copy,float height=0,float width=0)
{
    const float f=ImGui::GetFontSize();
    const auto textInk=ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0,0,0,0));
    const bool clicked=ImGui::Button(label,{width>0?width:ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2+f*2,height});
    ImGui::PopStyleColor();
    const auto id=ImGui::GetItemID();
    const auto& state=ImGui::GetCurrentWindow()->StateStorage;
    const float age=static_cast<float>(ImGui::GetTime())-state.GetFloat(Key(id,24,0),-10);
    auto* draw=ImGui::GetWindowDrawList();
    const auto left=ImGui::GetItemRectMin();
    const auto right=ImGui::GetItemRectMax();
    const ImVec2 c(left.x+ImGui::GetStyle().FramePadding.x+f*.5f,(left.y+right.y)*.5f);
    const auto caption=Neurotic::Translate(label);
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::ColorConvertU32ToFloat4(textInk));
    const float captionRight=right.x-ImGui::GetStyle().FramePadding.x;
    const ImVec2 captionOrigin(c.x+f,c.y-f*.5f);
    if(captionRight>captionOrigin.x+f)
        ImGui::RenderTextEllipsis(draw,captionOrigin,{captionRight,captionOrigin.y+f},captionRight,caption.c_str(),nullptr,nullptr);
    ImGui::PopStyleColor();
    const int phase=state.GetInt(Key(id,24,1),0);
    const bool feedback=age>=0 && (phase==2 || age<1.8f);
    if(feedback && (phase==2 || (kind==FeedbackKind::Save && age<.20f && !reducedMotion))) {
        const float turn=reducedMotion?0:static_cast<float>(ImGui::GetTime())*6;
        draw->PathArcTo(c,f*.35f,turn,turn+4.6f,16);
        draw->PathStroke(ImGui::GetColorU32(ImGuiCol_CheckMark),0,1.8f);
    } else if(feedback) {
        const bool success=state.GetInt(Key(id,24,1))!=0;
        const auto ink=ImGui::GetColorU32(success?ImVec4(.30f,.82f,.48f,1):ImVec4(.95f,.38f,.34f,1));
        if(success) {
            draw->AddLine({c.x-f*.30f,c.y},{c.x-f*.06f,c.y+f*.24f},ink,2);
            draw->AddLine({c.x-f*.06f,c.y+f*.24f},{c.x+f*.35f,c.y-f*.26f},ink,2);
        } else {
            draw->AddLine({c.x-f*.25f,c.y-f*.25f},{c.x+f*.25f,c.y+f*.25f},ink,2);
            draw->AddLine({c.x-f*.25f,c.y+f*.25f},{c.x+f*.25f,c.y-f*.25f},ink,2);
        }
    } else {
        const auto ink=ImGui::GetColorU32(ImGuiCol_TextDisabled);
        if(kind==FeedbackKind::Copy) {
            draw->AddRect({c.x-f*.3f,c.y-f*.2f},{c.x+f*.15f,c.y+f*.3f},ink,2,0,1.4f);
            draw->AddRect({c.x-f*.12f,c.y-f*.38f},{c.x+f*.33f,c.y+f*.12f},ink,2,0,1.4f);
        } else {
            draw->AddRect({c.x-f*.35f,c.y-f*.35f},{c.x+f*.35f,c.y+f*.35f},ink,2,0,1.4f);
            draw->AddRect({c.x-f*.18f,c.y-f*.35f},{c.x+f*.18f,c.y-f*.06f},ink,0,0,1.2f);
            draw->AddRect({c.x-f*.20f,c.y+f*.08f},{c.x+f*.20f,c.y+f*.35f},ink,0,0,1.2f);
        }
    }
    return clicked;
}
inline bool SupportButton()
{
    const bool clicked=ImGui::Button(Neurotic::UiLiteral("ingame.sleekshell.send_ko_fi_183e38d3", "Send Ko-fi"));
    auto* window=ImGui::GetCurrentWindow();
    const auto key=Key(ImGui::GetItemID(),30,0);
    if(clicked) window->StateStorage.SetFloat(key,static_cast<float>(ImGui::GetTime()));
    const float age=static_cast<float>(ImGui::GetTime())-window->StateStorage.GetFloat(key,-10);
    const float duration=reducedMotion?.35f:1.3f;
    if(age>=0 && age<duration) {
        const float t=age/duration,f=ImGui::GetFontSize();
        ImVec2 centre=(ImGui::GetItemRectMin()+ImGui::GetItemRectMax())*.5f;
        if(!reducedMotion) { centre.x+=std::sin(t*24)*f*.55f*(1-t);centre.y-=f*4*(1-(1-t)*(1-t)); }
        const float tilt=reducedMotion?0:std::sin(t*17)*.25f;
        auto* draw=ImGui::GetForegroundDrawList(window->Viewport);
        draw->PushClipRect(window->Pos,window->Pos+window->Size,true);
        for(int point=0;point<40;++point) {
            const float a=point*6.2831853f/40;
            const float sine=std::sin(a);
            const float x=16*sine*sine*sine,y=-(13*std::cos(a)-5*std::cos(2*a)-2*std::cos(3*a)-std::cos(4*a));
            const float size=f*.045f*(.8f+.2f*std::sin(t*3.14159f));
            draw->PathLineTo({centre.x+(x*std::cos(tilt)-y*std::sin(tilt))*size,
                centre.y+(x*std::sin(tilt)+y*std::cos(tilt))*size});
        }
        draw->PathFillConcave(ImGui::GetColorU32(ImVec4(.96f,.18f,.26f,(1-t)*(1-t))));
        draw->PopClipRect();
    }
    return clicked;
}
// Closed dropdowns repaint their short visual tail as disabled, input-transparent decoration.
// Native popup ownership/focus closes immediately; no saved draw commands or GPU resources.
inline void NotePopupClose(ImGuiWindow* window)
{
    if(!window || window->Flags & ImGuiWindowFlags_Modal) return;
    auto& state=window->StateStorage;
    if(state.GetInt(Key(window->ID,25,0),-1000)>=ImGui::GetFrameCount()-1)
        state.SetFloat(Key(window->ID,25,2),static_cast<float>(ImGui::GetTime()));
}
inline bool ClosingPopup(ImGuiWindow* window)
{
    return window && window->StateStorage.GetInt(Key(window->ID,25,3),-1)==ImGui::GetFrameCount();
}
inline bool BeginClosingPopup(const char* name, ImGuiID popup)
{
    if(!active || reducedMotion) return false;
    auto* window=ImGui::FindWindowByName(name);
    if(!window || window->PopupId!=popup || window->Flags & ImGuiWindowFlags_Modal) return false;
    const float age=static_cast<float>(ImGui::GetTime())-window->StateStorage.GetFloat(Key(window->ID,25,2),-10);
    if(age<0 || age>RevealDuration) return false;
    const auto position=window->Pos, size=window->Size;
    const float initial=window->StateStorage.GetFloat(Key(window->ID,26,0),1);
    const float closed=window->StateStorage.GetFloat(Key(window->ID,25,2),-10);
    char decoration[48];
    ImFormatString(decoration,sizeof(decoration),"##SleekClosing_%08x_%08x",window->ID,popup);
    ImGui::SetNextWindowPos(position); ImGui::SetNextWindowSize(size);
    ImGui::Begin(decoration,nullptr,ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing);
    window=ImGui::GetCurrentWindow();
    // This decoration inherits an already measured popup rectangle. ImGui hides
    // newly appearing tooltips for a sizing frame; that would blink the closed
    // popup off before its tail reappears. Keep other visibility suppressions.
    window->HiddenFramesCannotSkipItems=0;
    window->Hidden=window->HiddenFramesCanSkipItems>0 || window->HiddenFramesForRenderOnly>0;
    if(window->StateStorage.GetFloat(Key(window->ID,25,2),-10)!=closed) {
        window->StateStorage.SetFloat(Key(window->ID,25,2),closed);
        window->StateStorage.SetFloat(Key(window->ID,26,0),initial);
        window->StateStorage.SetInt(Key(window->ID,26,1),ImGui::GetFrameCount()-1);
        window->StateStorage.SetFloat(Key(window->ID,26,3),1);
    }
    window->StateStorage.SetInt(Key(window->ID,25,3),ImGui::GetFrameCount());
    ImGui::BeginDisabled();
    return true;
}
inline float PopupProgress(ImGuiWindow* window)
{
    if(!active || window->Flags & ImGuiWindowFlags_Modal) return 1;
    const bool closing=ClosingPopup(window);
    auto& state=window->StateStorage;
    const int frame=ImGui::GetFrameCount();
    if(!closing) {
        if(window->Appearing || state.GetInt(Key(window->ID,25,0),-1000)<frame-1) {
            state.SetFloat(Key(window->ID,26,0),0);
            state.SetInt(Key(window->ID,26,1),frame-1);
        }
        state.SetInt(Key(window->ID,25,0),frame);
        // CloseCurrentPopup may already have requested a tail during this very frame.
    }
    if(!closing && (reducedMotion || (GImGui->NavCursorVisible && GImGui->NavInputSource==ImGuiInputSource_Keyboard)))
        state.SetFloat(Key(window->ID,26,0),1);
    return AnimateLinear(window->ID,26,closing?0.0f:1.0f);
}
inline void PreparePopupPresentation(ImGuiWindow* window)
{
    if(!active || window->Flags & ImGuiWindowFlags_Modal) return;
    window->StateStorage.SetFloat(Key(window->ID,25,2),-10);
    const float t=PopupProgress(window);
    window->ClipRect.Max.y=(std::min)(window->ClipRect.Max.y,window->Pos.y+window->Size.y*(.10f+.90f*t));
    window->DrawList->PopClipRect();
    window->DrawList->PushClipRect(window->ClipRect.Min,window->ClipRect.Max,true);
}
inline void PopupPresentation(ImGuiWindow* window)
{
    if(!active || window->Flags & ImGuiWindowFlags_Modal) return;
    const bool closing=ClosingPopup(window);
    const float t=PopupProgress(window);
    const float bottom=window->Pos.y+window->Size.y*(.10f+.90f*t);
    auto* draw=window->DrawList;
    for(auto& command:draw->CmdBuffer) command.ClipRect.w=(std::min)(command.ClipRect.w,bottom);
    const float alpha=closing?t:(.15f+.85f*t);
    for(auto& vertex:draw->VtxBuffer) {
        const auto a=static_cast<unsigned>((vertex.col>>IM_COL32_A_SHIFT)&255);
        vertex.col=(vertex.col&~IM_COL32_A_MASK)|(static_cast<unsigned>(a*alpha)<<IM_COL32_A_SHIFT);
    }
}
}
