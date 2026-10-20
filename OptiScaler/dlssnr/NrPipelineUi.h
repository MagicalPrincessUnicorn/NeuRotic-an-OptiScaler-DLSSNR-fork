#pragma once
#include "NrPipelineView.h"
#include "NrPipelineMotion.h"
#include <menu/ItemHelp.h>
#include <menu/SleekUi.h>
#include <menu/SleekShell.h>
#include <imgui/imgui_internal.h>
#include <menu/Localization.h>
#include <imgui/imgui.h>
#include <string>
#include <algorithm>
namespace DlssNr::PipelineUi {
using namespace PipelineView;
struct Readouts { std::array<std::string,7> provider{}; };
inline const char* OrderCaption() {
    return Neurotic::UiLiteral("ingame.pipeline.processing_order","Processing Order");
}
inline const char* DetailCaption(bool detailed) {
    return detailed?Neurotic::UiLiteral("desktop.translationeditorwindow.compact_d6853bf3","Compact"):
        Neurotic::UiLiteral("ingame.option.6d46fcd50a63","Detailed");
}
inline float ToolbarButtonWidth(const char* label) {
    const float font=ImGui::GetFontSize(),padding=(std::min)(ImGui::GetStyle().FramePadding.x,font*.35f);
    return ImCeil(ImGui::CalcTextSize(label).x+padding*2+font*1.3f);
}
inline bool DirectionalButton(const char* label,const char* id,float width,float turn) {
    const float font=ImGui::GetFontSize(),padding=(std::min)(ImGui::GetStyle().FramePadding.x,font*.35f);
    const bool pressed=ImGui::Button(id,{width,0});
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();auto* draw=ImGui::GetWindowDrawList();
    const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
    draw->PushClipRect(lo,hi,true);
    draw->AddText({lo.x+padding,lo.y+(hi.y-lo.y-font)*.5f},ink,label);
    const float arrowX=hi.x-padding-font*.4f,arrowY=(lo.y+hi.y)*.5f;
    for(float offset:{-.18f,.18f})Neurotic::Sleek::Chevron(draw,{arrowX,arrowY+font*offset},font*.65f,ink,turn);
    draw->PopClipRect();return pressed;
}
struct WorkspaceHeaderLayout { float padding, width, masterHeight, disclosureHeight; bool stackReadouts; };
inline WorkspaceHeaderLayout WorkspaceHeader(float available) {
    const float font=ImGui::GetFontSize(),padding=font*.3f;
    const auto title=Neurotic::Translate("Neural Rendering");
    const float titleWidth=ImGui::GetFont()->CalcTextSizeA(font*1.18f,FLT_MAX,0,title.c_str()).x;
    const float requested=(std::max)(font*13.5f,titleWidth+font*4.7f);
    const float width=(std::min)(requested,(std::max)(1.f,available-padding*2));
    const float titleHeight=ImGui::GetFont()->CalcTextSizeA(font*1.18f,FLT_MAX,(std::max)(1.f,width-font*4.4f),title.c_str()).y;
    const float controls=ToolbarButtonWidth(OrderCaption())+(std::max)(ToolbarButtonWidth(DetailCaption(true)),ToolbarButtonWidth(DetailCaption(false)))+font*.55f;
    const int toolbarRows=controls>available-padding*2?2:1;
    const float toolbarHeight=ImGui::GetFrameHeight()*toolbarRows+ImGui::GetStyle().ItemSpacing.y*(toolbarRows-1);
    return {padding,width,(std::max)(font*2.65f,titleHeight+font*.5f),toolbarHeight,available-padding*2-width-ImGui::GetStyle().ItemSpacing.x<font*12.f};
}
// Presentation changes only the local value on native button activation. The caller owns persistence.
inline bool MasterButton(bool& enabled,const WorkspaceHeaderLayout& header) {
    const float font=ImGui::GetFontSize(),height=header.masterHeight;
    const bool pressed=ImGui::Button("###NrMaster",{header.width,height});
    if(pressed)enabled=!enabled;
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    auto* draw=ImGui::GetWindowDrawList();
    const auto title=Neurotic::Translate("Neural Rendering");
    const auto bg=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const bool light=bg.x+bg.y+bg.z>1.5f;
    const auto onColor=light?ImVec4(.10f,.43f,.24f,1):ImVec4(.45f,.86f,.61f,1);
    if(enabled)draw->AddRect({lo.x+1.f,lo.y+1.f},{hi.x-1.f,hi.y-1.f},ImGui::GetColorU32(onColor),ImGui::GetStyle().FrameRounding,0,2.f);
    const float divider=hi.x-font*3.5f,titleSize=font*1.18f;
    draw->PushClipRect({lo.x+font*.6f,lo.y},{divider-font*.3f,hi.y},true);
    const float titleWrap=(std::max)(1.f,divider-font*.3f-lo.x-font*.6f);
    const float titleHeight=ImGui::GetFont()->CalcTextSizeA(titleSize,FLT_MAX,titleWrap,title.c_str()).y;
    draw->AddText(ImGui::GetFont(),titleSize,{lo.x+font*.6f,lo.y+(height-titleHeight)*.5f},ImGui::GetColorU32(ImGuiCol_Text),title.c_str(),nullptr,titleWrap);
    draw->PopClipRect();
    draw->AddLine({divider,lo.y+font*.55f},{divider,hi.y-font*.55f},ImGui::GetColorU32(ImGuiCol_Border));
    const char* onOff=enabled?"ON":"OFF";
    const float stateSize=font*(enabled?1.2f:1.f);
    const auto stateWidth=ImGui::GetFont()->CalcTextSizeA(stateSize,FLT_MAX,0,onOff).x;
    if(enabled)draw->AddRectFilled({divider+font*.25f,lo.y+font*.4f},{hi.x-font*.25f,hi.y-font*.4f},
        ImGui::GetColorU32(Neurotic::Sleek::Mix(ImGui::GetStyleColorVec4(ImGuiCol_Button),onColor,.25f)),ImGui::GetStyle().FrameRounding);
    draw->AddText(ImGui::GetFont(),stateSize,{divider+(hi.x-divider-stateWidth)*.5f,lo.y+(height-stateSize)*.5f},
        ImGui::GetColorU32(enabled?onColor:ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)),onOff);
    if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s: %s",title.c_str(),onOff);
    return pressed;
}
inline bool NavigationButton(const char* label,const char* id) {
    const float font=ImGui::GetFontSize();
    const float width=ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2+font;
    const bool pressed=ImGui::Button(id,{width,0});
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();auto* draw=ImGui::GetWindowDrawList();
    const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddText({lo.x+ImGui::GetStyle().FramePadding.x,lo.y+(hi.y-lo.y-font)*.5f},ink,label);
    Neurotic::Sleek::Chevron(draw,{hi.x-ImGui::GetStyle().FramePadding.x-font*.25f,(lo.y+hi.y)*.5f},font*.65f,ink,0);
    return pressed;
}
inline ImVec4 SelectedFill(bool selected) {
    const auto base=ImGui::GetStyleColorVec4(ImGuiCol_Button);
    if(!selected)return base;
    const auto accent=ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const auto bg=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    // A filled, emphasized surface in both themes, independent of signal status.
    auto fill=Neurotic::Sleek::Mix(base,accent,.22f);
    if(bg.x+bg.y+bg.z<1.5f)fill=Neurotic::Sleek::Mix(fill,ImVec4(1,1,1,1),.12f);
    return fill;
}
inline void Icon(ImDrawList* draw,Stage stage,ImVec2 origin,float size,ImU32 ink) {
    if(stage==Stage::Nr||stage==Stage::Sr||stage==Stage::Fg){
        Neurotic::Sleek::Icon(draw,stage==Stage::Nr?1:stage==Stage::Sr?2:3,origin,size,ink);return;
    }
    const auto p=[&](float x,float y){return ImVec2(origin.x+x*size,origin.y+y*size);};
    const auto line=[&](float x,float y,float a,float b){draw->AddLine(p(x,y),p(a,b),ink,1.5f);};
    draw->AddRect(p(.12f,.15f),p(.88f,.72f),ink,size*.08f,0,1.5f);
    if(stage==Stage::Delivery){line(.5f,.72f,.5f,.88f);line(.3f,.88f,.7f,.88f);}
    else if(stage==Stage::Present){line(.33f,.33f,.64f,.43f);line(.64f,.43f,.33f,.56f);}
    else {line(.22f,.59f,.4f,.34f);line(.4f,.34f,.55f,.53f);line(.55f,.53f,.69f,.4f);}
}
inline std::string ActivityText(const Node& node) {
    if(node.stage==Stage::Nr&&node.state==State::Fallback)return "Rendering, reduced inputs";
    if(node.state==State::Used){
        if(node.stage==Stage::Render)return "Frames arriving";
        if(node.stage==Stage::Present)return "Calls arriving";
        if(node.stage==Stage::Delivery)return "Output ready";
    }
    return {}; // Other stages use their existing consumption vocabulary.
}
// Josh requested English-only Experimental additions pending design acceptance.
inline std::string Text(const char*,const char* english){return english;}
inline std::string Name(Stage s,bool rr=false){switch(s){
case Stage::Render:return Text("render","Game Render");case Stage::Inputs:return Text("inputs","Inputs");
case Stage::Nr:return "NR";case Stage::Sr:return rr?"SR + RR":"SR";
case Stage::Present:return Text("game-present","Game Present");case Stage::Fg:return Text("fg","Frame Gen");
default:return Text("delivery","Delivery");}}
inline std::string StateText(State s){switch(s){
case State::Off:return Text("off","Off");case State::Requested:return Text("pending","Requested");
case State::Detected:return Text("detected","Detected");case State::Used:return Text("used","In use");
case State::Fallback:return Text("fallback","Fallback");case State::Blocked:return Text("blocked","Blocked");
default:return Text("unknown","Unknown");}}
inline const char* Mark(State s){switch(s){case State::Used:return "\xe2\x9c\x93";case State::Detected:return "\xe2\x97\x8b";
case State::Fallback:case State::Requested:return "\xe2\x96\xb3";case State::Blocked:return "\xc3\x97";case State::Off:return "-";default:return "?";}}
inline ImVec4 Color(State s){const auto bg=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);const bool light=bg.x+bg.y+bg.z>1.5f;
switch(s){case State::Used:return light?ImVec4(.10f,.43f,.24f,1):ImVec4(.45f,.86f,.61f,1);
case State::Detected:return light?ImVec4(.12f,.36f,.62f,1):ImVec4(.42f,.75f,1,1);
case State::Requested:case State::Fallback:return light?ImVec4(.59f,.29f,.03f,1):ImVec4(.99f,.77f,.37f,1);
case State::Blocked:return light?ImVec4(.72f,.12f,.12f,1):ImVec4(.95f,.30f,.28f,1);
default:return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);}}
inline void Attention(State state,bool reducedMotion) {
    // Only explicit refusal/fallback, never unknown, stale, pending or optional.
    // A short dwell filters transition noise. No navigation/disclosure mutations.
    const int severity=state==State::Blocked?2:state==State::Fallback?1:0;
    const auto id=ImGui::GetItemID();auto* storage=ImGui::GetStateStorage();
    const auto kind=ImHashStr("attention-kind",0,id),start=ImHashStr("attention-start",0,id);
    const float now=float(ImGui::GetTime());
    if(storage->GetInt(kind,0)!=severity||now<storage->GetFloat(start,now)){
        storage->SetInt(kind,severity);storage->SetFloat(start,now);
    }
    const float age=now-storage->GetFloat(start,now);
    if(!severity||age<1.25f)return;
    auto color=Color(state);
    const float pulse=age-1.25f;
    color.w*=!reducedMotion&&pulse<3.f?.5f+.35f*float(.5-.5*std::cos(pulse*5.f)):.65f;
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),ImGui::GetColorU32(color),ImGui::GetStyle().FrameRounding,0,2.f);
}
inline std::string PlacementText(Placement p){switch(p){case Placement::BeforeSr:return Text("before-sr","Before SR");
case Placement::AfterSr:return Text("after-sr","After SR");case Placement::AfterPresent:return Text("after-present","After Game Present");
case Placement::CapturedImage:return Text("captured-image","Captured image");default:return Text("order-unknown","Order unknown");}}
inline std::string Detail(Stage s,bool rr){switch(s){
case Stage::Render:return Text("render-help","The game builds its scene frame here.");
case Stage::Inputs:return Text("inputs-help","These source choices apply to NR. Upscaling and frame generation choose their inputs separately.");
case Stage::Nr:return Text("nr-help","Applies your model and image settings to the game frame.");
case Stage::Sr:return rr?Text("rr-help","Super resolution and ray reconstruction run together here."):
    Text("sr-help","Reconstructs a higher-resolution image from the game frame.");
case Stage::Present:return Text("present-help","The game submits its image here. More processing may follow before display.");
case Stage::Fg:return Text("fg-help","Creates additional frames between rendered game frames.");
default:return Text("delivery-help","The processed frame is handed back for presentation. Physical display is not verified.");}}
inline Stage Draw(const Facts& facts,bool reducedMotion=false,bool* activated=nullptr,const Readouts& readouts={},bool sharedHeader=false){
    const auto m=Build(facts);auto* storage=ImGui::GetStateStorage();const auto key=ImGui::GetID("##PipelineInspectedStage");
    auto selected=static_cast<Stage>(storage->GetInt(key,int(Stage::Nr)));
    const float font=ImGui::GetFontSize(),gap=font*.55f;
    const auto openKey=ImGui::GetID("##ProcessingOrderOpen"),detailKey=ImGui::GetID("##ProcessingOrderDetail");
    bool open=storage->GetBool(openKey,true),detail=storage->GetBool(detailKey,true);
    const float left=ImGui::GetCursorPosX(),availableWidth=ImGui::GetContentRegionAvail().x;
    const auto header=WorkspaceHeader(availableWidth);
    const float right=left+availableWidth-(sharedHeader?header.padding:0.f);
    Neurotic::Sleek::Scope animationScope(reducedMotion);
    const float rowTop=ImGui::GetCursorPosY(),contentLeft=left+(sharedHeader?header.padding:0.f);
    const float toolbarWidth=(std::max)(1.f,right-contentLeft);
    const float orderWidth=(std::min)(toolbarWidth,ToolbarButtonWidth(OrderCaption()));
    const float detailWidth=(std::min)(toolbarWidth,(std::max)(ToolbarButtonWidth(DetailCaption(true)),ToolbarButtonWidth(DetailCaption(false))));
    const float orderX=sharedHeader?right-orderWidth:contentLeft;
    const float detailX=sharedHeader?orderX-gap-detailWidth:right-detailWidth;
    const bool sameRow=sharedHeader?detailX>=contentLeft:detailX>=orderX+orderWidth+gap;
    ImGui::SetCursorPos({orderX,rowTop});
    if(DirectionalButton(OrderCaption(),"##ProcessingOrderToggle",orderWidth,
        Neurotic::Sleek::AnimateLinear(openKey,42,open?-1.f:1.f))){open=!open;storage->SetBool(openKey,open);}
    if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("Show or hide the processing order. Select a stage to open its controls.");
    if(open){
        const char* detailLabel=DetailCaption(detail);
        ImGui::SetCursorPos({sameRow?detailX:right-detailWidth,rowTop+(sameRow?0.f:ImGui::GetFrameHeight()+ImGui::GetStyle().ItemSpacing.y)});
        if(DirectionalButton(detailLabel,"###PipelineDetailToggle",detailWidth,
            Neurotic::Sleek::AnimateLinear(detailKey,42,detail?-1.f:1.f))){detail=!detail;storage->SetBool(detailKey,detail);}
    }
    const float toolbarHeight=sharedHeader?header.disclosureHeight:
        ImGui::GetFrameHeight()+(open&&!sameRow?ImGui::GetFrameHeight()+ImGui::GetStyle().ItemSpacing.y:0.f);
    ImGui::SetCursorPos({left,rowTop+toolbarHeight+(sharedHeader?header.padding:ImGui::GetStyle().ItemSpacing.y)});
    const float expansion=Neurotic::Sleek::AnimateLinear(openKey,40,open?1.f:0.f);
    const float detailAmount=Neurotic::Sleek::AnimateLinear(detailKey,41,detail?1.f:0.f);
    if(expansion>0){
        std::array<Node,7> visible{};unsigned count=0;
        for(unsigned i=0;i<m.count;++i)if(m.nodes[i].stage!=Stage::Inputs&&
            (m.nodes[i].stage!=Stage::Fg||facts.fgOrder!=FgOrder::Unknown))visible[count++]=m.nodes[i];
        const float available=ImGui::GetContentRegionAvail().x;
        std::array<std::string,7> names{};
        for(unsigned i=0;i<count;++i){const auto stage=visible[i].stage;
            names[i]=stage==Stage::Nr?std::string("Neural Rendering"):stage==Stage::Sr?std::string("Upscaling"):stage==Stage::Delivery?std::string("Output"):Name(stage,m.rrIntegrated);}
        const float titleSize=font*1.08f;
        const auto pushTitleFont=[&](){const auto* window=ImGui::GetCurrentWindow();
            const float fontScale=ImGui::GetIO().FontGlobalScale*window->FontWindowScale*window->FontDpiScale;
            ImGui::PushFont(nullptr,titleSize/fontScale);};
        float minimumWidth=font*7.15f;
        pushTitleFont();
        for(unsigned i=0;i<count;++i)minimumWidth=(std::max)(minimumWidth,ImCeil(ImGui::CalcTextSize(names[i].c_str()).x+font*(1.7f+.35f)));
        ImGui::PopFont();
        const float width=(std::max)(minimumWidth,(available-gap*(count-1)-2)/count);
        const float height=font*(3.9f+2.1f*detailAmount);
        const float railHeight=(height+font*(width*count+gap*(count-1)>available?1.5f:.3f))*expansion;
        static ImGuiContext* motionContext=nullptr;static Motion motion;static float previousWidth=0;
        if(motionContext!=ImGui::GetCurrentContext()){motionContext=ImGui::GetCurrentContext();motion={};previousWidth=0;}
        std::array<float,7> target{};for(unsigned i=0;i<count;++i)target[int(visible[i].stage)]=float(i);
        const auto positions=motion.Update(target,ImGui::GetTime(),reducedMotion,std::abs(width-previousWidth)>.5f);previousWidth=width;
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,font);
        const float railTop=ImGui::GetCursorPosY();
        ImGui::BeginChild("##ProcessingOrderRail",{0,railHeight},ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        // Override ImGui's child minimum so the whole following layout reaches zero smoothly.
        auto* rail=ImGui::GetCurrentWindow();rail->Size.y=rail->SizeFull.y=railHeight;
        rail->OuterRectClipped.Max.y=(std::min)(rail->OuterRectClipped.Max.y,rail->Pos.y+railHeight);
        rail->ClipRect.Max.y=(std::min)(rail->ClipRect.Max.y,rail->Pos.y+railHeight);
        rail->DrawList->PopClipRect();rail->DrawList->PushClipRect(rail->ClipRect.Min,rail->ClipRect.Max,true);
        ImGui::BeginDisabled(!open);
        if(ImGui::IsWindowHovered()&&!ImGui::GetIO().KeyCtrl&&ImGui::GetScrollMaxX()>0)
            ImGui::SetScrollX(ImGui::GetScrollX()-ImGui::GetIO().MouseWheel*font*5);
        const auto origin=ImGui::GetCursorPos();
        for(unsigned i=0;i<count;++i){const auto& node=visible[i];const int index=int(node.stage);
            ImGui::SetCursorPos({origin.x+positions[index]*(width+gap),origin.y});
            const auto& name=names[i];
            const auto id="###NrPipelineStage"+std::to_string(index);
            const auto base=ImGui::GetStyleColorVec4(ImGuiCol_Button),accent=ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            auto fill=SelectedFill(selected==node.stage);
            if(node.stage==Stage::Nr)fill=Neurotic::Sleek::Mix(fill,accent,.10f);
            ImGui::PushStyleColor(ImGuiCol_Button,fill);if(ImGui::Button(id.c_str(),{width,height})){selected=node.stage;storage->SetInt(key,index);if(activated)*activated=true;}ImGui::PopStyleColor();
            Attention(node.stage==Stage::Sr?State::Unknown:node.state,reducedMotion);
            const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();auto* draw=ImGui::GetWindowDrawList();
            const auto line=[&](const std::string& text,float y,ImU32 color,float inset=.45f){const ImVec2 a(lo.x+font*inset,lo.y+y),b(hi.x-font*.35f,lo.y+y+font*1.1f);ImGui::PushStyleColor(ImGuiCol_Text,color);ImGui::RenderTextEllipsis(draw,a,b,b.x,text.c_str(),nullptr,nullptr);ImGui::PopStyleColor();};
            draw->PushClipRect(lo,hi,true);
            if(node.stage==Stage::Nr)draw->AddLine({lo.x+font*.4f,hi.y-2},{hi.x-font*.4f,hi.y-2},ImGui::GetColorU32(accent),2);
            const float headerHeight=font*1.7f;
            const auto band=Neurotic::Sleek::Mix(fill,ImGui::GetStyleColorVec4(ImGuiCol_Text),.035f);
            draw->AddRectFilled(lo,{hi.x,lo.y+headerHeight},ImGui::GetColorU32(band),ImGui::GetStyle().FrameRounding,ImDrawFlags_RoundCornersTop);
            draw->AddLine({lo.x,lo.y+headerHeight},{hi.x,lo.y+headerHeight},ImGui::GetColorU32(ImGuiCol_Border),1);
            const float titleY=(headerHeight-titleSize)*.5f;
            Icon(draw,node.stage,{lo.x+font*.4f,lo.y+titleY},titleSize,ImGui::GetColorU32(node.stage==Stage::Nr?accent:ImGui::GetStyleColorVec4(ImGuiCol_Text)));
            pushTitleFont();
            line(name,titleY,ImGui::GetColorU32(ImGuiCol_Text),1.7f);
            ImGui::PopFont();
            auto stateText=ActivityText(node);if(stateText.empty())stateText=StateText(node.state);
            line(std::string(Mark(node.state))+" "+stateText,font*2,ImGui::GetColorU32(Color(node.state)));
            const auto& provider=readouts.provider[index];
            if(detailAmount>.01f)line(provider.empty()?"Not reported":provider,font*3.7f,ImGui::GetColorU32(ImGuiCol_TextDisabled));
            draw->PopClipRect();
            if(i+1<count){
                // Flow represents completed NR output, not use of every optional stage.
                const bool flowing=facts.enabled&&facts.active&&!facts.blocked&&node.ordered&&visible[i+1].ordered;
                for(int chevron=0;chevron<3;++chevron){
                    auto arrow=Color(flowing?State::Used:State::Unknown);
                    if(flowing&&!reducedMotion)arrow.w*=.78f+.14f*float(std::sin(ImGui::GetTime()*2.4-double(i)*.65-double(chevron)*.6));
                    Neurotic::Sleek::Chevron(draw,{hi.x-font*(1.65f-.5f*chevron),hi.y-font*.6f},font*.85f,ImGui::GetColorU32(arrow),0);
                }
            }
            if(Neurotic::ItemHelpHovered()) {ImGui::BeginTooltip();ImGui::PushTextWrapPos((std::min)(font*27,(std::max)(font*8,ImGui::GetMainViewport()->WorkSize.x-font*3)));ImGui::TextUnformatted(name.c_str());if(!detail){ImGui::TextUnformatted(provider.empty()?"Provider not reported":provider.c_str());if(node.stage==Stage::Nr)ImGui::TextUnformatted(PlacementText(m.placement).c_str());}ImGui::TextUnformatted(Detail(node.stage,m.rrIntegrated).c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();}
        }
        ImGui::SetCursorPos({origin.x+count*(width+gap)-gap,origin.y+height});ImGui::Dummy({1,1});
        ImGui::EndDisabled();ImGui::EndChild();ImGui::PopStyleVar();
        ImGui::SetCursorPosY(railTop+railHeight);
    }
    return selected;
}
}
