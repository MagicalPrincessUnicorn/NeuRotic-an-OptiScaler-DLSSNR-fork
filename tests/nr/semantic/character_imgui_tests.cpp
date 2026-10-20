#include "nr/semantic/character/CharacterInspectorDraw.h"
#include "nr/semantic/character/CharacterInspectorMenu.h"
#include "nr/semantic/character/CharacterInspectorOverlay.h"
#include <imgui/imgui_internal.h>
#include <iostream>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
namespace Neurotic::Semantic::Character {
// UI-only test host: process/capture behavior has separate native tests.
static int workerStarts=0,workerStops=0;
void StopCharacterWorker() noexcept {++workerStops;}
void StartCharacterWorkerAtDefaultLocation() {++workerStarts;}
RuntimeView TryCharacterRuntimeView(){return {};}
bool CharacterWorkerRequested() noexcept{return false;}
}
static int checks=0;
static void Check(bool ok,const char* why) {++checks;if(!ok)throw std::runtime_error(why);}
struct MenuFixture {
    CustomOptional<bool> CharacterInspectorEnabled { false };
    CustomOptional<std::string> CharacterInspectorProvider { "auto" };
    CustomOptional<bool> CharacterInspectorBodyBoxes { true };
    CustomOptional<bool> CharacterInspectorTorsoEstimate { false };
    CustomOptional<uint32_t> CharacterInspectorMaximumPersons { 4 };
    CustomOptional<uint32_t> CharacterInspectorMaximumLabels { 8 };
    CustomOptional<uint32_t> CharacterInspectorBoxHoldMs { 80 };
    CustomOptional<uint32_t> CharacterInspectorBoxThickness { 2 };
    CustomOptional<uint32_t> CharacterInspectorLabelScalePercent { 100 };
    CustomOptional<uint32_t> CharacterInspectorUpdateIntervalMs { 33 };
    CustomOptional<bool> CharacterInspectorAutoLabelScale { true };
    CustomOptional<bool> CharacterInspectorSmartBoxHandoff { true };
    CustomOptional<bool> CharacterInspectorDetectObjects { true };
};
int main() {
 try {
    ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.IniFilename=nullptr;
    io.DisplaySize={1920,1080}; io.DeltaTime=1.0f/60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    ImGui::NewFrame(); auto& list=*ImGui::GetBackgroundDrawList();
    auto fixture=MakeCharacterFixture();
    DrawStyle style{};style.color=0xffffffff;style.estimatedColor=0xff00ffff;
    style.maxLabels=0; ViewportRect viewport{100,50,600,400};
    const auto clipCount=list._ClipRectStack.Size;
    const auto base=list.VtxBuffer.Size;
    Check(DrawFixtureObservation(list,fixture,viewport,style,false)==Admission::Disabled,"disabled fixture admission");
    Check(list.VtxBuffer.Size==base,"off creates no geometry");
    Check(DrawFixtureObservation(list,fixture,viewport,style,true)==Admission::Ready,"fixture canvas admission");
    Check(list.VtxBuffer.Size>base && list._ClipRectStack.Size==clipCount,"actual ImGui vertices and balanced clips");
    bool canvasClip=false;
    for(const auto& command:list.CmdBuffer) {
        const auto clip=command.ClipRect;
        if(command.ElemCount && clip.x==100 && clip.y==50 && clip.z==700 && clip.w==450) canvasClip=true;
    }
    Check(canvasClip,"nonzero canvas clip on actual emitted commands");
    Check(list.VtxBuffer[base].pos.x>170 && list.VtxBuffer[base].pos.x<176,"normalized source mapped to viewport origin");
    const auto fixtureEnd=list.VtxBuffer.Size;
    DisplayContext display{{1,2,3,4,5,6,7,8},12,1'100'000'000,true,false,false};
    Check(DrawCharacterOverlay(list,fixture,display,viewport,style)==Admission::NotLive,"fixture cannot be relabeled by live draw");
    Check(list.VtxBuffer.Size==fixtureEnd,"rejected provenance emits no vertices");
    auto live=fixture;live.origin=Origin::LiveModel;live.key={display.epoch,10,1'000'000'000};
    display.enabled=false;
    Check(DrawCharacterOverlay(list,live,display,viewport,style)==Admission::Disabled,"live disabled");
    Check(list.VtxBuffer.Size==fixtureEnd,"disabled live path emits zero geometry");
    display.enabled=true;display.fgActive=true;
    Check(DrawCharacterOverlay(list,live,display,viewport,style)==Admission::UnqualifiedFg,"FG active revokes display");
    display.fgActive=false;display.nowNs=1'150'000'001;
    Check(DrawCharacterOverlay(list,live,display,viewport,style)==Admission::Stale,"age cutoff");
    display.nowNs=1'100'000'000;viewport.width=0;
    Check(DrawCharacterOverlay(list,live,display,viewport,style)==Admission::Malformed,"minimized canvas emits no geometry");
    viewport.width=600; style.thickness=std::numeric_limits<float>::quiet_NaN();
    Check(DrawCharacterOverlay(list,live,display,viewport,style)==Admission::Malformed,"malformed style refused");
    style.thickness=1.5f;live.count=0; const auto beforeEmpty=list.VtxBuffer.Size;
    Check(DrawCharacterOverlay(list,live,display,viewport,style)==Admission::Ready,"successful no match");
    Check(list.VtxBuffer.Size==beforeEmpty,"no-match emits no ghost boxes");
    fixture.count=16;
    for(std::size_t i=2;i<16;++i) {fixture.items[i]=fixture.items[0];fixture.items[i].id=i+1;}
    style.bodyBoxes=false;style.maxLabels=0;
    const auto noLabelStart=list.VtxBuffer.Size;
    Check(DrawFixtureObservation(list,fixture,viewport,style,true)==Admission::Ready,"boxes/labels independently disabled");
    Check(list.VtxBuffer.Size==noLabelStart,"no unwanted geometry when both display controls off");
    style.maxLabels=2;
    Check(DrawFixtureObservation(list,fixture,viewport,style,true)==Admission::Ready,"bounded fixture labels");
    const auto twoLabelVertices=list.VtxBuffer.Size-noLabelStart;
    Check(twoLabelVertices>0,"two labels create actual glyph vertices");
    fixture.count=2; const auto twoStart=list.VtxBuffer.Size;
    DrawFixtureObservation(list,fixture,viewport,style,true);
    Check(list.VtxBuffer.Size-twoStart==twoLabelVertices,"sixteen observations still draw only two labels");
    auto crowd=fixture;crowd.count=16;
    for(std::size_t i=0;i<16;++i){crowd.items[i]=fixture.items[0];crowd.items[i].id=i+1;crowd.items[i].body={.01+i*.06,.2,.05+i*.06,.9};}
    style.bodyBoxes=true;style.maxLabels=0;const auto crowdStart=list.IdxBuffer.Size;
    DrawFixtureObservation(list,crowd,viewport,style,true);const auto crowdIndices=list.IdxBuffer.Size-crowdStart;
    crowd.count=1;const auto oneStart=list.IdxBuffer.Size;DrawFixtureObservation(list,crowd,viewport,style,true);
    Check(crowdIndices==16*(list.IdxBuffer.Size-oneStart)&&crowdIndices>0,"all sixteen boxes emit actual ImGui rectangle geometry");
    style.bodyBoxes=false;
    fixture.count=1;style.maxLabels=1;
    const auto glyphHeight=[&](float fontSize) {
        style.labelHeight=fontSize;const auto begin=list.VtxBuffer.Size;
        DrawFixtureObservation(list,fixture,viewport,style,true);
        float low=1e6f,high=-1e6f;
        for(int i=begin;i<list.VtxBuffer.Size;++i){if(list.VtxBuffer[i].uv.x==list._Data->TexUvWhitePixel.x&&list.VtxBuffer[i].uv.y==list._Data->TexUvWhitePixel.y)continue;low=std::min(low,list.VtxBuffer[i].pos.y);high=std::max(high,list.VtxBuffer[i].pos.y);}
        return high-low;
    };
    const auto smallGlyphs=glyphHeight(16),largeGlyphs=glyphHeight(32);
    Check(smallGlyphs>0 && largeGlyphs>smallGlyphs*1.7f,"4K label size enlarges actual glyphs, not only their position");
    fixture.count=2;style.labelHeight=16;
    for(float scale:{.5f,1.f,2.f}) {
        io.DisplayFramebufferScale={scale,scale}; viewport={20,30,300*scale,200*scale};
        style.bodyBoxes=true;style.maxLabels=0;
        Check(DrawFixtureObservation(list,fixture,viewport,style,true)==Admission::Ready,"scale variation accepted in UI coordinates");
    }
    Check(!io.WantCaptureKeyboard && !io.WantCaptureMouse,"display-only draw does not acquire gameplay input");
    InspectorSettings scaling{};ImGuiViewport output{};output.Size={1920,1080};io.DisplayFramebufferScale={1,1};
    Check(InspectorDrawStyle(scaling,output).labelHeight==16,"1080 output uses 16px labels");
    output.Size={3840,2160};Check(InspectorDrawStyle(scaling,output).labelHeight==32,"4K output uses 32px labels");
    output.Size={1920,1080};io.DisplayFramebufferScale={2,2};Check(InspectorDrawStyle(scaling,output).labelHeight==16,"4K framebuffer at double DPI scales only once");
    scaling.labelScalePercent=150;Check(InspectorDrawStyle(scaling,output).labelHeight==24,"manual scale multiplies automatic physical font size");
    output.Size={960,540};scaling.labelScalePercent=50;
    Check(InspectorDrawStyle(scaling,output).labelHeight==4,"50 percent at double DPI preserves 8 physical pixels");
    output.Size={960,540};io.DisplayFramebufferScale={4,4};
    Check(InspectorDrawStyle(scaling,output).labelHeight==4,"4K at quadruple DPI preserves 16 physical pixels");
    io.DisplayFramebufferScale={1,1};
    // Full game preview must use the game viewport even with no settings window.
    auto& preview=InspectorPreviewSession();
    InspectorSettings overlaySettings{};overlaySettings.enabled=true;
    auto* mainViewport=ImGui::GetMainViewport();
    mainViewport->Pos={80,40};mainViewport->Size={1280,720};
    auto& gameList=*ImGui::GetBackgroundDrawList(mainViewport);
    const auto gameStart=gameList.VtxBuffer.Size;
    Check(!NeedsInspectorFixtureOverlay(overlaySettings,*mainViewport),"viewport preview defaults off");
    Check(RenderInspectorFixtureOverlay(overlaySettings,*mainViewport)==Admission::Disabled,"no implicit game overlay");
    Check(gameList.VtxBuffer.Size==gameStart,"off viewport preview emits nothing");
    preview.fullGame=true;
    Check(NeedsInspectorFixtureOverlay(overlaySettings,*mainViewport),"explicit preview demands frames while menu hidden");
    Check(RenderInspectorFixtureOverlay(overlaySettings,*mainViewport)==Admission::Ready,"full viewport draw admitted without menu window");
    Check(gameList.VtxBuffer.Size>gameStart,"background game draw list receives geometry");
    bool gameClip=false;
    for(const auto& command:gameList.CmdBuffer)
        if(command.ElemCount && command.ClipRect.x==80 && command.ClipRect.y==40 &&
           command.ClipRect.z==1360 && command.ClipRect.w==760) gameClip=true;
    Check(gameClip,"full viewport origin and extent used, not menu child");
    const auto gameEnd=gameList.VtxBuffer.Size;
    overlaySettings.enabled=false;
    Check(!NeedsInspectorFixtureOverlay(overlaySettings,*mainViewport),"inspector off removes hidden-menu frame demand");
    RenderInspectorFixtureOverlay(overlaySettings,*mainViewport);
    Check(gameList.VtxBuffer.Size==gameEnd,"inspector off adds no viewport geometry");
    overlaySettings.enabled=true;mainViewport->Size.x=0;
    Check(!NeedsInspectorFixtureOverlay(overlaySettings,*mainViewport),"minimize suspends preview drawing");
    Check(WantsInspectorFixtureOverlay(overlaySettings),"restore can poll platform size even after a zero-size viewport");
    mainViewport->Size={1280,720};
    live=MakeCharacterFixture();live.origin=Origin::LiveModel;live.key={display.epoch,10,1'000'000'000};display.nowNs=1'100'000'000;
    const auto liveStart=gameList.VtxBuffer.Size;
    Check(RenderInspectorLiveOverlay(live,display,overlaySettings,*mainViewport)==Admission::Ready,"live observation draws in whole game viewport");
    Check(gameList.VtxBuffer.Size>liveStart,"live background geometry exists while menu hidden");
    display.nowNs=1'150'000'001;const auto liveEnd=gameList.VtxBuffer.Size;
    Check(RenderInspectorLiveOverlay(live,display,overlaySettings,*mainViewport)==Admission::Stale,"live viewport enforces capture TTL");
    Check(gameList.VtxBuffer.Size==liveEnd,"stale live viewport emits no geometry");
    BoxHold heldBoxes;HeldSnapshot held;
    Check(heldBoxes.Update(live,1'100'000'000,500),"two live people accepted into hold cache");
    display.nowNs=1'400'000'000;
    Check(heldBoxes.Read(display,500,held)&&held.geometry.count==2,"both people remain after ordinary TTL");
    overlaySettings.boxHoldMs=500;
    overlaySettings.maximumLabels=0;overlaySettings.torsoEstimate=false;
    const auto heldStart=gameList.IdxBuffer.Size;
    Check(RenderInspectorLiveOverlay(held,display,overlaySettings,*mainViewport)==Admission::Ready,"held people draw through actual whole-game ImGui path");
    const auto twoRects=gameList.IdxBuffer.Size-heldStart;
    auto singleHeld=held;singleHeld.geometry.count=1;const auto singleStart=gameList.IdxBuffer.Size;
    RenderInspectorLiveOverlay(singleHeld,display,overlaySettings,*mainViewport);
    Check(twoRects==2*(gameList.IdxBuffer.Size-singleStart)&&twoRects>0,"two people emit two rectangles even with labels disabled");
    display.fgActive=true;const auto refusedStart=gameList.IdxBuffer.Size;
    Check(RenderInspectorLiveOverlay(held,display,overlaySettings,*mainViewport)==Admission::UnqualifiedFg,"FG refuses held geometry");
    Check(gameList.IdxBuffer.Size==refusedStart,"refused hold emits no vertices");display.fgActive=false;
    // Six physical outputs reuse identical tracked geometry even with velocity.
    // A new worker reply, not elapsed presentation time, updates FG boxes.
    display.fgActive=true;display.postFgQualified=true;display.nowNs=1'420'000'000;
    held.geometry.key.captureNs=1'400'000'000;
    for(std::size_t i=0;i<held.geometry.count;++i){
        held.sources[i]=held.geometry.key;held.receivedNs[i]=1'410'000'000;
        held.geometry.items[i].motionTracked=true;held.geometry.items[i].confirmedNs=1'400'000'000;
        held.geometry.items[i].poseNs=1'400'000'000;
        held.geometry.items[i].velocity={.8,.4};
    }
    const auto repeatedStart=gameList.VtxBuffer.Size;
    Check(RenderInspectorLiveOverlay(held,display,overlaySettings,*mainViewport)==Admission::Ready,"qualified physical FG draws");
    const auto repeatedSize=gameList.VtxBuffer.Size-repeatedStart;
    for(unsigned output=1;output<6;++output){
        display.nowNs+=2'000'000;const auto begin=gameList.VtxBuffer.Size;
        Check(RenderInspectorLiveOverlay(held,display,overlaySettings,*mainViewport)==Admission::Ready,"MFG repeated output admitted");
        Check(gameList.VtxBuffer.Size-begin==repeatedSize,"MFG repeated vertex count");
        bool same=true;for(int i=0;i<repeatedSize;++i){
            const auto& first=gameList.VtxBuffer[repeatedStart+i].pos;const auto& next=gameList.VtxBuffer[begin+i].pos;
            same=same&&first.x==next.x&&first.y==next.y;
        }
        Check(same,"MFG boxes do not extrapolate between worker replies");
    }
    auto expiredPose=held.geometry;expiredPose.items[0].poseNs=1'150'000'000;
    const auto sanitized=PredictTrackedGeometry(expiredPose,display.nowNs,false);
    Check(!sanitized.items[0].hasPose&&sanitized.items[0].body.x0==expiredPose.items[0].body.x0,"FG freezes body but still expires old pose");
    display.nowNs=1'510'000'000;
    Check(RenderInspectorLiveOverlay(held,display,overlaySettings,*mainViewport)==Admission::Stale,"MFG repetition does not extend stale lifetime");
    display.fgActive=false;display.postFgQualified=false;
    Check(!io.WantCaptureMouse && !io.WantCaptureKeyboard,"full game draw leaves gameplay input free");
    ResetInspectorPreviewSession();
    Check(!NeedsInspectorFixtureOverlay(overlaySettings,*mainViewport),"context teardown clears session preview");
    MenuFixture menu;
    ImGui::SetNextWindowSize({600,700});
    ImGui::Begin("Existing settings menu");
    ImGui::SetNextItemOpen(true);
    const float disabledTop=ImGui::GetCursorPosY();
    ImGui::PushID("Disabled settings");
    RenderInspectorMenu(menu);
    ImGui::PopID();
    Check(!ReadSettings(menu).enabled,"visiting panel does not enable inspector");
    bool detectionCard=false,overlayCard=false;for(auto* child:GImGui->Windows)if(child->Active){const std::string name=child->Name;detectionCard|=name.find("##InspectorDetection")!=std::string::npos;overlayCard|=name.find("##InspectorOverlay")!=std::string::npos;}
    Check(detectionCard&&overlayCard,"disabled inspector keeps Detection and Overlay cards exposed");
    Check(workerStarts==0,"visiting disabled inspector never starts a worker");
    menu.CharacterInspectorEnabled=true;
    ImGui::SetNextItemOpen(true);
    ImGui::PushID("Enabled settings");
    RenderInspectorMenu(menu);
    ImGui::PopID();
    Check(ReadSettings(menu).provider=="auto","enabled panel does not invent an installed provider");
    ImGui::End();
    ImGui::EndFrame();ImGui::Render();
    Check(ImGui::GetDrawData()->TotalVtxCount>0,"one existing frame finalized");
    InspectorPreviewSession().fullGame=true;
    io.DisplaySize={800,600};
    ImGui::NewFrame();
    auto* resizedViewport=ImGui::GetMainViewport();
    auto& resizedList=*ImGui::GetBackgroundDrawList(resizedViewport);
    const auto resizedStart=resizedList.VtxBuffer.Size;
    Check(NeedsInspectorFixtureOverlay(ReadSettings(menu),*resizedViewport),"preview demand survives closing the menu");
    Check(RenderInspectorFixtureOverlay(ReadSettings(menu),*resizedViewport)==Admission::Ready,"second frame draws with no menu window");
    Check(resizedList.VtxBuffer[resizedStart].pos.x>93 && resizedList.VtxBuffer[resizedStart].pos.x<99,"boxes remap after viewport resize");
    ImGui::EndFrame();ImGui::Render();
    Check(ImGui::GetDrawData()->TotalVtxCount>0,"hidden menu overlay finalized by same frame owner");
    Check(!io.WantCaptureMouse && !io.WantCaptureKeyboard,"closed-menu overlay requests no input");
    ResetInspectorPreviewSession();
    menu.CharacterInspectorMaximumPersons=15;
    menu.CharacterInspectorDetectObjects=false;

    ImVec2 resetPoint{};
    const auto resetFrame=[&](int control) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({700,500});
        ImGui::Begin("Inspector reset interaction");
        if(control==0) InspectorSlider("Maximum detections",menu.CharacterInspectorMaximumPersons,1,16);
        if(control==1) InspectorToggle("People and objects",menu.CharacterInspectorDetectObjects);

        resetPoint=(ImGui::GetItemRectMin()+ImGui::GetItemRectMax())*.5f;
        ImGui::End();ImGui::Render();
    };
    for(int control=0;control<2;++control) {
        io.AddMousePosEvent(-100,-100);resetFrame(control);
        io.AddMousePosEvent(resetPoint.x,resetPoint.y);resetFrame(control);
        io.AddMouseButtonEvent(0,true);resetFrame(control);
        io.AddMouseButtonEvent(0,false);resetFrame(control);
    }
    Check(menu.CharacterInspectorMaximumPersons.value_or_default()==4 && !menu.CharacterInspectorMaximumPersons.has_value(),"slider Reset restores configured default and removes override");
    Check(menu.CharacterInspectorDetectObjects.value_or_default() && !menu.CharacterInspectorDetectObjects.has_value(),"toggle Reset restores true default");

    Check(workerStarts==0,"editing and resetting inspector settings never starts capture");
    for(float size:{13.f,26.f})for(float width:{350.f,700.f,1100.f})for(int frame=0;frame<5;++frame) {
        ImGui::NewFrame();Neurotic::Sleek::Scope scope(true);ImGui::PushFont(nullptr,size);
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,700});ImGui::Begin("Inspector responsive controls");
        InspectorPreviewSession()={true,true};RenderInspectorMenu(menu,false);
        Check(!InspectorPreviewSession().canvas&&!InspectorPreviewSession().fullGame,"retired session choices clear on panel visit");
        Check(workerStarts==0,"responsive inspector does not start capture");
        if(frame==4)for(auto* child:GImGui->Windows)if(child->Active&&child->ParentWindow==ImGui::GetCurrentWindow()) {
            if(child->ScrollMax.x!=0)std::cerr<<"overflow "<<child->Name<<" size="<<size<<" width="<<width<<" card="<<child->Size.x<<" content="<<child->ContentSize.x<<" current="<<(child->DC.CursorMaxPos.x-child->DC.CursorStartPos.x)<<" scroll="<<child->ScrollMax.x<<"\\n";
            Check(child->ScrollMax.x==0,"Inspector cards do not clip controls horizontally");
        }
        ImGui::End();ImGui::PopFont();ImGui::Render();
        Check(GImGui->ColorStack.empty()&&GImGui->StyleVarStack.empty(),"Inspector cards restore product theme stacks");
    }
    // Explicit activation starts once; ordinary redraws and saved On do not.
    MenuFixture activation;ImGuiID enableId=0,retiredResetId=0;
    const auto activationFrame=[&](ImGuiID action=0){
        ImGui::NewFrame();Neurotic::Sleek::Scope scope(true);ImGui::SetNextWindowSize({900,700});
        ImGui::Begin("Inspector activation flow");
        if(action)GImGui->NavActivateId=GImGui->NavActivateDownId=GImGui->NavActivatePressedId=action;
        RenderInspectorMenu(activation,false);
        for(auto* window:GImGui->Windows)if(window->Active&&std::string(window->Name).find("Inspector activation flow/##InspectorSession")!=std::string::npos){
            const auto scopeId=ImHashStr("Enable Character Inspector",0,window->ID);
            enableId=ImHashStr("##Value",0,scopeId);retiredResetId=ImHashStr("Reset",0,scopeId);
        }
        ImGui::End();ImGui::Render();
    };
    activationFrame();activationFrame();Check(enableId!=0,"activation control is exposed in the session card");
    const int startsBefore=workerStarts;
    activationFrame(enableId);Check(activation.CharacterInspectorEnabled.value_or_default()&&workerStarts==startsBefore+1,"enabling immediately requests live inspection once");
    activationFrame();activationFrame();Check(workerStarts==startsBefore+1,"redrawing enabled settings does not restart the worker");
    activationFrame(retiredResetId);Check(activation.CharacterInspectorEnabled.value_or_default()&&workerStarts==startsBefore+1,"activation has no Reset action");
    const int stopsBefore=workerStops;activationFrame(enableId);
    Check(!activation.CharacterInspectorEnabled.value_or_default()&&workerStops>stopsBefore,"disabling stops inspection");
    activation.CharacterInspectorEnabled=true;activationFrame();
    Check(workerStarts==startsBefore+1&&std::string(InspectorSessionStatus({},true))=="Enabled, not running","saved On does not silently start capture or claim a running worker");
    ImGui::DestroyContext();
    std::cout << "PASS " << checks << " actual ImGui checks (no GPU/game)\n";return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';ImGui::DestroyContext();return 1;}
}
