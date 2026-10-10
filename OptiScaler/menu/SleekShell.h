#include <menu/Localization.h>
#pragma once
#include <dlssnr/ObservationRefreshButton.h>
#include "SleekUi.h"
#include "SleekBrand.h"
#include "SleekPilotLight.h"
#include "Localization.h"
#include "LanguagePicker.h"
#include "localization/LanguageRuntime.h"
#include "localization/FontNoticesView.h"
#include "WindowSectionHeader.h"
#include <algorithm>

namespace Neurotic::Sleek
{
inline const char* Pages[] = {Neurotic::UiLiteral("ingame.option.c910d474dcd7", "General"), Neurotic::UiLiteral("ingame.option.d827fea4f9a0", "Upscaling"), Neurotic::UiLiteral("ingame.menu-common.neural_rendering_5cde3731", "Neural Rendering"),
    Neurotic::UiLiteral("ingame.dlssnr-menu.frame_generation_c41396f4", "Frame Generation"), Neurotic::UiLiteral("ingame.objectruleeditor.advanced_cfbc9ae1", "Advanced"), Neurotic::UiLiteral("ingame.option.ea93d6a262ec", "Tools"), Neurotic::UiLiteral("ingame.option.268f14bbfe11", "Diagnostics")};

inline constexpr int PageCount = sizeof(Pages)/sizeof(Pages[0]);
inline constexpr ImGuiWindowFlags FixedShellFlags = ImGuiWindowFlags_NoScrollbar |
    ImGuiWindowFlags_NoScrollWithMouse;

inline void ApplyMetrics(ImGuiStyle& style)
{
    style.WindowRounding = 16;
    style.ChildRounding = 12;
    style.FrameRounding = 8;
    style.PopupRounding = 12;
    style.TabRounding = 7;
    style.ScrollbarRounding = 12;
    style.GrabRounding = 12;
    style.WindowPadding = {16,16};
    style.FramePadding = {12,6};
    style.ItemSpacing = {8,8};
    style.CellPadding = {8,7};
    style.WindowTitleAlign = {0,.5f};
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.FrameBorderSize = 1;
    style.PopupBorderSize = 1;
    style.ScrollbarSize = 9;
    style.GrabMinSize = 12;
}

// Original line glyphs, drawn at the current font size; no icon font or texture lifetime.
inline void Icon(ImDrawList* draw, int page, ImVec2 origin, float size, ImU32 color)
{
    auto p = [&](float x,float y) { return ImVec2(origin.x+x*size,origin.y+y*size); };
    auto line = [&](float x,float y,float a,float b) { draw->AddLine(p(x,y),p(a,b),color,1.5f); };
    switch (page)
    {
    case 0:
        draw->AddRect(p(.12f,.16f),p(.88f,.84f),color,size*.15f,0,1.5f);
        line(.35f,.16f,.35f,.84f); line(.5f,.4f,.72f,.4f); line(.5f,.6f,.65f,.6f); break;
    case 1:
        for (int i=0;i<3;++i) for (int j=0;j<2;++j)
            draw->AddCircle(p(.2f+i*.3f,.3f+j*.4f),size*.09f,color,12,1.5f);
        line(.28f,.3f,.42f,.7f); line(.28f,.7f,.42f,.3f);
        line(.58f,.3f,.72f,.7f); line(.58f,.7f,.72f,.3f); break;
    case 2:
        draw->AddRect(p(.12f,.46f),p(.54f,.88f),color,2,0,1.5f);
        line(.46f,.12f,.88f,.12f); line(.88f,.12f,.88f,.54f);
        line(.45f,.55f,.88f,.12f); break;
    case 3:
        draw->AddRect(p(.12f,.3f),p(.72f,.82f),color,2,0,1.5f);
        line(.3f,.16f,.88f,.16f); line(.88f,.16f,.88f,.65f);
        line(.35f,.45f,.52f,.55f); line(.52f,.55f,.35f,.65f); break;
    case 4:
        for (int i=0;i<3;++i) { float y=.25f+i*.25f; line(.12f,y,.88f,y);
            draw->AddCircleFilled(p(i==1?.35f:.65f,y),size*.08f,color); } break;
    case 5:
        // An open jaw, narrow shaft and rounded handle share the other glyphs' stroke.
        draw->PathLineTo(p(.64f,.10f)); draw->PathLineTo(p(.51f,.15f));
        draw->PathLineTo(p(.43f,.29f)); draw->PathLineTo(p(.46f,.43f));
        draw->PathLineTo(p(.14f,.75f));
        draw->PathBezierCubicCurveTo(p(.03f,.87f),p(.20f,.99f),p(.30f,.87f));
        draw->PathLineTo(p(.62f,.55f)); draw->PathLineTo(p(.76f,.57f));
        draw->PathLineTo(p(.89f,.48f)); draw->PathLineTo(p(.94f,.34f));
        draw->PathLineTo(p(.75f,.41f)); draw->PathLineTo(p(.60f,.27f));
        draw->PathLineTo(p(.64f,.10f)); draw->PathStroke(color,0,1.5f); break;
    default:
        line(.1f,.75f,.1f,.2f); line(.1f,.75f,.9f,.75f);
        line(.18f,.6f,.35f,.42f); line(.35f,.42f,.52f,.56f); line(.52f,.56f,.8f,.24f); break;
    }
}
inline void Brand(const char* context = nullptr, float reserved = 0)
{
    const float f = ImGui::GetFontSize();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float edge=pos.x+ImGui::GetContentRegionAvail().x-reserved;
    Neurotic::Brand::Draw(ImGui::GetWindowDrawList(),pos,f*2.0f,(std::max)(0.0f,edge-pos.x),
        edge-pos.x<f*6,ImGui::GetColorU32(ImGuiCol_Text),ImGui::GetColorU32(ImVec4(1,1,1,1)));
    if(edge-pos.x<f*6) {
        // Compact viewports use the mark from the same official lockup.
        ImGui::Dummy({f*2,ImGui::GetFrameHeight()*1.25f});ImGui::Spacing();return;
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+f*2.6f);
    ImGui::PushFont(nullptr,f*1.65f);
    const auto titleOrigin=ImGui::GetCursorScreenPos();
    const auto titleSize=ImVec2((std::max)(1.0f,edge-titleOrigin.x),ImGui::GetFontSize());
    // Keep the existing header/subtitle geometry while the lockup supplies the title.
    ImGui::Dummy(titleSize);
    ImGui::PopFont();
    if (context && *context)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+f*2.6f);
        const auto origin=ImGui::GetCursorScreenPos();
        const ImVec2 size((std::max)(1.0f,edge-origin.x),f);
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),origin,origin+size,edge,context,nullptr,nullptr);
        ImGui::PopStyleColor();ImGui::Dummy(size);
    }
    ImGui::Spacing();
}
inline bool WideNavigation(float available, float scale)
{
    // The rail reserves enough space for the longest translated page name.
    float caption = 0;
    for (auto* page : Pages) caption = (std::max)(caption,ImGui::CalcTextSize(page).x);
    return available > caption + ImGui::GetFontSize()*4 + 510*scale;
}
inline ImVec2 WindowSize(ImVec2 viewport, float scale)
{
    return {(std::min)(940*scale,(std::max)(32.0f,viewport.x-24)),
            (std::min)(878*scale,(std::max)(32.0f,viewport.y-24))};
}
inline ImVec2 WindowPosition(ImVec2 current, ImVec2 origin, ImVec2 viewport, ImVec2 size)
{
    return {ImClamp(current.x,origin.x+12,(std::max)(origin.x+12,origin.x+viewport.x-size.x-12)),
            ImClamp(current.y,origin.y+12,(std::max)(origin.y+12,origin.y+viewport.y-size.y-12))};
}
struct NavigationStatus { bool running = false; bool issue = false; PilotState pilot = PilotState::Degraded; };
constexpr NavigationStatus UpscalingNavigationStatus(bool feature, bool initialized, bool frozen, bool librariesAvailable = true)
{
    const bool ready=feature && initialized && !frozen;
    return {ready,!ready,ready?PilotState::On:(!feature || !initialized)&&!librariesAvailable?PilotState::Blocked:
        feature&&!initialized&&!frozen?PilotState::Blocked:PilotState::Degraded};
}
constexpr NavigationStatus FrameGenerationNavigationStatus(bool requested, bool nativeActive, bool managedActive, bool paused, bool waitingForInputs = false)
{
    const bool active = nativeActive || (managedActive && !paused && !waitingForInputs);
    const bool waiting = requested || paused || waitingForInputs;
    return {active, !active && waiting, active ? PilotState::On : waiting ? PilotState::Degraded : PilotState::Off};
}
inline void Navigation(int& selected, float height, bool wide,
                       const NavigationStatus* status = nullptr)
{
    const float f=ImGui::GetFontSize();const auto& style=ImGui::GetStyle();
    float width=0,longestWord=0;
    for(auto* page:Pages){width=(std::max)(width,ImGui::CalcTextSize(page).x);const auto caption=Neurotic::Translate(page);size_t start=0;while(start<caption.size()){const auto end=caption.find(' ',start);const auto word=caption.substr(start,end==std::string::npos?end:end-start);longestWord=(std::max)(longestWord,ImGui::CalcTextSize(word.c_str()).x);if(end==std::string::npos)break;start=end+1;}}
    width+=f*3.7f;const float available=(std::max)(1.f,ImGui::GetContentRegionAvail().x);
    const float equalWidth=(std::max)(1.f,(available-style.ItemSpacing.x*(PageCount-1))/PageCount);
    const float minimumCell=(std::min)(available,(std::max)(f*3.f,longestWord+f*2.1f));
    const bool strip=!wide&&equalWidth<minimumCell;
    const float cellWidth=strip?minimumCell:equalWidth;
    const float captionRoom=(std::max)(1.f,cellWidth-f*2.1f),captionFont=f;
    float cellHeight=f*2.25f;
    if(!wide)for(auto* page:Pages){const auto caption=Neurotic::Translate(page);cellHeight=(std::max)(cellHeight,(std::min)(2*f,ImGui::CalcTextSize(caption.c_str(),nullptr,false,captionRoom).y)+f*.7f);}
    auto* parent=ImGui::GetCurrentWindow();const auto identity=parent->IDStack.back();
    const auto stripId=ImGui::GetID("##NavigationStrip");
    const auto selectedKey=ImGui::GetID("##NavigationStripSelection"),widthKey=ImGui::GetID("##NavigationStripWidth");
    const bool reveal=parent->StateStorage.GetInt(selectedKey,-1)!=selected||std::abs(parent->StateStorage.GetFloat(widthKey,-1)-available)>.5f;
    ImGui::PushFont(nullptr,captionFont);ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(0,0,0,0));
    if(wide)ImGui::BeginChild("##NavigationRail",{width,height},ImGuiChildFlags_None);
    else if(strip){ImGui::BeginChild("##NavigationStrip",{0,cellHeight+style.ScrollbarSize+2*style.WindowPadding.y},ImGuiChildFlags_NavFlattened,ImGuiWindowFlags_HorizontalScrollbar);ImGui::PushOverrideID(identity);}
    else ImGui::BeginGroup();
    for(int i=0;i<PageCount;++i){
        const float rowWidth=wide?ImGui::GetContentRegionAvail().x:cellWidth;if(!wide&&i)ImGui::SameLine();ImGui::PushID(i);
        const auto origin=ImGui::GetCursorScreenPos();const ImVec2 size{rowWidth,wide?f*2.7f:cellHeight};
        const bool pressed=ImGui::InvisibleButton("##Page",size,ImGuiButtonFlags_EnableNav);if(pressed)selected=i;
        const auto id=ImGui::GetItemID();const bool hover=ImGui::IsItemHovered(),held=ImGui::IsItemActive();
        // The same native item can move between the row and scroll strip on resize.
        auto* previous=GImGui->NavWindow;auto* current=ImGui::GetCurrentWindow();
        if(!wide&&id==GImGui->NavId&&previous&&previous!=current&&
           (previous==parent||(previous->ParentWindow==parent&&previous->ChildId==stripId)))
            ImGui::SetFocusID(id,current);
        if(strip&&selected==i&&(reveal||pressed))ImGui::SetScrollHereX(.5f);
        const float chosen=Animate(id,10,selected==i?1.f:0.f,22);auto bg=ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);bg.w*=(std::max)(chosen,Animate(id,11,hover?.45f:0.f));auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin,origin+size,ImGui::GetColorU32(bg),8);if(selected==i)draw->AddRect(origin,origin+size,ImGui::GetColorU32(ImGuiCol_CheckMark),8);
        const auto ink=ImGui::GetColorU32(selected==i?ImGuiCol_CheckMark:ImGuiCol_Text);
        const auto iconInk=status&&(i==1||i==2||i==3)?ImGui::GetColorU32(PilotColor(i==2?(status[i].running?PilotState::On:PilotState::Off):status[i].pilot)):ink;
        Icon(draw,i==1?2:i==2?1:i,{origin.x+f*(wide?.7f:.35f),origin.y+(size.y-f)*.5f},f,iconInk);
        const auto caption=Neurotic::Translate(Pages[i]);const auto captionSize=ImGui::CalcTextSize(caption.c_str(),nullptr,false,wide?0:captionRoom);const float left=origin.x+f*(wide?2.2f:1.75f),right=origin.x+size.x-f*.35f;const bool abbreviated=!wide&&captionSize.y>2*f+.1f;
        draw->PushClipRect({left,origin.y},{right,origin.y+size.y},true);
        if(abbreviated){const auto at=ImVec2{left,origin.y+(size.y-f)*.5f};ImGui::RenderTextEllipsis(draw,at,{right,at.y+f},right,caption.c_str(),nullptr,nullptr);}
        else{const float x=wide?left:left+(std::max)(0.f,(captionRoom-captionSize.x)*.5f);draw->AddText(ImGui::GetFont(),captionFont,{x,origin.y+(size.y-captionSize.y)*.5f+held*.5f},ink,caption.c_str(),nullptr,wide?0:captionRoom);}
        draw->PopClipRect();if(abbreviated&&(hover||ImGui::IsItemFocused())){ImGui::BeginTooltip();ImGui::PushTextWrapPos((std::min)(400.f,(std::max)(1.f,ImGui::GetMainViewport()->WorkSize.x-48)));ImGui::TextUnformatted(caption.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();}
        if(wide&&chosen>.001f){auto mark=ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);mark.w*=chosen;draw->AddRectFilled({origin.x,origin.y+f*.8f},{origin.x+3,origin.y+size.y-f*.8f},ImGui::GetColorU32(mark),2);}
        ImGui::RenderNavCursor(ImRect(origin,origin+size),id);ImGui::PopID();
    }
    if(wide){ImGui::EndChild();ImGui::SameLine();}else if(strip){ImGui::PopID();ImGui::EndChild();ImGui::Spacing();ImGui::Separator();ImGui::Spacing();}else{ImGui::EndGroup();ImGui::Spacing();ImGui::Separator();ImGui::Spacing();}
    parent->StateStorage.SetInt(selectedKey,selected);parent->StateStorage.SetFloat(widthKey,available);ImGui::PopStyleColor();ImGui::PopFont();
}

struct PageReveal
{
    explicit PageReveal(int selected)
    {
        auto* window = ImGui::GetCurrentWindow();
        auto& storage=window->StateStorage;
        const auto id=window->GetID("##PageReveal");
        const bool changed=storage.GetInt(Key(id,12,2),-1)!=selected;
        const int frame=ImGui::GetFrameCount();
        if (changed || storage.GetInt(Key(id,12,1),-1000)<frame-1) {
            storage.SetInt(Key(id,12,2),selected);
            storage.SetFloat(Key(id,12,0),0);
            storage.SetInt(Key(id,12,1),frame-1);
        }
        const float t=Animate(id,12,1,22);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*(.85f+.15f*t));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY()+(1-t)*ImGui::GetFontSize()*.45f);
    }
    ~PageReveal() { ImGui::PopStyleVar(); }
};
inline void ContinueRow(float width, float spacing = -1)
{
    const float gap=spacing<0?ImGui::GetStyle().ItemSpacing.x:spacing;
    const float edge=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
    if (edge-ImGui::GetItemRectMax().x >= gap+width) ImGui::SameLine(0,gap);
}
inline float ButtonWidth(const char* text)
{
    return ImGui::CalcTextSize(text).x+ImGui::GetStyle().FramePadding.x*2;
}
inline float ScaleControlWidth() { return ImGui::GetFontSize()*6.2f; }
inline float FieldLabelWidth(const char* text)
{
    ImGui::PushFont(nullptr,ImGui::GetFontSize()*.82f);
    const float width=ImGui::CalcTextSize(text).x;
    ImGui::PopFont();return width;
}
inline float FieldLabelReserve(const char* text)
{
    return FieldLabelWidth(text)+ImGui::GetStyle().ItemInnerSpacing.x;
}
inline void FieldLabel(const char* text,ImVec2 origin,float rowHeight)
{
    ImGui::PushFont(nullptr,ImGui::GetFontSize()*.82f);
    ImGui::SetCursorScreenPos({origin.x,origin.y+(rowHeight-ImGui::GetFontSize())*.5f});
    ImGui::TextUnformatted(text);ImGui::PopFont();
}
inline float GraphControlWidth(bool caption=true)
{
    return ToggleWidth()+(caption?ImGui::GetStyle().ItemInnerSpacing.x+ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.sleekshell.show_graphs_ec43657d", "Show Graphs")).x:0);
}
struct HeaderGeometry
{
    float side=ImGui::GetFrameHeight()*1.25f,gap=ImGui::GetStyle().ItemSpacing.x;
    float scale=ScaleControlWidth()+FieldLabelReserve(Neurotic::UiLiteral("ingame.menu-common.scale_92419c71", "Scale")),graph=GraphControlWidth(),save=ButtonWidth(Neurotic::UiLiteral("ingame.sleekshell.save_settings_f2d25d67", "Save Settings"))+ImGui::GetFontSize()*2;
    bool controls=false,withScale=true,graphCaption=true;
    float Width() const { return side*2+save+gap*2+(controls?graph+gap+(withScale?scale+gap:0):0); }
    explicit HeaderGeometry(bool withControls, bool includeScale=true) : controls(withControls), withScale(includeScale)
    {
        const float available=(std::max)(0.0f,ImGui::GetContentRegionAvail().x-ImGui::GetFontSize()*3);
        if(controls && Width()+ImGui::GetFontSize()*10>available) { graphCaption=false;graph=GraphControlWidth(false); }
        // Large explicit scales in a small viewport retain the same native controls.
        // Only their captions compress; icon/hit targets and the common gaps remain.
        if(Width()>available) save=(std::max)(side,save-(Width()-available));
        if(controls && withScale && Width()>available) scale=(std::max)(side+FieldLabelReserve(Neurotic::UiLiteral("ingame.menu-common.scale_92419c71", "Scale")),scale-(Width()-available));
        if(Width()>available) gap=(std::max)(0.0f,gap-(Width()-available)/(controls?(withScale?4:3):2));
    }
};
inline float HeaderActionsWidth(bool controls=false, bool scale=true)
{
    return HeaderGeometry(controls,scale).Width();
}
struct HeaderActionResult { bool themeChanged=false,saveClicked=false,closeClicked=false,scaleChanged=false; ImGuiID saveId=0; };
inline HeaderActionResult HeaderActions(bool& light,int* scale=nullptr,bool* graphs=nullptr,const char* autoText=Neurotic::UiLiteral("ingame.menu-common.auto_b980aecf", "Auto"))
{
    const auto origin=ImGui::GetCursorScreenPos();
    const HeaderGeometry layout(graphs != nullptr, scale != nullptr);
    const float side=layout.side,gap=layout.gap;
    const float edge=origin.x+ImGui::GetContentRegionAvail().x;
    const bool graphCaption=layout.graphCaption;
    float x=edge-layout.Width();
    HeaderActionResult result;
    if(scale) {
        const float label=FieldLabelReserve(Neurotic::UiLiteral("ingame.menu-common.scale_92419c71", "Scale"));
        FieldLabel(Neurotic::UiLiteral("ingame.menu-common.scale_92419c71", "Scale"),{x,origin.y},side);
        ImGui::SetCursorScreenPos({x+label,origin.y+(side-ImGui::GetFrameHeight())*.5f});
        ImGui::SetNextItemWidth(layout.scale-label);
        const char* choices[]={autoText,"0.5","0.6","0.7","0.8","0.9","1.0","1.1","1.2","1.3","1.4","1.5","1.6","1.7","1.8","1.9","2.0"};
        if(ImGui::BeginCombo("##MenuScale",choices[ImClamp(*scale,0,16)])) {
            for(int i=0;i<17;++i) {
                if(ImGui::Selectable(choices[i],*scale==i)) { *scale=i;result.scaleChanged=true; }
                if(*scale==i) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        x+=layout.scale+gap;
    }
    if(graphs) {
        ImGui::SetCursorScreenPos({x,origin.y+(side-ImGui::GetFrameHeight())*.5f});
        ImGui::Checkbox(graphCaption?Neurotic::UiLiteral("ingame.sleekshell.show_graphs_ec43657d", "Show Graphs###HeaderGraphs"):"###HeaderGraphs",graphs);
        x+=layout.graph+gap;
    }
    ImGui::SetCursorScreenPos({x,origin.y});
    result.themeChanged=ThemeButton(light,false);
    ImGui::SetCursorScreenPos({x+side+gap,origin.y});
    result.saveClicked=FeedbackButton(Neurotic::UiLiteral("ingame.sleekshell.save_settings_f2d25d67", "Save Settings"),FeedbackKind::Save,side,layout.save);
    result.saveId=ImGui::GetItemID();
    ImGui::SameLine(0,gap);
    result.closeClicked=ImGui::Button("X##CloseMenu",{side,side});
    ImGui::SetCursorScreenPos(origin);
    return result;
}
// Child pages share the native scrolling tab bar used by Neural Rendering.
inline const char* const* ChildPages(int page,int& count)
{
    static const char* general[]={Neurotic::UiLiteral("ingame.option.22e2bada8f1c", "Updates"),Neurotic::UiLiteral("ingame.sleekshell.key_binds_8c47ee2b", "Key Binds"),Neurotic::UiLiteral("ingame.sleekshell.gameplay_input_9a93476e", "Gameplay Input"),Neurotic::UiLiteral("ingame.provider.574b2b771756", "Theme & Color"),Neurotic::UiLiteral("ingame.option.ec811d30a89c", "Brightness"),Neurotic::UiLiteral("ingame.option.7f7c33066764", "Animations")};
    static const char* upscaling[]={Neurotic::UiLiteral("ingame.sleekshell.upscalers_a32656b9", "Upscalers"),Neurotic::UiLiteral("ingame.option.0a50d9b5b905", "DLSS"),Neurotic::UiLiteral("ingame.provider.03917f4d25dd", "FSR / XeSS"),Neurotic::UiLiteral("ingame.option.1aa4cb0bcca7", "Image"),Neurotic::UiLiteral("ingame.option.8be62fc3ae5e", "Initialization"),Neurotic::UiLiteral("ingame.sleekshell.sr_pre_flight_7c178d08", "SR Pre-Flight")};
    static const char* frameGeneration[]={Neurotic::UiLiteral("ingame.sleekshell.game_fg_native_mfg_ff441192", "Game FG / Native MFG"),Neurotic::UiLiteral("ingame.option.7013af4c42fe", "Setup"),Neurotic::UiLiteral("ingame.sleekshell.replacement_fg_66f508aa", "Replacement FG"),Neurotic::UiLiteral("ingame.menu-common.pacing_latency_ed3df81a", "Pacing & Latency")};
    static const char* advanced[]={Neurotic::UiLiteral("ingame.sleekshell.graphics_overrides_561c74c0", "Graphics Overrides"),Neurotic::UiLiteral("ingame.menu-common.anisotropic_filtering_a7028718", "Anisotropic Filtering"),Neurotic::UiLiteral("ingame.sleekshell.shader_troubleshooting_9a2cb169", "Shader Troubleshooting"),Neurotic::UiLiteral("ingame.sleekshell.resource_barriers_2b197be7", "Resource Barriers"),Neurotic::UiLiteral("ingame.sleekshell.root_signatures_96b09579", "Root Signatures")};
    static const char* tools[]={Neurotic::UiLiteral("ingame.option.067348ce6894", "Screenshots"),Neurotic::UiLiteral("ingame.option.b3a4c2cd1dae", "Magnifier"),Neurotic::UiLiteral("ingame.menu-common.mipmap_bias_c3d9e7cf", "Mipmap Bias"),Neurotic::UiLiteral("ingame.dlssnr-menu.compare_614fcd95", "Compare")};
    static const char* diagnostics[]={Neurotic::UiLiteral("ingame.option.d3ef01b4a9c9", "Logging"),Neurotic::UiLiteral("ingame.option.12d0b77aba94", "Quirks"),Neurotic::UiLiteral("ingame.menu-common.fps_overlay_85d6c2e4", "FPS Overlay"),Neurotic::UiLiteral("ingame.option.1900b478a586", "Capture"),Neurotic::UiLiteral("ingame.provider.e2fe353986e6", "Rendering")};
    switch(page) {
    case 0: count=6;return general;
    case 1: count=6;return upscaling;
    case 3: count=4;return frameGeneration;
    case 4: count=5;return advanced;
    case 5: count=4;return tools;
    case 6: count=5;return diagnostics;
    default: count=0;return nullptr;
    }
}
inline int ChildPageCount(int page) { int count=0;ChildPages(page,count);return count; }
inline void SectionNavigation(int page,int& selected,PilotState preflight=PilotState::Degraded,bool forceSelection=false)
{
    int count=0;const auto labels=ChildPages(page,count);
    if(!count) return;
    selected=ImClamp(selected,0,count-1);
    const int requested=selected;
    if(ImGui::BeginTabBar("##SectionPages",ImGuiTabBarFlags_FittingPolicyScroll)) {
        for(int display=0;display<count;++display) {
            // Pre-flight leads visually; existing page/scroll keys stay unchanged.
            const int i=page==1?(display==0?5:display-1):page==3?(display==0?1:display==1?0:display):page==6?(display==0?4:display-1):display;
            if ((page==1 && i==4) || (page==4 && i==1)) continue;
            // Original English labels retain identity when the display language changes.
            const bool lamp=page==1&&i==5;
            if(lamp)ImGui::SetNextItemWidth(ImGui::CalcTextSize(labels[i]).x+ImGui::GetStyle().FramePadding.x*2+PilotReserve());
            const bool open=ImGui::BeginTabItem(labels[i],nullptr,ImGuiTabItemFlags_NoTooltip |
                (forceSelection&&i==requested?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None));
            if(lamp)TabPilotLight(preflight);
            if(open) { selected=i;ImGui::EndTabItem(); }
        }
        ImGui::EndTabBar();
    }
}
// Native tabs remain outside each child page's scroll region.
inline void NeuralNavigation(int& selected,bool main=false,bool multipass=false,bool overrides=false,bool inspector=false,
                             bool forceSelection=false,PilotState preflight=PilotState::Degraded)
{
    static const char* names[] = {Neurotic::UiLiteral("ingame.sleekshell.nr_settings_bec41745", "NR Settings"), Neurotic::UiLiteral("ingame.sleekshell.multi_pass_4874148b", "Multi Pass"), Neurotic::UiLiteral("ingame.option.da188e3b1cef", "Inspector"), Neurotic::UiLiteral("ingame.option.268f14bbfe11", "Diagnostics"), Neurotic::UiLiteral("ingame.option.7f6e1f2662b4", "Overrides"), Neurotic::UiLiteral("ingame.sleekshell.object_rules_f7828407", "Object Rules"), Neurotic::UiLiteral("ingame.sleekshell.nr_pre_flight_59ef61b0", "NR Pre-flight")};
    static const char* ids[] = {"###NrOverview", "###NrMultipass", "###NrInspector", "###NrDiagnostics", "###NrOverrides", "###NrObjectRules", "###NrInputPreflight"};
    // Keep page identities and their scroll state stable while changing display order.
    static constexpr int order[] = {6, 0, 1, 5, 4, 2};
    const int requested=selected;
    if (ImGui::BeginTabBar("##NeuralPages",ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int i : order) {
            std::string label=Neurotic::Translate(names[i]);
            const bool onState=i==0?main:i==1?multipass:i==4?overrides:i==2?inspector:false;
            const bool lamp=i==0 || i==1 || i==4 || i==2 || i==6;
            if(lamp)ImGui::SetNextItemWidth(ImGui::CalcTextSize(label.c_str()).x+ImGui::GetStyle().FramePadding.x*2+PilotReserve());
            label+=ids[i];
            const bool open=ImGui::BeginTabItem(label.c_str(),nullptr,
                ImGuiTabItemFlags_NoTooltip | (forceSelection && i==requested ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None));
            if(lamp)TabPilotLight(i==6?preflight:onState?PilotState::On:PilotState::Off);
            if(open) {
                selected=i;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}
inline float FooterScaleWidth()
{
    const auto* window=ImGui::GetCurrentWindow();
    const float available=window->Size.x-window->WindowPadding.x*2-window->ScrollbarSizes.x;
    return (std::min)(ScaleControlWidth()+FieldLabelReserve(Neurotic::UiLiteral("ingame.menu-common.scale_92419c71", "Scale")),available*.36f);
}
inline float FooterReadoutOffset() { return ImGui::GetFontSize()/8.0f; }
inline float FooterBottomInset()
{
    return (std::max)(0.0f,ImGui::GetStyle().WindowPadding.y-FooterReadoutOffset()*2);
}
inline float FooterHeight(bool readout)
{
    const auto& style=ImGui::GetStyle();
    return ImGui::GetFrameHeight()+(readout?ImGui::GetFontSize()*.82f+style.ItemSpacing.y*.35f+FooterReadoutOffset():0);
}
inline bool FooterScale(int& scale,const char* autoText=Neurotic::UiLiteral("ingame.menu-common.auto_b980aecf", "Auto"))
{
    const auto* window=ImGui::GetCurrentWindow();
    const float right=window->Pos.x+window->Size.x-window->WindowPadding.x-window->ScrollbarSizes.x;
    const float bottom=window->Pos.y+window->Size.y-FooterBottomInset();
    const float width=FooterScaleWidth();
    const bool caption=width>=ScaleControlWidth()+FieldLabelReserve(Neurotic::UiLiteral("ingame.menu-common.scale_92419c71", "Scale"))-.5f;
    const float label=caption?FieldLabelReserve(Neurotic::UiLiteral("ingame.menu-common.scale_92419c71", "Scale")):0;
    if(caption) FieldLabel(Neurotic::UiLiteral("ingame.menu-common.scale_92419c71", "Scale"),{right-width,bottom-ImGui::GetFrameHeight()},ImGui::GetFrameHeight());
    ImGui::SetCursorScreenPos({right-width+label,bottom-ImGui::GetFrameHeight()});
    ImGui::SetNextItemWidth(width-label);
    const char* choices[]={autoText,"0.5","0.6","0.7","0.8","0.9","1.0","1.1","1.2","1.3","1.4","1.5","1.6","1.7","1.8","1.9","2.0"};
    bool changed=false;
    if(ImGui::BeginCombo("##MenuScale",choices[ImClamp(scale,0,16)])) {
        for(int i=0;i<17;++i) {
            if(ImGui::Selectable(choices[i],scale==i)) { scale=i;changed=true; }
            if(scale==i) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}
inline float LanguageControlWidth()
{
    float width=0;
    for(const auto& language : Neurotic::Languages) width=(std::max)(width,ImGui::CalcTextSize(language.name).x);
    return width+ImGui::GetFrameHeight()+ImGui::GetStyle().FramePadding.x*2;
}
struct FooterActionResult { bool supportClicked=false,languageChanged=false,scaleChanged=false; };
inline FooterActionResult FooterActions(int* language=nullptr,float reservedWidth=0)
{
    const auto origin=ImGui::GetCursorScreenPos();
    const float available=(std::max)(1.0f,ImGui::GetContentRegionAvail().x-reservedWidth);
    const float gap=ImGui::GetStyle().ItemSpacing.x;
    const float button=ButtonWidth(Neurotic::UiLiteral("ingame.sleekshell.send_ko_fi_183e38d3", "Send Ko-fi"));
    float languageWidth=language?LanguageControlWidth():0;
    const bool languageCaption=available>=button+languageWidth+FieldLabelReserve(Neurotic::UiLiteral("ingame.sleekshell.language_990617e3", "Language"))+gap*3;
    const float languageLabel=languageCaption?FieldLabelReserve(Neurotic::UiLiteral("ingame.sleekshell.language_990617e3", "Language")):0;
    float languageGroup=language?languageWidth+languageLabel+gap*2:0;
    if(language && available<button+languageGroup+gap) {
        languageWidth=(std::max)(ImGui::GetFrameHeight()*1.25f,
            available-button-gap*3-languageLabel);
        languageGroup=languageWidth+languageLabel+gap*2;
    }
    const bool prompt=available>=button+languageGroup+gap*2+ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.sleekshell.enjoying_neurotic_f2ba5f3e", "Enjoying NeuRotic?")).x;
    FooterActionResult result;
    ImGui::BeginGroup();
    if(prompt) { ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.sleekshell.enjoying_neurotic_f2ba5f3e", "Enjoying NeuRotic?"));ImGui::SameLine(); }
    result.supportClicked=SupportButton();
    if(language) {
        const auto end=ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine({end.x+gap,origin.y+4},
            {end.x+gap,origin.y+ImGui::GetFrameHeight()-4},ImGui::GetColorU32(ImGuiCol_Border),1);
        ImGui::SameLine(0,gap*2);
        const auto field=ImGui::GetCursorScreenPos();
        if(languageCaption) FieldLabel(Neurotic::UiLiteral("ingame.sleekshell.language_990617e3", "Language"),field,ImGui::GetFrameHeight());
        ImGui::SetCursorScreenPos({field.x+languageLabel,field.y});
        ImGui::SetNextItemWidth(languageWidth);
        if(ImGui::BeginCombo("##FooterLanguage",Neurotic::Languages[ImClamp(*language,0,Neurotic::LanguageCount-1)].name)) {
            for(int i=0;i<Neurotic::LanguageCount;++i) {
                if(ImGui::Selectable(Neurotic::Languages[i].name,*language==i)) { *language=i;result.languageChanged=true; }
                if(*language==i) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::EndGroup();
    return result;
}
// One footer row: support stays left; language and scale share the right-hand baseline.
inline FooterActionResult FooterControls(int& language,int& scale,const char* autoText=Neurotic::UiLiteral("ingame.menu-common.auto_b980aecf", "Auto"),const char* scalePreview=nullptr)
{
    const auto origin=ImGui::GetCursorScreenPos();
    const float available=ImGui::GetContentRegionAvail().x;
    const float gap=ImGui::GetStyle().ItemSpacing.x;
    const float height=ImGui::GetFrameHeight();
    float languageWidth=LanguageControlWidth(),scaleWidth=ScaleControlWidth();
    if(scalePreview)scaleWidth=(std::max)(scaleWidth,ImGui::CalcTextSize(scalePreview).x+height+ImGui::GetStyle().FramePadding.x*2);
    float languageLabel=FieldLabelReserve(Neurotic::UiLiteral("ingame.sleekshell.language_990617e3", "Language")),scaleLabel=FieldLabelReserve(Neurotic::UiLiteral("ingame.menu-common.scale_92419c71", "Scale"));
    const float budget=(std::max)(height*2,available-ButtonWidth(Neurotic::UiLiteral("ingame.sleekshell.send_ko_fi_183e38d3", "Send Ko-fi"))-gap*3);
    if(languageWidth+scaleWidth+languageLabel+scaleLabel+gap*2>budget)
        languageLabel=scaleLabel=0;
    if(languageWidth+scaleWidth+gap*2>budget) {
        const float controlRoom=(std::max)(height*2,budget-gap*2);
        // Keep the requested/displayed numeric scale readable; language names
        // already have the picker's full-label hover presentation when compact.
        scaleWidth=(std::min)(scaleWidth,controlRoom-height);
        languageWidth=(std::min)(languageWidth,controlRoom-scaleWidth);
    }
    const float total=languageWidth+scaleWidth+languageLabel+scaleLabel+gap*2;
    const float languageX=origin.x+available-total;
    const float scaleX=origin.x+available-scaleWidth-scaleLabel;
    const auto labelAt=[&](const char* text,float x) {
        ImGui::PushFont(nullptr,ImGui::GetFontSize()*.82f);
        // Draw at the row centre; previous button baselines must not shift the label down.
        const auto translated=Neurotic::Translate(text);
        ImGui::GetWindowDrawList()->AddText({x,origin.y+(height-ImGui::GetFontSize())*.5f},
            ImGui::GetColorU32(ImGuiCol_Text),translated.c_str());
        ImGui::PopFont();
    };
    ImGui::BeginGroup();
    auto result=FooterActions(nullptr,total+gap);
    if(languageLabel>0) labelAt(Neurotic::UiLiteral("ingame.sleekshell.language_990617e3", "Language"),languageX);
    ImGui::SetCursorScreenPos({languageX+languageLabel,origin.y});
    ImGui::SetNextItemWidth(languageWidth);
    (void)language; // Saved legacy IDs remain readable; shared selection owns presentation.
    result.languageChanged=Neurotic::LanguagePicker("##FooterLanguage");
    ImGui::GetWindowDrawList()->AddLine({scaleX-gap,origin.y+4},{scaleX-gap,origin.y+height-4},
        ImGui::GetColorU32(ImGuiCol_Border),1);
    if(scaleLabel>0) labelAt(Neurotic::UiLiteral("ingame.menu-common.scale_92419c71", "Scale"),scaleX);
    ImGui::SetCursorScreenPos({scaleX+scaleLabel,origin.y});
    ImGui::SetNextItemWidth(scaleWidth);
    const char* choices[]={autoText,"0.5","0.6","0.7","0.8","0.9","1.0","1.1","1.2","1.3","1.4","1.5","1.6","1.7","1.8","1.9","2.0"};
    if(ImGui::BeginCombo("##MenuScale",scalePreview?scalePreview:choices[ImClamp(scale,0,16)])) {
        for(int i=0;i<17;++i) {
            if(ImGui::Selectable(choices[i],scale==i)) { scale=i;result.scaleChanged=true; }
            if(scale==i) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndGroup();
    return result;
}
inline void FooterReadout(const char* text,float reservedWidth=0)
{
    // Keep long device names within the existing bezel; the full value remains inspectable.
    ImGui::SetCursorPosY(ImGui::GetCursorPosY()-ImGui::GetStyle().ItemSpacing.y*.65f+FooterReadoutOffset());
    ImGui::PushFont(nullptr,ImGui::GetFontSize()*.82f);
    const auto origin=ImGui::GetCursorScreenPos();
    const auto size=ImVec2((std::max)(1.0f,ImGui::GetContentRegionAvail().x-reservedWidth),ImGui::GetFontSize());
    const bool clipped=ImGui::CalcTextSize(text).x>size.x;
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),origin,origin+size,origin.x+size.x,text,nullptr,nullptr);
    ImGui::PopStyleColor();
    ImGui::Dummy(size);
    ImGui::PopFont();
}
struct ObservationActionResult { bool refreshClicked=false,copyClicked=false; ImGuiID copyId=0; };
inline ObservationActionResult ObservationActions(bool refreshBusy=false)
{
    const float gap=ImGui::GetStyle().ItemSpacing.x;
    const auto refreshLabel=Neurotic::Translate(Neurotic::UiLiteral("ingame.dlssnr-menu.refresh_observations_57f5c6d4", "Refresh observations"));
    const float refresh=DlssNr::ObservationRefreshButtonWidth(refreshLabel.c_str());
    const float copy=ButtonWidth(Neurotic::UiLiteral("ingame.dlssnr-menu.copy_observation_report_0cee5963", "Copy observation report"))+ImGui::GetFontSize()*2;
    const bool paired=ImGui::GetContentRegionAvail().x>=refresh+gap+copy;
    const float width=paired?refresh+gap+copy:refresh;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(std::max)(0.0f,ImGui::GetContentRegionAvail().x-width));
    ObservationActionResult result;
    result.refreshClicked=DlssNr::ObservationRefreshButton(Neurotic::UiLiteral("ingame.dlssnr-menu.refresh_observations_57f5c6d4", "Refresh observations"),refreshBusy,refresh);
    if(paired) ImGui::SameLine();
    else ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(std::max)(0.0f,ImGui::GetContentRegionAvail().x-copy));
    result.copyClicked=FeedbackButton(Neurotic::UiLiteral("ingame.dlssnr-menu.copy_observation_report_0cee5963", "Copy observation report"));
    result.copyId=ImGui::GetItemID();
    return result;
}
struct DetectedSource { const char* name; bool present; };
inline void UpscalerNotice(const char* message,const DetectedSource* sources=nullptr,int count=0,
                           const char* backend=nullptr, bool active=false, bool idle=false)
{
    ImGui::BeginChild("##UpscalerNotice",{0,0},ImGuiChildFlags_AutoResizeY |
        ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    WindowSectionHeader(Neurotic::UiLiteral("ingame.sleekshell.upscalers_a32656b9", "Upscalers"));
    if(backend && *backend) ImGui::TextColored(active?ImVec4(.30f,.82f,.48f,1):idle?
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled):ImVec4(1.f,.72f,.25f,1),"%s: %s",Neurotic::Translate(backend).c_str(),Neurotic::Translate(active?Neurotic::UiLiteral("ingame.provider.96879611650f", "active"):idle?Neurotic::UiLiteral("ingame.provider.4fb62348858c", "idle"):Neurotic::UiLiteral("ingame.sleekshell.not_initialized_223b845b", "not initialized")).c_str());
    ImGui::TextWrapped("%s",Neurotic::Translate(message).c_str());
    if(count) {
        const bool pairs=ImGui::GetContentRegionAvail().x>=ImGui::GetFontSize()*32;
        if(ImGui::BeginTable("##DetectedUpscalerFiles",pairs?4:2,ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            for(int pair=0;pair<(pairs?2:1);++pair) {
                ImGui::TableSetupColumn("##Source",ImGuiTableColumnFlags_WidthStretch,1.5f);
                ImGui::TableSetupColumn("##Status",ImGuiTableColumnFlags_WidthStretch,1);
            }
            for(int row=0;row<count;++row) {
                ImGui::TableNextColumn();ImGui::TextDisabled("%s",Neurotic::Translate(sources[row].name).c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                    "%s",Neurotic::Translate(sources[row].present?Neurotic::UiLiteral("ingame.provider.fefd8ba5a024", "Exists"):Neurotic::UiLiteral("ingame.provider.29480cd687c3", "Doesn't Exist")).c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}
}
