#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "menu/Localization.h"
#if __has_include("menu/SleekUi.h")
#include "menu/SleekUi.h"
#include "menu/SleekShell.h"
#include "menu/ExperimentalMfgControls.h"
#include "menu/MfgRestartWarning.h"
#include "menu/SleekSections.h"
#include "menu/SleekContentCard.h"
#include "dlssnr/DlssNr_MenuStatus.h"
#include "dlssnr/DlssNr_MenuControls.h"
#include "dlssnr/NrStatusPanel.h"
#include "dlssnr/DlssNr_MenuStatus.h"
#define HAS_SLEEK_UI 1
#else
#define HAS_SLEEK_UI 0
#endif
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <cstdlib>
#include <crtdbg.h>
static int checks = 0;
static void Check(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
    ++checks;
}

int main()
{
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    try
    {
        DlssNr::MenuStatus::TimingObservation timing;
        Check(!timing.Update(1,1,true,{},0),"timing starts unavailable");
        Check(timing.Update(1,1,true,1.25,0.1)==1.25,"completed timing becomes visible");
        Check(timing.Update(1,1,true,{},0.8)==1.25,"async query gaps retain the last recent measurement");
        Check(!timing.Update(1,1,true,{},1.2),"timing expires after one second without a sample");
        timing.Update(1,1,true,2.0,2.0);
        Check(!timing.Update(2,1,true,{},2.1),"selection changes clear old timing");
        timing.Update(2,1,true,2.0,2.2);
        Check(!timing.Update(2,2,true,{},2.3),"owner recreation clears old timing");
        timing.Update(2,2,true,2.0,2.4);
        Check(!timing.Update(2,2,false,{},2.5),"inactive or refused output clears timing");
        Check(!timing.Update(2,2,true,NAN,2.6),"invalid timing is never displayed");
        timing.Update(2,2,true,2.0,3.0);
        Check(!timing.Update(2,2,true,{},2.9),"clock reset clears old timing");
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = io.LogFilename = nullptr;
        io.DisplaySize = {1280, 900};
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.Fonts->AddFontDefault();
        bool enabled = false, disabled = false;
        int mixed=1;
        ImRect toggle;
        ImRect disabledRect,mixedRect;
        ImGuiID toggleId = 0;
        float minimumToggleWidth = 0;
        auto frame = [&]()
        {
            ImGui::NewFrame();
#if HAS_SLEEK_UI
            Neurotic::Sleek::Scope scope;
#endif
            ImGui::SetNextWindowPos({0,0});
            ImGui::SetNextWindowSize({800,600});
            ImGui::Begin("Motion test###sleek-test", nullptr, ImGuiWindowFlags_NoTitleBar);
            ImGui::Button("Save Settings");
            ImGui::Checkbox("Enable Neural Rendering", &enabled);
            toggle = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
            toggleId = GImGui->LastItemData.ID;
            minimumToggleWidth = ImGui::CalcTextSize("Enable Neural Rendering").x +
                ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetFrameHeight() * 1.5f;
            ImGui::BeginDisabled();
            ImGui::Checkbox("Disabled", &disabled);
            disabledRect={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
            ImGui::EndDisabled();
            ImGui::CheckboxFlags("Mixed flags",&mixed,3);
            mixedRect={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
            ImGui::End();
            ImGui::Render();
        };
        frame();
        // A stock square checkbox cannot satisfy this: the primary controls must be switches.
        Check(toggle.GetWidth() > minimumToggleWidth,
              "sleek controls use a wide switch track with an intact label hit target");
        const auto stableId = toggleId;
        io.AddMousePosEvent(toggle.Min.x + 5, toggle.GetCenter().y);
        frame();
        io.AddMouseButtonEvent(0, true); frame();
        Check(!enabled, "switch does not commit on mouse down");
        io.AddMouseButtonEvent(0, false); frame();
        Check(enabled, "switch commits exactly once on release");
        frame(); Check(enabled, "animation does not reapply the edit");
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMousePosEvent(790,590); frame();
        io.AddMouseButtonEvent(0,false); frame();
        Check(enabled, "release outside the switch does not change the setting");
        Neurotic::SetLanguage("de"); frame();
        Check(toggleId == stableId, "localized switch preserves native identity");
#if HAS_SLEEK_UI
        using namespace Neurotic::Sleek;
        Check(std::string(Pages[1])=="Upscaling" && std::string(Pages[2])=="Neural Rendering",
              "final navigation puts Upscaling before Neural Rendering");
        Check(PageCount==7, "navigation contains all seven pages");
        float collapsedHeight=0,compactLimit=0;
        for(int pass=0;pass<3;++pass) {
            ImGui::NewFrame();Scope scope(true);
            ImGui::SetNextWindowSize({800,700});ImGui::Begin("Compact disclosure budget");
            compactLimit=ImGui::GetFrameHeight()+ImGui::GetStyle().ItemSpacing.y;
            { ScopedCollapsingHeader section("Closed disclosure"); }
            collapsedHeight=ImGui::GetItemRectSize().y;
            ImGui::End();ImGui::Render();
        }
        Check(collapsedHeight<=compactLimit+1,"closed disclosures retain the approved flat height budget");
        // Actual child-card bounds expose redundant legacy Spacing() between siblings.
        float tightGap=0,extraGap=0,contentGap=0;
        const auto sectionGapFrame=[&]() {
            ImGui::NewFrame();Scope scope(true);
            ImGui::SetNextWindowSize({800,700});ImGui::Begin("Section rhythm###section-rhythm");
            { ScopedCollapsingHeader a("Comparison screenshots"); }
            const float firstBottom=ImGui::GetItemRectMax().y;
            { ScopedCollapsingHeader b("Interface brightness"); }
            tightGap=ImGui::GetItemRectMin().y-firstBottom;
            const float secondBottom=ImGui::GetItemRectMax().y;
            ImGui::Spacing();ImGui::Spacing();
            { ScopedCollapsingHeader c("Magnifier"); }
            extraGap=ImGui::GetItemRectMin().y-secondBottom;
            ImGui::Button("A real action between sections");
            const float actionBottom=ImGui::GetItemRectMax().y;
            ImGui::Spacing();
            { ScopedCollapsingHeader d("MipMap Bias"); }
            contentGap=ImGui::GetItemRectMin().y-actionBottom;
            ImGui::End();ImGui::Render();
        };
        sectionGapFrame();sectionGapFrame();
        std::printf("Section gap trace: tight=%.3f legacy=%.3f content=%.3f px\n",tightGap,extraGap,contentGap);
        Check(std::abs(extraGap-tightGap)<.1f,"sibling sections use the same tight gap despite redundant legacy spacer rows");
        Check(contentGap>=tightGap*2-.1f,"section rhythm preserves deliberate spacing after a real intervening control");
        // Same card rhythm with zero through three old spacer rows, whether the
        // bodies are open or closed. Real separators and other flows stay intact.
        for(bool open : {false,true}) for(int spacers=0;spacers<=3;++spacers) {
            float gap=0,separatorGap=0;
            auto rhythmFrame=[&]() {
                ImGui::NewFrame();Scope scope(true);
                ImGui::SetNextWindowSize({800,700});ImGui::Begin("Rhythm matrix###rhythm-matrix");
                ImGui::SetNextItemOpen(open,ImGuiCond_Always);
                { ScopedCollapsingHeader a("First card");if(a.IsHeaderOpen()) ImGui::TextUnformatted("Control"); }
                const float bottom=ImGui::GetItemRectMax().y;
                for(int i=0;i<spacers;++i) ImGui::Spacing();
                ImGui::SetNextItemOpen(open,ImGuiCond_Always);
                { ScopedCollapsingHeader b("Second card");if(b.IsHeaderOpen()) ImGui::TextUnformatted("Control"); }
                gap=ImGui::GetItemRectMin().y-bottom;
                ImGui::Separator();const float separatorBottom=ImGui::GetItemRectMax().y;
                ImGui::Spacing();ImGui::Spacing();
                { ScopedCollapsingHeader c("After separator"); }
                separatorGap=ImGui::GetItemRectMin().y-separatorBottom;
                ImGui::End();ImGui::Render();
            };
            rhythmFrame();rhythmFrame();
            Check(std::abs(gap-ImGui::GetStyle().ItemSpacing.y)<.1f,"open and closed sibling cards share one native item gap");
            Check(separatorGap>gap,"real separators preserve their own following spacing");
        }
        // Exercise production measured-height regions; animation must change layout, not just ink.
        bool sectionOpen=false, nested=false, hiddenEdit=false;
        float regionHeight=0;
        auto regionFrame=[&](bool reduce=false) {
            ImGui::NewFrame(); Scope scope(reduce);
            ImGui::SetNextWindowSize({800,600});
            ImGui::Begin("Sections###polish-test",nullptr,ImGuiWindowFlags_NoTitleBar);
            const float before=ImGui::GetCursorPosY();
            {
                AnimatedRegion region("##Reveal",sectionOpen);
                if(region.Visible()) {
                    ImGui::TextUnformatted("Additional controls");
                    ImGui::Checkbox("Hidden edit",&hiddenEdit);
                    if(nested) for(int i=0;i<4;++i) ImGui::Text("Nested option %d",i);
                }
            }
            regionHeight=ImGui::GetCursorPosY()-before;
            ImGui::End(); ImGui::Render();
        };
        regionFrame(); Check(regionHeight==0,"closed regions consume no row or padding");
        sectionOpen=true; regionFrame(); const float openingStart=regionHeight;
        regionFrame(); Check(regionHeight>openingStart,"opening region advances toward measured content height");
        for(int i=0;i<45;++i) regionFrame(); const float openHeight=regionHeight;
        Check(openHeight>ImGui::GetFrameHeight(),"fully opened region exposes all controls");
        nested=true; regionFrame(); regionFrame(); const float growingHeight=regionHeight;
        Check(growingHeight>openHeight,"nested conditional controls ease their parent's height");
        for(int i=0;i<45;++i) regionFrame(); const float fullHeight=regionHeight;
        sectionOpen=false;for(int i=0;i<90;++i) regionFrame();
        sectionOpen=true;float revealEarly=0,revealLate=0,revealPrevious=0;int settledFrame=0;
        for(int i=0;i<60;++i) {
            regionFrame();
            const float step=regionHeight-revealPrevious;
            if(regionHeight>fullHeight*.15f && regionHeight<fullHeight*.35f && step>0) revealEarly=step;
            if(regionHeight>fullHeight*.75f && regionHeight<fullHeight*.90f && step>0) revealLate=step;
            if(!settledFrame && std::abs(regionHeight-fullHeight)<.05f) settledFrame=i+1;
            revealPrevious=regionHeight;
        }
        std::printf("Reveal trace: early=%.3f late=%.3f settled=%d frames\n",revealEarly,revealLate,settledFrame);
        Check(revealEarly>0 && revealLate>=revealEarly*.9f,"section reveal retains its speed near full extension");
        Check(settledFrame>1 && settledFrame<=15,"section reveal finishes exactly without a long final-pixel tail");
        sectionOpen=false; regionFrame();
        Check(regionHeight>0 && regionHeight<fullHeight,"collapse retains a shrinking content region");
        regionFrame();const float reversedFrom=regionHeight;
        sectionOpen=true; regionFrame();
        Check(regionHeight>reversedFrom && regionHeight<fullHeight,"rapid reversal continues from the current height");
        sectionOpen=false; for(int i=0;i<90;++i) regionFrame();
        Check(regionHeight==0,"collapse settles without a leftover row");
        // Real child sizing must not pin a closing body at its native minimum,
        // then drop that entire minimum-height row in one frame.
        const auto savedRegionStyle=ImGui::GetStyle();
        ApplyMetrics(ImGui::GetStyle());
        sectionOpen=true; for(int i=0;i<60;++i) regionFrame();
        const float closingExtent=regionHeight;
        sectionOpen=false;
        float previousClosingHeight=regionHeight,lastClosingHeight=regionHeight;
        int pinnedClosingFrames=0;
        for(int i=0;i<90;++i) {
            regionFrame();
            Check(previousClosingHeight-regionHeight<=closingExtent*.12f+1,"closing regions never jump farther than a normal constant-speed frame");
            if(regionHeight>0) {
                if(regionHeight==previousClosingHeight) ++pinnedClosingFrames;
                lastClosingHeight=regionHeight;
            }
            previousClosingHeight=regionHeight;
        }
        std::printf("Collapse trace: pinned=%d final-drop=%.3f px\n",pinnedClosingFrames,lastClosingHeight);
        Check(pinnedClosingFrames<3,"closing sections never pause on the native child minimum before disappearing");
        Check(lastClosingHeight<=closingExtent*.12f+1,"closing section's final step stays within its normal frame travel");
        ImGui::GetStyle()=savedRegionStyle;
        sectionOpen=true; regionFrame(true); regionFrame(true);
        Check(regionHeight==fullHeight,"reduced motion immediately uses the measured full height");
        sectionOpen=false; regionFrame(true);
        Check(regionHeight==0,"reduced motion immediately removes hidden content");
        DlssNr::MenuStatus::ActivityObservation activity;
        Check(!activity.Fresh(1,100,0),"existing counters do not establish live NR activity");
        Check(activity.Fresh(1,101,.1),"new completed work establishes activity");
        Check(!activity.Fresh(1,101,1.2),"paused or stale telemetry clears the running dot");
        Check(!activity.Fresh(2,101,1.3),"changing settings cannot reuse an earlier running dot");
        Check(activity.Fresh(2,102,1.4),"fresh work on the new selection restores activity");
        Check(!activity.Fresh(2,0,1.5),"counter reset clears prior activity");
        bool popupClose=false, completeCopy=false, replacePopup=false,settlePopup=false; int selectedOption=0; bool closingBody=false; ImRect copyBounds{}; ImGuiID copyId=0;float openingFraction=1,openingProgress=1,copyFont=0;
        auto popupFrame=[&]() {
            ImGui::NewFrame(); Scope scope(settlePopup);
            ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({800,600});
            ImGui::Begin("Popups###popup-test");
            FeedbackButton("Copy observation report"); copyId=ImGui::GetItemID();
            copyFont=ImGui::GetFontSize();
            copyBounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
            if(completeCopy) { CompleteButtonFeedback(copyId,true); completeCopy=false; }
            const auto combo=ImGui::GetID("Resolution");
            if(selectedOption==0 && !popupClose) ImGui::OpenPopupEx(ImHashStr("##ComboPopup",0,combo));
            closingBody=false;
            if(ImGui::BeginCombo("Resolution","Match Native")) {
                closingBody=ClosingPopup(ImGui::GetCurrentWindow());
                if(!closingBody) {
                    const auto* window=ImGui::GetCurrentWindow();
                    openingFraction=(window->ClipRect.Max.y-window->Pos.y)/window->Size.y;
                    openingProgress=window->StateStorage.GetFloat(Key(window->ID,26,0),1);
                }
                if(closingBody) Check((GImGui->CurrentItemFlags&ImGuiItemFlags_Disabled)!=0,
                    "closing popup decoration disables all options");
                for(int i=0;i<3;++i) if(ImGui::Selectable(i==0?"Match Native":i==1?"Full Output":"Manual",selectedOption==i)) selectedOption=i;
                if(popupClose && !closingBody) ImGui::CloseCurrentPopup();
                ImGui::EndCombo();
            }
            const auto secondCombo=ImGui::GetID("Style");
            if(replacePopup) ImGui::OpenPopupEx(ImHashStr("##ComboPopup",0,secondCombo));
            if(ImGui::BeginCombo("Style","Cinematic")) {
                if(replacePopup) {
                    Check(!ClosingPopup(ImGui::GetCurrentWindow()),"replacement dropdown owns a real native popup");
                    Check((GImGui->CurrentItemFlags&ImGuiItemFlags_Disabled)==0,"replacement popup remains interactive during the other tail");
                }
                ImGui::Selectable("Cinematic",true);
                ImGui::EndCombo();
            }
            ImGui::End(); ImGui::Render();
        };
        popupFrame();popupFrame(); const ImRect normalCopy=copyBounds;
        Check(openingFraction<.60f,"mouse-opened dropdowns visibly unfold from a compact height");
        float popupEarly=0,popupLate=0,lastFraction=openingFraction;int popupSettled=0;
        for(int i=0;i<20;++i) {
            popupFrame();const float step=openingFraction-lastFraction;
            if(openingFraction>.25f && openingFraction<.50f && step>0) popupEarly=step;
            if(openingFraction>.70f && openingFraction<.90f && step>0) popupLate=step;
            if(!popupSettled && openingProgress==1) popupSettled=i+1;
            lastFraction=openingFraction;
        }
        std::printf("Popup reveal: early=%.3f late=%.3f settled=%d frames\n",popupEarly,popupLate,popupSettled);
        Check(popupEarly>0 && popupLate>=popupEarly*.9f,"selection popups retain their unfolding speed near the bottom");
        Check(popupSettled>0 && popupSettled<=13,"selection popups reach their exact endpoint promptly");
        settlePopup=true;popupFrame();
        Check(openingFraction>.90f,"reduced motion immediately exposes the complete dropdown");
        settlePopup=false;
        GImGui->NavCursorVisible=true;GImGui->NavInputSource=ImGuiInputSource_Keyboard;
        popupFrame();
        Check(openingFraction>.90f,"keyboard dropdown activation keeps every focused option immediately visible");
        GImGui->NavCursorVisible=false;GImGui->NavInputSource=ImGuiInputSource_Mouse;
        popupClose=true; completeCopy=true; popupFrame();
        Check(GImGui->OpenPopupStack.Size==0,"closing a dropdown releases native popup ownership immediately");
        popupFrame();Check(closingBody,"closed dropdown paints a brief collapse tail");
        bool leadingCheck=false;
        for(const auto& vertex : ImGui::FindWindowByName("Popups###popup-test")->DrawList->VtxBuffer) {
            const auto color=ImGui::ColorConvertU32ToFloat4(vertex.col);
            if((vertex.col&~IM_COL32_A_MASK)==(ImGui::GetColorU32(ImVec4(.30f,.82f,.48f,1))&~IM_COL32_A_MASK) && normalCopy.Contains(vertex.pos)) {
                leadingCheck=true;
                Check(vertex.pos.x<normalCopy.Min.x+ImGui::GetStyle().FramePadding.x+copyFont*1.2f,
                    "copy confirmation glyph stays before the caption");
            }
        }
        Check(leadingCheck,"copy confirmation draws a visible leading green check");
        Check(copyBounds.GetSize().x==normalCopy.GetSize().x && copyBounds.GetSize().y==normalCopy.GetSize().y,
            "copy success does not alter button size or surrounding layout");
        for(int i=0;i<20;++i) popupFrame();
        Check(!closingBody,"closed dropdown decoration expires and leaves no popup");
        popupClose=false; popupFrame();popupFrame();popupClose=true;popupFrame();
        replacePopup=true;popupFrame();
        Check(GImGui->OpenPopupStack.Size==1,"new dropdown replaces ownership while an earlier decoration is fading");
        replacePopup=false; ImGui::ClosePopupToLevel(0,true);
        for(int i=0;i<120;++i) popupFrame();
        auto* feedbackWindow=ImGui::FindWindowByName("Popups###popup-test");
        bool green=false;
        for(const auto& vertex:feedbackWindow->DrawList->VtxBuffer) {
            const auto color=ImGui::ColorConvertU32ToFloat4(vertex.col);
            green|=color.y>.75f && color.x<.40f && color.z<.6f;
        }
        Check(!green,"copy feedback expires and normal icon returns without a new row");
        ImGuiID saveFeedbackId=0;ImRect saveFeedbackBounds{};int savePhase=-1;
        const auto saveFeedbackFrame=[&](int transition=-1,bool reduced=false) {
            ImGui::NewFrame();Scope scope(reduced);
            ImGui::SetNextWindowSize({600,300});ImGui::Begin("Save feedback");
            FeedbackButton("Save Settings",FeedbackKind::Save);
            saveFeedbackId=ImGui::GetItemID();saveFeedbackBounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
            if(transition==2) BeginButtonFeedback(saveFeedbackId);
            if(transition==1 || transition==0) CompleteButtonFeedback(saveFeedbackId,transition==1);
            if(transition==3) CancelButtonFeedback(saveFeedbackId);
            savePhase=ImGui::GetCurrentWindow()->StateStorage.GetInt(Key(saveFeedbackId,24,1),-1);
            ImGui::End();ImGui::Render();
        };
        saveFeedbackFrame();const auto originalSaveBounds=saveFeedbackBounds;
        saveFeedbackFrame(2);for(int i=0;i<130;++i) saveFeedbackFrame();
        Check(savePhase==2,"save confirmation stays pending until the transaction completes");
        saveFeedbackFrame(3);saveFeedbackFrame();Check(savePhase==0,"cancelled save confirmation clears pending feedback");
        saveFeedbackFrame(0);saveFeedbackFrame(-1,true);Check(savePhase==0,"failed save cannot display a success check");
        saveFeedbackFrame(1);saveFeedbackFrame(-1,true);Check(savePhase==1,"successful save completes feedback in reduced motion");
        Check(saveFeedbackBounds.GetSize().x==originalSaveBounds.GetSize().x && saveFeedbackBounds.GetSize().y==originalSaveBounds.GetSize().y,
            "save pending, cancel, failure and success preserve button geometry");
        bool headerOpen=false;float contentBottom=0,nextSectionTop=0;
        auto headerFrame=[&](bool reduced) {
            ImGui::NewFrame(); Scope scope(reduced);
            ImGui::SetNextWindowSize({800,600});
            ImGui::Begin("Header layout###header-layout");
            ImGui::SetNextItemOpen(headerOpen,ImGuiCond_Always);
            {
                ScopedCollapsingHeader header("First section");
                if(header.IsHeaderOpen()) {
                    for(int i=0;i<4;++i) ImGui::Text("Option %d",i);
                    contentBottom=ImGui::GetItemRectMax().y;
                }
            }
            nextSectionTop=ImGui::GetCursorScreenPos().y;
            { ScopedCollapsingHeader next("Following section"); }
            ImGui::End(); ImGui::Render();
        };
        headerFrame(true);headerFrame(true);headerOpen=true;headerFrame(true);
        Check(nextSectionTop>=contentBottom,"first instant header expansion reserves its full height before the next section");
        headerOpen=false;headerFrame(true);headerOpen=true;
        GImGui->NavInputSource=ImGuiInputSource_Keyboard;GImGui->NavCursorVisible=true;
        headerFrame(false);
        Check(nextSectionTop>=contentBottom,"keyboard expansion reserves the complete enclosing header layout immediately");
        GImGui->NavInputSource=ImGuiInputSource_Mouse;GImGui->NavCursorVisible=false;
        // Measure the next control's actual position, including the surrounding
        // auto-sized card, rather than only the private region animation value.
        const auto savedHeaderStyle=ImGui::GetStyle();
        ApplyMetrics(ImGui::GetStyle());
        for(float rate : {30.0f,60.0f,120.0f}) {
            io.DeltaTime=1.0f/rate;
            headerOpen=false;for(int i=0;i<120;++i) headerFrame(true);
            const float closedTop=nextSectionTop;
            headerOpen=true;for(int i=0;i<120;++i) headerFrame(false);
            const float cardExtent=nextSectionTop-closedTop;
            headerOpen=false;for(int i=0;i<60;++i) headerFrame(true);
            headerOpen=true;float openingPrior=0,earlyTravel=0,lateTravel=0;int openSettled=0;
            for(int i=0;i<60;++i) {
                headerFrame(false);const float extent=nextSectionTop-closedTop;
                const float step=extent-openingPrior;
                if(extent>cardExtent*.2f && extent<cardExtent*.5f) earlyTravel=step;
                if(extent>cardExtent*.65f && extent<cardExtent*.9f) lateTravel=step;
                if(!openSettled && std::abs(extent-cardExtent)<.05f) openSettled=i+1;
                openingPrior=extent;
            }
            Check(earlyTravel>0 && lateTravel>=earlyTravel-1,"native card opening retains its velocity at every tested frame rate");
            Check(openSettled>0 && openSettled<=static_cast<int>(std::ceil(rate*RevealDuration))+3,"native card opening has an exact prompt endpoint at every tested frame rate");
            std::printf("Card reveal %.0f Hz: early=%.3f late=%.3f settled=%d frames\n",rate,earlyTravel,lateTravel,openSettled);
            headerOpen=false;
            float prior=nextSectionTop-closedTop,last=prior;
            int pinned=0;
            for(int i=0;i<120;++i) {
                headerFrame(false);
                const float remaining=nextSectionTop-closedTop;
                Check(remaining<=prior+1,"closing cards move following controls monotonically upward");
                Check(prior-remaining<=cardExtent/(rate*.15f)+1,"card collapse has no jump beyond its normal frame travel");
                if(remaining>2 && remaining==prior) ++pinned;
                if(remaining>0) last=remaining;
                prior=remaining;
            }
            std::printf("Card collapse %.0f Hz: pinned=%d final-drop=%.3f px\n",rate,pinned,last);
            Check(pinned<3,"surrounding cards do not reintroduce a pause during collapse");
            Check(last<=cardExtent/(rate*.15f)+1,"following section's final frame stays within the regular collapse motion");
        }
        io.DeltaTime=1.0f/60;ImGui::GetStyle()=savedHeaderStyle;
        bool treeOpen=false,renderSibling=true;float treeBottom=0,siblingTop=0,treeCardBottom=0;
        auto treeFrame=[&]() {
            ImGui::NewFrame();Scope scope;
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({800,600});
            ImGui::Begin("Tree collapse###tree-collapse");
            {
                ScopedCollapsingHeader card("Tree parent",ImGuiTreeNodeFlags_DefaultOpen);
                if(card.IsHeaderOpen()) {
                    ImGui::SetNextItemOpen(treeOpen,ImGuiCond_Always);
                    {
                        ScopedTreeNode tree("Nested tree");
                        if(!treeOpen) treeBottom=ImGui::GetItemRectMax().y;
                        if(tree.IsOpen()) for(int i=0;i<4;++i) ImGui::Text("Nested control %d",i);
                    }
                    siblingTop=ImGui::GetCursorScreenPos().y;
                    if(renderSibling) ImGui::Button("Following action");
                }
            }
            treeCardBottom=ImGui::GetItemRectMax().y;
            ImGui::End();ImGui::Render();
        };
        ApplyMetrics(ImGui::GetStyle());
        for(int i=0;i<120;++i) treeFrame();
        Check(siblingTop-treeBottom>=8,"closed trees preserve native spacing before their following sibling");
        const float treeClosedSibling=siblingTop;
        treeOpen=true;for(int i=0;i<120;++i) treeFrame();
        treeOpen=false;float lastTreeSibling=siblingTop-treeClosedSibling;
        float previousSibling=lastTreeSibling,previousStep=FLT_MAX;
        for(int i=0;i<120;++i) {
            treeFrame();
            const float remaining=siblingTop-treeClosedSibling;
            const float step=previousSibling-remaining;
            Check(remaining>=-1 && step>=-1,"closing trees never overlap or move following controls downward");
            Check(step<=previousStep+1,"tree collapse never accelerates at its native sibling-gap boundary");
            previousSibling=remaining;previousStep=step;
            if(siblingTop>treeClosedSibling) lastTreeSibling=siblingTop-treeClosedSibling;
        }
        Check(siblingTop-treeBottom>=8,"tree collapse restores the same closed following-row spacing");
        // Last-item sizing is a separate consumer: no following control reserves
        // its row, so the outer card must lose only the animated body extent.
        renderSibling=false;for(int i=0;i<120;++i) treeFrame();
        const float treeClosedBottom=treeCardBottom;
        treeOpen=true;for(int i=0;i<120;++i) treeFrame();
        const float treeExtent=treeCardBottom-treeClosedBottom;
        treeOpen=false;float lastTreeCard=treeCardBottom-treeClosedBottom;int treeSettledFrame=0;
        for(int i=0;i<120;++i) {
            treeFrame();
            if(treeCardBottom>treeClosedBottom) lastTreeCard=treeCardBottom-treeClosedBottom;
            if(!treeSettledFrame && std::abs(treeCardBottom-treeClosedBottom)<.05f) treeSettledFrame=i+1;
        }
        std::printf("Tree collapse: card-drop=%.3f sibling-drop=%.3f closed-gap=%.3f settled=%d frames\n",lastTreeCard,lastTreeSibling,siblingTop-treeBottom,treeSettledFrame);
        Check(treeSettledFrame>0 && treeSettledFrame<=18,"a nested tree and its enclosing card finish together without a parent tail");
        Check(lastTreeCard<=treeExtent*.12f+1,"a tree at the end of a card has no final step beyond its regular motion");
        ImGui::GetStyle()=savedHeaderStyle;
        // Fresh nested cards must be measured while clipped; progressively discovering
        // their native minimum heights creates a late second expansion.
        bool nestedOpen=false;float nestedEdge=0;
        auto nestedFrame=[&]() {
            ImGui::NewFrame();Scope scope;
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({800,850});
            ImGui::Begin("Nested opening###nested-opening");
            ImGui::SetNextItemOpen(nestedOpen,ImGuiCond_Always);
            {
                ScopedCollapsingHeader parent("Parent options");
                if(parent.IsHeaderOpen()) for(int child=0;child<4;++child) {
                    ImGui::PushID(child);
                    { ScopedCollapsingHeader card("Nested options",ImGuiTreeNodeFlags_DefaultOpen);
                      if(card.IsHeaderOpen()) for(int row=0;row<4;++row) ImGui::Text("Nested row %d",row); }
                    ImGui::Spacing();ImGui::PopID();
                }
            }
            nestedEdge=ImGui::GetItemRectMax().y;
            ImGui::End();ImGui::Render();
        };
        ApplyMetrics(ImGui::GetStyle());for(int i=0;i<60;++i) nestedFrame();
        nestedOpen=true;float previousEdge=nestedEdge,previousOpeningStep=FLT_MAX;
        bool lateAcceleration=false;
        for(int i=0;i<60;++i) {
            nestedFrame();const float step=nestedEdge-previousEdge;
            if(i>=10 && step>previousOpeningStep+2) lateAcceleration=true;
            previousEdge=nestedEdge;previousOpeningStep=step;
        }
        Check(!lateAcceleration,"nested sections finish one continuous reveal without a delayed second expansion");
        ImGui::GetStyle()=savedHeaderStyle;
        float at60 = 0, at120 = 0;
        for (int i=0; i<60; ++i) at60 = Approach(at60,1,1.0f/60,18);
        for (int i=0; i<120; ++i) at120 = Approach(at120,1,1.0f/120,18);
        Check(std::abs(at60-at120) < 0.0001f, "motion duration is independent of frame rate");
        Check(Approach(0,1,0,18)==0, "zero elapsed time does not advance animation");
        Check(Approach(0,1,10,18)==1, "long frame gaps settle without replaying motion");
        Check(std::isfinite(Approach(NAN,1,NAN,18)), "invalid timing never poisons draw geometry");
        Check(LinearStep(0,1,0,1/RevealDuration)==0,"a paused reveal does not advance");
        Check(LinearStep(.5f,1,10,1/RevealDuration)==1,"a long frame gap finishes reveal without replaying it");
        Check(std::isfinite(LinearStep(NAN,1,NAN,NAN)),"invalid reveal state never poisons draw geometry");
        const auto largeSize=WindowSize({1920,1080},2);
        const auto grownPosition=WindowPosition({1338,12},{0,0},{1920,1080},largeSize);
        Check(grownPosition.x+largeSize.x<=1908 && grownPosition.y+largeSize.y<=1068,
              "growing from 0.5 to 2.0 keeps the complete menu inside the viewport");
        const auto moved=WindowPosition({150,80},{0,0},{1920,1080},WindowSize({1920,1080},.5f));
        Check(moved.x==150 && moved.y==80,"fitting a window preserves an in-bounds dragged position");
        Neurotic::SetLanguage("en");
        frame();
        auto* window = ImGui::FindWindowByName("Motion test###sleek-test");
        const int initialStorage = window->StateStorage.Data.Size;
        for (int i=0;i<1000;++i) frame();
        Check(window->StateStorage.Data.Size == initialStorage, "stable controls do not allocate new animation keys per frame");
        ImGui::NewFrame();
        ImGui::Begin("Motion test###sleek-test");
        {
            Scope scope;
            const float initial = Animate(9123, 5, 0);
            const float twice = Animate(9123, 5, 1);
            Check(initial == twice, "an animation channel advances at most once per frame");
        }
        {
            Scope scope(true);
            Check(Animate(9123,5,1)==1, "reduced motion settles immediately even within the same frame");
        }
        ImGui::End(); ImGui::Render();
#endif
        Check(!disabled, "disabled controls retain their state");
        io.AddMousePosEvent(disabledRect.GetCenter().x,disabledRect.GetCenter().y);frame();
        io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
        Check(!disabled,"clicking a disabled switch cannot edit its value");
        io.AddMousePosEvent(mixedRect.GetCenter().x,mixedRect.GetCenter().y);frame();
        io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
        Check(mixed==3,"activating a mixed switch commits the complete native flag mask");
        io.AddMousePosEvent(-100,-100);frame();
        // Keyboard activation uses the original ImGui checkbox behavior.
        auto* focusWindow = ImGui::FindWindowByName("Motion test###sleek-test");
        GImGui->NavWindow = focusWindow;
        GImGui->NavId = toggleId;
        GImGui->NavLayer = ImGuiNavLayer_Main;
        GImGui->NavCursorVisible = true;
        io.AddKeyEvent(ImGuiKey_Space,true); frame();
        io.AddKeyEvent(ImGuiKey_Space,false); frame();
        Check(!enabled,"keyboard Space activates the same switch");
#if HAS_SLEEK_UI
        ImGui::NewFrame();
        {
            Scope scope;
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({940,700});
            ImGui::Begin("Official brand###brand-test");
            Brand("Example game",HeaderActionsWidth(true)+ImGui::GetStyle().ItemSpacing.x);
            ImGui::End();
        }
        ImGui::Render();
        Check(GImGui->UserTextures.Size==1,"header replaces the generic glyph and font title with one embedded official-brand image");
        auto* brandTexture=GImGui->UserTextures[0];
        Check(brandTexture->Format==ImTextureFormat_RGBA32 && brandTexture->UseColors && brandTexture->Pixels,
              "official brand retains its transparent RGBA pixels and gradient colors");
        brandTexture->SetTexID(ImTextureID_Invalid);brandTexture->SetStatus(ImTextureStatus_Destroyed);
        ImGui::NewFrame();
        {
            Scope scope;ImGui::Begin("Official brand###brand-test");
            Brand("Example game",HeaderActionsWidth(true)+ImGui::GetStyle().ItemSpacing.x);
            ImGui::End();
        }
        ImGui::Render();
        Check(GImGui->UserTextures.Size==1 && GImGui->UserTextures[0]==brandTexture &&
              brandTexture->Status==ImTextureStatus_WantCreate,
              "logo reuploads after renderer invalidation without duplicating its per-context texture");
        // A mouse-focused control must not keep emitting a tooltip at the footer.
        bool hintLight=false,hintGraphs=false,hintTooltip=false,hintReadoutHovered=false;int hintScale=0,hintLanguage=0;
        std::string hintStatus="Active: selected provider";float hintStatusHeight=0;
        ImGuiID hintTheme=0,hintScaleId=0,hintLanguageId=0,hintStatusId=0;ImVec2 hintFooter{},hintThemePoint{},hintStatusPoint{};
        const auto hintFrame=[&](int focus) {
            ImGui::NewFrame();Scope scope;
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({940,700});
            ImGui::Begin("Pointer hints###hints");
            auto* host=ImGui::GetCurrentWindow();
            hintTheme=ImGui::GetID("##ThemeToggle");hintScaleId=ImGui::GetID("##MenuScale");hintLanguageId=ImGui::GetID("##FooterLanguage");
            if(focus>=1 && focus<=4) {
                GImGui->NavWindow=host;GImGui->NavId=(focus==1 || focus==4)?hintTheme:focus==2?hintScaleId:hintLanguageId;
                GImGui->NavLayer=ImGuiNavLayer_Main;GImGui->NavCursorVisible=focus==4;GImGui->NavHighlightItemUnderNav=focus==4;
                GImGui->NavInputSource=focus==4?ImGuiInputSource_Keyboard:ImGuiInputSource_Mouse;
            }
            const HeaderGeometry hintLayout(true);
            const auto hintOrigin=ImGui::GetCursorScreenPos();
            hintThemePoint={hintOrigin.x+ImGui::GetContentRegionAvail().x-hintLayout.side*1.5f-hintLayout.gap*2-hintLayout.save,
                hintOrigin.y+hintLayout.side*.5f};
            HeaderActions(hintLight,&hintScale,&hintGraphs);
            ImGui::Dummy({0,hintLayout.side+ImGui::GetStyle().ItemSpacing.y});
            const float statusY=ImGui::GetCursorScreenPos().y;
            DlssNr::StatusPanel::Line("##HintStatus",hintStatus,{1,1,1,1});
            hintStatusHeight=ImGui::GetCursorScreenPos().y-statusY;
            hintStatusId=ImGui::GetItemID();hintStatusPoint=(ImGui::GetItemRectMin()+ImGui::GetItemRectMax())*.5f;
            hintFooter={host->Pos.x+host->Size.x*.5f,host->Pos.y+host->Size.y-12};
            ImGui::SetCursorScreenPos({host->Pos.x+ImGui::GetStyle().WindowPadding.x,host->Pos.y+host->Size.y-FooterBottomInset()-FooterHeight(focus==5)});
            bool advisor=false;FooterActions(&hintLanguage);
            if(focus==5) {
                FooterReadout("1920x1080 | GPU: Example");
                hintReadoutHovered=ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);
                hintFooter=(ImGui::GetItemRectMin()+ImGui::GetItemRectMax())*.5f;
            }
            hintTooltip=GImGui->TooltipPreviousWindow!=nullptr;
            ImGui::End();ImGui::Render();
        };
        hintFrame(0);io.AddMousePosEvent(hintFooter.x,hintFooter.y);
        hintFrame(1);hintFrame(1);
        Check(!hintTooltip,"moving to the footer cannot summon the focused theme control's tooltip");
        hintFrame(2);Check(!hintTooltip,"Scale has no hover/focus tooltip at the footer");
        hintFrame(3);Check(!hintTooltip,"Language has no hover/focus tooltip at the footer");
        hintFrame(4);Check(!hintTooltip,"retained keyboard focus cannot summon a hint under the footer pointer");
        GImGui->NavId=0;GImGui->NavWindow=nullptr;io.AddMousePosEvent(-100,-100);
        hintFrame(0);io.AddMousePosEvent(hintThemePoint.x,hintThemePoint.y);
        for(int i=0;i<40;++i) hintFrame(0);
        Check(!hintTooltip,"theme icon keeps its animation without hover tooltips");
        io.AddMousePosEvent(hintFooter.x,hintFooter.y);hintFrame(0);
        Check(!hintTooltip,"a visible hover hint disappears when the pointer leaves its control");
        for(int i=0;i<40;++i) {io.AddMousePosEvent(hintFooter.x,hintFooter.y);hintFrame(5);}
        Check(hintReadoutHovered,"readout regression probes the blank part of the actual footer row");
        Check(!hintTooltip,"blank space after a readable footer readout has no tooltip");
        GImGui->NavId=0;GImGui->NavWindow=nullptr;
        io.AddMousePosEvent(hintStatusPoint.x,hintStatusPoint.y);hintFrame(0);
        io.AddMouseButtonEvent(0,true);hintFrame(0);
        io.AddMouseButtonEvent(0,false);hintFrame(0);
        Check(GImGui->OpenPopupStack.Size==0&&!hintTooltip,"a readable status row remains a passive readout on hover and click");
        const float readableStatusHeight=hintStatusHeight;
        hintStatus="Owner details: "+std::string(500,'W')+"\nSecond observation";
        for(int settle=0;settle<3;++settle)hintFrame(0);
        Check(hintTooltip&&std::abs(hintStatusHeight-readableStatusHeight)<.1f,
            "overflowing and multiline status values expose hover details without changing row geometry");
        hintStatus="Active: selected provider";
        io.AddKeyEvent(ImGuiKey_Escape,true);hintFrame(0);io.AddKeyEvent(ImGuiKey_Escape,false);hintFrame(0);
        io.AddMousePosEvent(hintFooter.x,hintFooter.y);hintFrame(0);
        for(int settle=0;settle<40;++settle)hintFrame(0);
        Check(!hintTooltip,"focused NR status row cannot emit a tooltip after the pointer moves to the footer");
        GImGui->NavId=0;GImGui->NavWindow=nullptr;
        io.AddMousePosEvent(-100,-100);
        for (const auto& language : Neurotic::Languages)
        for (int step=5;step<=20;++step)
        for (bool light : {false,true})
        for (const auto viewport : {ImVec2(1280,900),ImVec2(800,600)})
        {
            Neurotic::SetLanguage(language.code);
            const float scale=step/10.0f;
            ImGui::GetStyle()=ImGuiStyle();
            if (light) ImGui::StyleColorsLight(); else ImGui::StyleColorsDark();
            ApplyMetrics(ImGui::GetStyle());
            ImGui::GetStyle().ScaleAllSizes(scale);
            ImGui::NewFrame();
            ImGui::PushFont(nullptr,16*scale);
            Scope scope;
            const float width=WindowSize(viewport,scale).x;
            ImGui::SetNextWindowSize({width,850});
            ImGui::SetNextWindowPos({0,0});
            ImGui::Begin("Responsive shell###shell");
            const auto shellOrigin=ImGui::GetCursorScreenPos();
            const float shellEdge=shellOrigin.x+ImGui::GetContentRegionAvail().x;
            bool shellTheme=light;
            int shellScale=0,shellLanguage=0;bool shellGraphs=false;
            Check(HeaderActionsWidth(true,false)<=ImGui::GetContentRegionAvail().x,"complete localized header fits at every scale");
            Check(HeaderActionsWidth(true,false)+ImGui::GetFontSize()*2.6f<=ImGui::GetContentRegionAvail().x,
                "compact header preserves room for the brand glyph without overlapping scale");
            HeaderActions(shellTheme,nullptr,&shellGraphs);
            Check(ImGui::GetItemRectMax().x<=shellEdge+1,
                  "localized header close stays right aligned at every supported scale");
            Check(ImGui::GetCursorScreenPos().x==shellOrigin.x && ImGui::GetCursorScreenPos().y==shellOrigin.y,
                  "localized header actions restore the brand origin at every scale");
            Brand("Example game",HeaderActionsWidth(true,false)+ImGui::GetStyle().ItemSpacing.x);
            int page=1;
            const bool wide=false;
            const float navigationRight=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
            const float navigationFont=ImGui::GetFontSize();
            Navigation(page,650,wide);
            const ImRect nav(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
            Check(nav.Max.x<=ImGui::GetWindowPos().x+width+1,"navigation fits translated captions at every supported scale");
            Check(std::abs(nav.Max.x-navigationRight)<1,"seven top tabs fill the entire available width");
            Check(ImGui::GetFontSize()==navigationFont,"equal top tabs preserve the caption font size");
            Check(nav.GetHeight()<800,"compact navigation leaves room for page content");
            ImGui::BeginChild("Content",{0,200},ImGuiChildFlags_Borders);
            {
            PageReveal reveal(page);

            int flags=1;
            ImGui::CheckboxFlags("Mixed",&flags,3);
            Check(flags==1,"mixed switch drawing preserves flag values");
            }
            ImGui::EndChild();
            const float footerTop=850-FooterBottomInset()-FooterHeight(true);
            ImGui::SetCursorScreenPos({shellOrigin.x,footerTop});
            bool shellAdvisor=false;
            FooterControls(shellLanguage,shellScale);
            Check(std::abs(ImGui::GetItemRectMax().x-shellEdge)<1,"right footer controls stay anchored at every scale and language");
            Check(std::abs(ImGui::GetItemRectMax().y-(footerTop+ImGui::GetFrameHeight()))<1,
                "support, language and scale share one row at every supported size");
            FooterReadout("1920x1080 -> 3840x2160 | GPU: Example device");
            Check(ImGui::GetItemRectMax().x<=shellEdge+1 && ImGui::GetItemRectMax().y<=850-FooterBottomInset()+1,
                "device readout stays below the aligned footer row");
            ImGui::End(); ImGui::PopFont(); ImGui::Render();
        }
        Neurotic::SetLanguage("en");
        ImGui::GetStyle()=ImGuiStyle();ApplyMetrics(ImGui::GetStyle());
        int selectedPage=1;ImVec2 firstPagePoint{};ImGuiID secondPageId=0;
        auto navigationFrame=[&]() {
            ImGui::NewFrame();Scope scope;
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({700,700});
            ImGui::Begin("Navigation activation###navactivation");
            firstPagePoint=ImGui::GetCursorScreenPos()+ImVec2(25,20);
            ImGui::PushID(1);secondPageId=ImGui::GetID("##Page");ImGui::PopID();
            Navigation(selectedPage,400,false);
            ImGui::End();ImGui::Render();
        };
        navigationFrame();
        io.AddMousePosEvent(firstPagePoint.x,firstPagePoint.y);navigationFrame();
        io.AddMouseButtonEvent(0,true);navigationFrame();
        io.AddMouseButtonEvent(0,false);navigationFrame();
        Check(selectedPage==0,"native navigation click selects General immediately");
        io.AddMousePosEvent(-100,-100);navigationFrame();
        GImGui->NavWindow=ImGui::FindWindowByName("Navigation activation###navactivation");
        GImGui->NavId=secondPageId;GImGui->NavLayer=ImGuiNavLayer_Main;GImGui->NavCursorVisible=true;
        io.AddKeyEvent(ImGuiKey_Space,true);navigationFrame();
        io.AddKeyEvent(ImGuiKey_Space,false);navigationFrame();
        Check(selectedPage==1,"keyboard navigation selects Upscaling");
        ImRect firstCell{};
        for(int cell=0;cell<7;++cell) {
            ImGui::NewFrame();Scope scope;
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({700,700});
            ImGui::Begin("Equal cell geometry###equalcells");
            auto* window=ImGui::GetCurrentWindow();
            ImGui::PushID(cell);const auto id=ImGui::GetID("##Page");ImGui::PopID();
            GImGui->NavWindow=window;GImGui->NavId=id;GImGui->NavLayer=ImGuiNavLayer_Main;
            int selected=cell;Navigation(selected,400,false);
            const auto rectangle=window->NavRectRel[ImGuiNavLayer_Main];
            if(cell==0) firstCell=rectangle;
            Check(rectangle.GetWidth()>0 && std::abs(rectangle.GetWidth()-firstCell.GetWidth())<1,
                "each native top-tab focus target has the same width");
            Check(std::abs(rectangle.GetHeight()-firstCell.GetHeight())<1,
                "each native top-tab focus target has the same height");
            ImGui::End();ImGui::Render();
        }
        ImGui::NewFrame();
        {
            Scope scope;ImGui::Begin("Selection outline bounds");
            auto* draw=ImGui::GetWindowDrawList();const ImVec2 lo(100,100),hi(300,140);
            for(bool focused : {false,true}) {
                const int first=draw->VtxBuffer.Size;
                DlssNr::MenuControls::HighlightItem(draw,lo,hi,focused);
                for(int vertex=first;vertex<draw->VtxBuffer.Size;++vertex) {
                    const auto p=draw->VtxBuffer[vertex].pos;
                    Check(p.x>=lo.x-.01f && p.x<=hi.x+.01f && p.y>=lo.y-.01f && p.y<=hi.y+.01f,
                        "NR selection outline stays inside the dropdown border");
                }
            }
            ImGui::End();
        }
        ImGui::Render();
        ImVec2 headerSave{},headerTheme{},headerClose{};bool fixtureLight=false,fixtureAdvisor=false;
        int fixtureScale=0,fixtureLanguage=0;bool fixtureGraphs=true;
        bool fixtureSave=false,fixtureClose=false;
        const auto actionFrame=[&]() {
            ImGui::NewFrame();Scope scope;
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({900,700});
            ImGui::Begin("Header and footer bounds###actions");
            const auto origin=ImGui::GetCursorScreenPos();
            const float edge=origin.x+ImGui::GetContentRegionAvail().x;
            const float side=ImGui::GetFrameHeight()*1.25f,gap=ImGui::GetStyle().ItemSpacing.x;
            headerTheme={edge-side*2-gap*2-ButtonWidth("Save Settings")-ImGui::GetFontSize()*2+side*.5f,origin.y+side*.5f};
            headerSave={edge-side-gap-(ButtonWidth("Save Settings")+ImGui::GetFontSize()*2)*.5f,origin.y+side*.5f};
            headerClose={edge-side*.5f,origin.y+side*.5f};
            const auto result=HeaderActions(fixtureLight,&fixtureScale,&fixtureGraphs);
            fixtureSave|=result.saveClicked;fixtureClose|=result.closeClicked;
            Check(std::abs(ImGui::GetItemRectMax().x-edge)<1,"header close X is anchored to the content right edge");
            Check(ImGui::GetCursorScreenPos().x==origin.x && ImGui::GetCursorScreenPos().y==origin.y,
                "header actions preserve the brand's layout origin");
            ImGui::SetCursorScreenPos({origin.x,ImGui::GetWindowPos().y+700-FooterBottomInset()-FooterHeight(true)});
            const auto footerOrigin=ImGui::GetCursorScreenPos();
            FooterActions(&fixtureLanguage);
            Check(ImGui::GetItemRectMax().x<=edge+1,"remaining footer actions stay inside the right edge");
            FooterReadout("1920x1080 -> 3840x2160 (2.0) [3840x2160 (2.0)]  1200 | GPU: deliberately long device name for the compact footer");
            Check(ImGui::GetItemRectMin().x==footerOrigin.x,"resolution and GPU readout starts at the footer left edge");
            Check(ImGui::GetItemRectMax().x<=edge+1 && ImGui::GetItemRectMax().y<=700-FooterBottomInset()+1,
                "footer readout fits the original window bezel without growing the window");
            ImGui::End();ImGui::Render();
        };
        actionFrame();
        for(const auto point : {headerTheme,headerSave,headerClose}) {
            io.AddMousePosEvent(point.x,point.y);actionFrame();
            io.AddMouseButtonEvent(0,true);actionFrame();
            io.AddMouseButtonEvent(0,false);actionFrame();
        }
        Check(fixtureLight && fixtureSave && fixtureClose,"right header actions retain independent native click behavior");
        io.AddMousePosEvent(-100,-100);
        for(float panelWidth : {420.0f,900.0f}) {
            ImGui::NewFrame();Scope scope;
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({panelWidth,700});
            ImGui::Begin("Observation action bounds");
            const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
            ImGui::TextUnformatted("Signal Status");
            const auto result=ObservationActions();
            Check(result.copyId!=0,"right-aligned copy action keeps its native feedback identity");
            Check(std::abs(ImGui::GetItemRectMax().x-right)<1,"copy action is right anchored at narrow and wide widths");
            ImGui::End();ImGui::Render();
        }
        for (const auto& language : Neurotic::Languages)
        for (float scale : {0.5f,1.0f,2.0f}) {
            Neurotic::SetLanguage(language.code);
            ImGui::NewFrame(); Scope scope;
            ImGui::PushFont(nullptr,14.0f*scale);
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1000,600});
            ImGui::Begin("Inline multipass sliders");
            const float groupWidth=DlssNr::MenuControls::HalfWidthSliderGroup({"Model Resolution", "Model Strength", "Detail Strength"});
            Check(groupWidth<=ImGui::GetContentRegionAvail().x*.5f,"global sliders use at most half the window");
            for (const char* label : {"Model Resolution###Model Resolution##AdditionalPassModelResolution",
                    "Model Strength###Model Strength##AdditionalPassModelStrength",
                    "Detail Strength###Detail Strength##AdditionalPassDetailStrength"}) {
                const auto origin=ImGui::GetCursorScreenPos();
                const float right=origin.x+ImGui::GetContentRegionAvail().x;
                float value=1;ImGui::PushID(label);
                ImGui::SetNextItemWidth(groupWidth);
                ImGui::SetNextItemWidth(DlssNr::MenuControls::InlineSliderWidth(label));
                Check(std::abs(ImGui::CalcItemWidth()-groupWidth)<1,"translated global sliders retain equal track lengths");
                ImGui::SliderFloat(label,&value,0,2);const float rowBottom=ImGui::GetItemRectMax().y;
                ImGui::SameLine();ImGui::SmallButton("Reset");
                Check(ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+ImGui::CalcTextSize("(?)").x<=right+1,
                    "slider and reset reserve enough space for the inline help marker");
                ImGui::SameLine();ImGui::TextDisabled("(?)");
                Check(ImGui::GetItemRectMax().y<=rowBottom+1,"slider label, reset and help fit one row");
                ImGui::PopID();
            }
            ImGui::End();ImGui::PopFont();ImGui::Render();
        }
        Neurotic::SetLanguage("en");
        Check(ChildPageCount(0)==6,"General exposes the final Animations child without moving existing settings");
        Check(ChildPageCount(1)==6,"Upscaling always exposes its Pre-flight tab");
        int upscalingCount=0;Check(std::string(ChildPages(1,upscalingCount)[5])=="SR Pre-Flight","SR readiness retains its stable page identity");
        for(bool feature:{false,true})for(bool initialized:{false,true})for(bool frozen:{false,true}) {
            const auto status=UpscalingNavigationStatus(feature,initialized,frozen);
            Check(status.running==(feature&&initialized&&!frozen)&&status.issue==(!feature||!initialized||frozen),
                "Upscaling retains the legacy readiness and issue facts");
            Check(status.pilot==(!feature||frozen?PilotState::Degraded:initialized?PilotState::On:PilotState::Blocked),
                "SR distinguishes absent/frozen service from initialized failure and ready output");
        }
        for(bool requested:{false,true}) for(bool native:{false,true})
            for(bool managed:{false,true}) for(bool paused:{false,true}) for(bool waiting:{false,true}) {
                const auto fg=FrameGenerationNavigationStatus(requested,native,managed,paused,waiting);
                const bool active=native || (managed&&!paused&&!waiting);
                Check(fg.running==active,"FG icon requires native work or active unpaused managed generation");
                Check(fg.issue==(!active&&(requested||paused||waiting)),"FG request and pause are waiting states");
                Check(fg.pilot==(active?PilotState::On:(requested||paused||waiting)?PilotState::Degraded:PilotState::Off),
                    "FG icon does not mistake an unlock request for working generation");
            }
        Check(UpscalingNavigationStatus(false,false,false,false).pilot==PilotState::Blocked,
            "SR reports confirmed missing upscaler libraries as blocked");
        Check(UpscalingNavigationStatus(false,false,false,true).pilot==PilotState::Degraded,
            "SR reports available libraries waiting for game input as degraded");
        Check(UpscalingNavigationStatus(true,true,false,false).pilot==PilotState::On,
            "An active owner takes precedence over optional library detection");
        for(const char* language:{"en","de","fr","es","pt"})for(float size:{13.f,26.f})for(float width:{350.f,700.f,1200.f}) {
            Neurotic::SetLanguage(language);ImGui::NewFrame();Scope scope(true);ImGui::PushFont(nullptr,size);
            ImGui::SetNextWindowSize({width,300});ImGui::Begin("Primary label bounds");bool primaryValue=false;
            const auto expectedId=ImGui::GetID("Enable Neural Rendering");
            const float available=ImGui::GetContentRegionAvail().x;
            const int firstVertex=ImGui::GetWindowDrawList()->VtxBuffer.Size;
            GImGui->NavActivateId=GImGui->NavActivateDownId=GImGui->NavActivatePressedId=expectedId;
            const bool changed=DlssNr::MenuControls::EmphasizedCheckbox("Enable Neural Rendering",&primaryValue,false,true);
            Check(changed&&primaryValue,"primary keyboard activation still toggles the native value");
            const float content=ToggleWidth()+ImGui::GetStyle().ItemInnerSpacing.x+ImGui::CalcTextSize("Enable Neural Rendering").x;
            Check(ImGui::GetItemRectSize().x>=(std::min)(content+8,available),"primary hit target and border reserve padding around the complete localized label");
            Check(ImGui::GetItemRectSize().x<=available+.5f,"primary wrapped label fits narrow content");
            const ImRect bounds(ImGui::GetItemRectMin()-ImVec2(1,1),ImGui::GetItemRectMax()+ImVec2(1,1));
            for(int v=firstVertex;v<ImGui::GetWindowDrawList()->VtxBuffer.Size;++v)
                Check(bounds.Contains(ImGui::GetWindowDrawList()->VtxBuffer[v].pos),"primary glyphs and highlight stay inside the padded bounds");
            Check(GImGui->LastItemData.ID==expectedId,"primary toggle preserves native identity");
            const float primarySwitchLeft=ImGui::GetItemRectMin().x+DlssNr::MenuControls::PrimaryCheckboxPadding();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX()+DlssNr::MenuControls::PrimaryCheckboxPadding());
            bool model=false;ImGui::Checkbox("Apply the model",&model);
            Check(std::abs(ImGui::GetItemRectMin().x-primarySwitchLeft)<.1f,"Enable and Apply switches and label origins align");
            ImGui::End();ImGui::PopFont();ImGui::Render();
        }
        Neurotic::SetLanguage("en");
        // Restart is a presentation of real session transitions, not a new admission gate.
        Neurotic::Mfg::MfgSetupInput restartInput;
        Check(!MfgRestartRequired(restartInput,false,{},{}),"ordinary unsupported/off sessions show no restart warning");
        restartInput.supported=restartInput.selected=restartInput.startedSelected=true;
        restartInput.publication=Neurotic::Mfg::MfgRuntimeStatus::Published;
        Check(!MfgRestartRequired(restartInput,false,{},{}),"ready native MFG does not require restart");
        Check(MfgRestartRequired(restartInput,true,{},{}),"persistent higher-ratio refusal requires restart even after unlock publication");
        restartInput.routePending=true;
        Check(MfgRestartRequired(restartInput,false,{},{}),"a staged native route requires restart");
        restartInput.routePending=false;restartInput.startedSelected=false;
        Check(!MfgRestartRequired(restartInput,false,{},{}),"late publication alone does not make a working provider require restart");
        restartInput.publication=Neurotic::Mfg::MfgRuntimeStatus::Unavailable;
        Check(MfgRestartRequired(restartInput,false,{},{}),"native setup requiring restart remains visible");
        restartInput.selected=false;
        Check(!MfgRestartRequired(restartInput,false,{},{}),"turning manual overrides off can take effect live");
        for(bool a:{false,true})for(bool b:{false,true})for(bool c:{false,true})for(bool d:{false,true})
            Check(MfgRestartRequired(restartInput,false,{a,b},{c,d})==(a!=c||b!=d),"experimental edits compare to startup intent, including saved edits and reversions");
        float warningHeight=0,compatibilityY=0;
        for(float size:{13.f,26.f})for(float width:{350.f,700.f})for(bool required:{false,true})for(int settle=0;settle<3;++settle) {
            ImGui::NewFrame();ImGui::PushFont(nullptr,size);Scope scope;
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,450});
            ImGui::Begin("Restart warning sizing");const float before=ImGui::GetCursorPosY();
            DrawMfgRestartWarning(required);
            const float height=ImGui::GetCursorPosY()-before;
            bool compatibility=false;ImGui::Checkbox("RTX 40",&compatibility);const float y=ImGui::GetItemRectMin().y;
            if(!required){warningHeight=height;compatibilityY=y;}
            Check(height>0&&std::abs(height-warningHeight)<.1f&&std::abs(y-compatibilityY)<.1f,
                "GPU compatibility title reserves equal warning and control geometry when restart changes");
            for(auto* child:GImGui->Windows)if(settle==2&&child->Active&&child->ParentWindow==ImGui::GetCurrentWindow()) {
                Check(child->ScrollMax.x==0 && child->ScrollMax.y==0,"red restart banner fits narrow and scaled layouts");
            }
            ImGui::End();ImGui::PopFont();ImGui::Render();
            Check(GImGui->ColorStack.empty()&&GImGui->FontStack.empty(),"restart warning restores font and style state");
        }
        // Exercise the production compatibility controls: consent is per option and per enable.
        bool compatibility[3]{};
        ImGuiID compatibilityIds[3]{};
        unsigned compatibilityChanges=0;
        auto compatibilityFrame=[&](ImGuiID activate=0) {
            ImGui::NewFrame();Scope scope(true);
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({800,500});
            ImGui::Begin("Compatibility consent fixture");
            for(int i=0;i<3;++i) {
                const std::string label="###ExperimentalMfgRTX"+std::to_string(40-i*10);
                compatibilityIds[i]=ImGui::GetID(label.c_str());
            }
            if(activate) {
                GImGui->NavActivateId=GImGui->NavActivateDownId=GImGui->NavActivatePressedId=activate;
            }
            compatibilityChanges=ExperimentalMfgControls(compatibility[0],compatibility[1],compatibility[2],1);
            ImGui::End();ImGui::Render();
        };
        compatibilityFrame();compatibilityFrame();
        compatibilityFrame(compatibilityIds[0]);
        Check(compatibility[0]&&compatibilityChanges==1u&&GImGui->OpenPopupStack.Size==0,"RTX40 enabling applies immediately without experimental agreement");
        compatibilityFrame();
        for(int family=1;family<3;++family) {
            compatibilityFrame(compatibilityIds[family]);compatibilityFrame();
            Check(!compatibility[family] && compatibilityChanges==0,"enable stays unapplied before agreement");
            auto* warning=ImGui::FindWindowByName("Confirm experimental GPU compatibility");
            Check(warning && GImGui->OpenPopupStack.Size==1,"each GPU choice opens the experimental warning directly");
            compatibilityFrame(warning->GetID("Cancel"));compatibilityFrame();
            Check(!compatibility[family] && GImGui->OpenPopupStack.Size==0,"cancel preserves disabled intent");
            compatibilityFrame(compatibilityIds[family]);compatibilityFrame();
            compatibilityFrame(warning->GetID("I agree"));
            Check(compatibility[family] && compatibilityChanges==(1u<<family),"agreement enables only the selected GPU choice");
            compatibilityFrame();compatibilityFrame();
        }
        Check(compatibility[0]&&compatibility[1]&&compatibility[2],"all three compatibility preferences are independent");
        for(int family=0;family<3;++family) {
            compatibilityFrame(compatibilityIds[family]);
            Check(!compatibility[family] && compatibilityChanges==(1u<<family) && GImGui->OpenPopupStack.Size==0,
                  "disabling applies without another warning");
            compatibilityFrame();
        }
        compatibilityFrame(compatibilityIds[0]);compatibilityFrame();
        Check(compatibility[0] && GImGui->OpenPopupStack.Size==0,"RTX40 re-enabling has no experimental agreement");
        compatibilityFrame();
        // Simulate wheel scrolling at the content limit and over fixed navigation.
        float pinnedHeader=0,pinnedNavigation=0,contentScroll=0;
        ImVec2 contentPoint{},headerPoint{};
        auto fixedFrame=[&]() {
            ImGui::NewFrame();Scope scope(true);
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({940,700});
            ImGui::SetNextWindowScroll({0,0});
            ImGui::Begin("Fixed menu shell",nullptr,FixedShellFlags);
            const float header=ImGui::GetCursorScreenPos().y;
            ImGui::TextUnformatted("NeuRotic");headerPoint=ImGui::GetItemRectMin()+ImVec2(5,5);
            int selected=0;const float nav=ImGui::GetCursorScreenPos().y;Navigation(selected,500,false);
            if(pinnedHeader) Check(header==pinnedHeader&&nav==pinnedNavigation,"title and primary navigation remain pinned during wheel scrolling");
            pinnedHeader=header;pinnedNavigation=nav;
            ImGui::BeginChild("Selected content",{0,420});
            contentPoint=ImGui::GetWindowPos()+ImVec2(100,100);
            contentScroll=ImGui::GetScrollY();
            for(int row=0;row<90;++row) ImGui::Text("row %d",row);
            ImGui::EndChild();
            // Overfull ancillary content cannot turn the root into a scrolling window.
            ImGui::Dummy({0,300});
            Check(ImGui::GetScrollY()==0,"outer shell never scrolls");
            ImGui::End();ImGui::Render();
        };
        fixedFrame();fixedFrame();io.AddMousePosEvent(contentPoint.x,contentPoint.y);fixedFrame();
        io.AddMouseWheelEvent(0,-4);fixedFrame();fixedFrame();
        Check(contentScroll>0,"wheel scrolling reaches the selected section content");
        for(int i=0;i<12;++i) {io.AddMouseWheelEvent(0,-20);fixedFrame();}
        io.AddMousePosEvent(headerPoint.x,headerPoint.y);fixedFrame();
        io.AddMouseWheelEvent(0,-10);fixedFrame();fixedFrame();
        io.AddMousePosEvent(-100,-100);
        // Every new parent keeps navigation pinned and each child owns its scroll state.
        for(int parent : {0,1,3,4,5,6}) {
            const std::vector<int> visible = parent==1 ? std::vector<int>{5,0,1,2,3} :
                parent==3 ? std::vector<int>{1,0,2,3} : parent==4 ? std::vector<int>{0,2,3,4} :
                [&]{std::vector<int> ids;for(int i=0;i<ChildPageCount(parent);++i)ids.push_back(i);return ids;}();
            const int firstChild=visible.front();int child=firstChild;const int count=int(visible.size());ImVec2 targets[6]{};
            float firstScroll=0,pinnedY=0;
            auto childFrame=[&](bool scroll=false) {
                ImGui::NewFrame();Scope scope;
                ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1000,700});
                ImGui::Begin("Child navigation regression");ImGui::PushID(parent);
                SectionNavigation(parent,child);
                auto* bar=GImGui->TabBars.GetByKey(ImGui::GetID("##SectionPages"));
                Check(bar && bar->Tabs.Size==count,"each parent exposes the requested child count");
                for(int i=0;i<count;++i) {const int semantic=visible[i];targets[semantic]={bar->BarRect.Min.x+bar->Tabs[i].Offset+bar->Tabs[i].Width*.5f,bar->BarRect.GetCenter().y};}
                if(parent==1)Check(std::string(ImGui::TabBarGetTabName(bar,&bar->Tabs[0]))=="SR Pre-Flight","SR Pre-Flight is first without changing semantic page indices");
                if(pinnedY) Check(std::abs(pinnedY-targets[0].y)<1,"child navigation remains outside scrolling content");
                pinnedY=targets[0].y;
                ImGui::PushID(child);ImGui::BeginChild("Page",{0,400});
                if(scroll) ImGui::SetScrollY(180);
                if(child==firstChild) firstScroll=ImGui::GetScrollY();
                else Check(ImGui::GetScrollY()==0,"unvisited children start at the top");
                for(int row=0;row<70;++row) ImGui::Text("row %d",row);
                ImGui::EndChild();ImGui::PopID();ImGui::PopID();ImGui::End();ImGui::Render();
            };
            childFrame();childFrame();childFrame(true);childFrame();
            Check(firstScroll>100,"first child can scroll independently");
            for(int i=1;i<=count;++i) {
                const int target=visible[i%count];
                io.AddMousePosEvent(targets[target].x,targets[target].y);childFrame();
                io.AddMouseButtonEvent(0,true);childFrame();io.AddMouseButtonEvent(0,false);childFrame();childFrame();
                Check(child==target,"each new child tab is selectable by native click");
            }
            Check(firstScroll>100,"returning to a child restores its own scroll position");
        }
        Check(ChildPageCount(4)==5,"Advanced retains five legacy page identities while consolidated navigation shows four tabs");
        io.AddMousePosEvent(-100,-100);
        // Follow-up layout: pinned NR navigation and bottom-right scaling.
        int neuralSelection=6,footerScale=0,footerLanguage=0;bool topLight=false,topGraphs=false;
        ImVec2 neuralPoints[7]{};ImRect scaleBounds,languageBounds;float overviewScroll=0;
        bool mainOn=false,multipassOn=false,overridesOn=false,inspectorOn=false,forceNeural=false;float statusWidths[4]{};ImGuiID statusIds[4]{};
        PilotState neuralHealth=PilotState::Degraded;float healthWidth=0;ImGuiID healthId=0;ImVec2 neuralLampPoint{};
        const auto neuralFrame=[&](bool scroll=false) {
            ImGui::NewFrame();Scope scope;
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1280,700});
            ImGui::Begin("Nested navigation###nested-test");
            const float edge=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
            HeaderActions(topLight,nullptr,&topGraphs);
            ImGui::Dummy({0,ImGui::GetFrameHeight()*1.5f});
            NeuralNavigation(neuralSelection,mainOn,multipassOn,overridesOn,inspectorOn,forceNeural,neuralHealth);
            auto* bar=GImGui->TabBars.GetByKey(ImGui::GetID("##NeuralPages"));
            Check(bar && bar->Tabs.Size==7,"NR exposes intake first and retains all processing and diagnostic pages");
            const int displayOrder[] = {6,0,1,5,4,2,3};
            const char* displayNames[] = {"NR Pre-flight", "NR Settings", "Multi Pass", "Object Rules", "Overrides", "Inspector", "Diagnostics"};
            for(int i=0;i<7;++i) {
                neuralPoints[displayOrder[i]]={bar->BarRect.Min.x+bar->Tabs[i].Offset+bar->Tabs[i].Width*.5f,bar->BarRect.GetCenter().y};
                Check(std::string(ImGui::TabBarGetTabName(bar,&bar->Tabs[i])).starts_with(displayNames[i]),"NR tabs retain their semantic page identities in the requested order");
            }
            const auto& service=bar->Tabs[0];
            if(healthWidth>0)Check(std::abs(service.Width-healthWidth)<1&&service.ID==healthId,
                "preflight service status preserves native tab width and identity");
            healthWidth=service.Width;healthId=service.ID;
            neuralLampPoint={bar->BarRect.Min.x+service.Offset+service.Width-ImGui::GetStyle().FramePadding.x-ImGui::GetFontSize()*.25f,bar->BarRect.GetCenter().y};
            if(bar->PrevFrameVisible==bar->CurrFrameVisible-1){
                const ImU32 expected=ImGui::GetColorU32(neuralHealth==PilotState::On?ImVec4(.2f,.8f,.35f,1):neuralHealth==PilotState::Blocked?ImVec4(.95f,.2f,.2f,1):ImVec4(1,.58f,.12f,1));
                bool found=false;const ImRect slot(neuralLampPoint-ImVec2(3,3),neuralLampPoint+ImVec2(3,3));
                for(const auto& vertex:ImGui::GetWindowDrawList()->VtxBuffer)if(slot.Contains(vertex.pos)&&vertex.col==expected)found=true;
                Check(found,"NR preflight draws the requested ready, incomplete or blocked service pilot");
            }
            for(int n=0;n<4;++n) {
                auto& tab=bar->Tabs[n==0?1:n==1?2:n==2?4:5];
                const auto caption=std::string(ImGui::TabBarGetTabName(bar,&tab));
                Check(caption.find(" - On")==std::string::npos&&caption.find(" - Off")==std::string::npos,
                    "binary NR tab captions remain plain while status changes");
                if(statusWidths[n]>0) {
                    Check(std::abs(tab.Width-statusWidths[n])<1,"status tab width is fixed across On/Off changes");
                    Check(tab.ID==statusIds[n],"status changes preserve the selected tab identity");
                }
                statusWidths[n]=tab.Width;statusIds[n]=tab.ID;
                bool pilotFound=false;
                const ImRect bounds({bar->BarRect.Min.x+tab.Offset,bar->BarRect.Min.y},
                    {bar->BarRect.Min.x+tab.Offset+tab.Width,bar->BarRect.Max.y});
                const bool enabled=n==0?mainOn:n==1?multipassOn:n==2?overridesOn:inspectorOn;
                const ImU32 color=ImGui::GetColorU32(enabled?ImVec4(.2f,.8f,.35f,1):ImVec4(.48f,.5f,.54f,1));
                const ImVec2 center(bounds.Max.x-ImGui::GetStyle().FramePadding.x-ImGui::GetFontSize()*.25f,bounds.GetCenter().y);
                const ImRect slot(center-ImVec2(ImGui::GetFontSize()*.2f,ImGui::GetFontSize()*.2f),center+ImVec2(ImGui::GetFontSize()*.2f,ImGui::GetFontSize()*.2f));
                for (const auto& vertex : ImGui::GetWindowDrawList()->VtxBuffer)
                    if(slot.Contains(vertex.pos)&&vertex.col==color)pilotFound=true;
                if(bar->PrevFrameVisible==bar->CurrFrameVisible-1)Check(pilotFound,"binary NR tabs draw a green or gray pilot inside their reserved slot, including Off");
            }

            ImGui::PushID(neuralSelection);
            ImGui::BeginChild("##MainMenuPageContent",{0,450},ImGuiChildFlags_Borders);
            if(scroll) ImGui::SetScrollY(180);
            if(neuralSelection==0) overviewScroll=ImGui::GetScrollY();
            else Check(ImGui::GetScrollY()==0,"a new child tab starts at its own scroll origin");
            for(int row=0;row<90;++row) ImGui::Text("row %d",row);
            ImGui::EndChild();ImGui::PopID();
            ImGui::SetCursorScreenPos({ImGui::GetWindowPos().x+ImGui::GetStyle().WindowPadding.x,
                700-FooterBottomInset()-FooterHeight(true)});
            const float footerY=ImGui::GetCursorScreenPos().y;
            FooterControls(footerLanguage,footerScale);
            Check(std::abs(ImGui::GetItemRectMax().x-edge)<1,"right footer controls are anchored to the content edge");
            Check(std::abs(ImGui::GetItemRectMax().y-(footerY+ImGui::GetFrameHeight()))<1,"Scale and language are on the support row");
            // The rightmost native combo is Scale; its normal width is preserved in this fixture.
            scaleBounds={{edge-ScaleControlWidth(),footerY},{edge,footerY+ImGui::GetFrameHeight()}};
            const float languageRight=scaleBounds.Min.x-FieldLabelReserve("Scale")-ImGui::GetStyle().ItemSpacing.x*2;
            languageBounds={{languageRight-LanguageControlWidth(),footerY},{languageRight,footerY+ImGui::GetFrameHeight()}};
            FooterReadout("Long device and resolution readout");
            ImGui::End();ImGui::Render();
        };
        neuralFrame();neuralFrame();
        Check(neuralSelection==6,"NR first visit opens input preflight");
        neuralSelection=0;forceNeural=true;neuralFrame();forceNeural=false;neuralFrame();
        Check(neuralSelection==0,"Open Main selects the actual tab after visiting preflight");
        neuralFrame(true);neuralFrame();
        Check(overviewScroll>100,"Overview content can scroll independently");
        const float tabY=neuralPoints[0].y;
        for(int tab : {1,2,3,4,5,6,0}) {
            io.AddMousePosEvent(neuralPoints[tab].x,neuralPoints[tab].y);neuralFrame();
            io.AddMouseButtonEvent(0,true);neuralFrame();
            io.AddMouseButtonEvent(0,false);neuralFrame();neuralFrame();
            Check(neuralSelection==tab,"each named child tab is selectable by native click");
            Check(std::abs(neuralPoints[tab].y-tabY)<1,"NR child tabs stay pinned while content scrolls");
        }
        Check(overviewScroll>100,"returning to Main restores its own scroll position");
        mainOn=true;multipassOn=true;overridesOn=true;inspectorOn=true;neuralFrame();neuralFrame();
        mainOn=false;multipassOn=false;overridesOn=false;inspectorOn=false;neuralFrame();neuralFrame();
        for(auto health:{PilotState::On,PilotState::Blocked,PilotState::Degraded}){neuralHealth=health;neuralFrame();}
        io.AddMousePosEvent(neuralLampPoint.x,neuralLampPoint.y);neuralFrame();io.AddMouseButtonEvent(0,true);neuralFrame();io.AddMouseButtonEvent(0,false);neuralFrame();neuralFrame();
        Check(neuralSelection==6&&!mainOn&&!multipassOn&&!overridesOn&&!inspectorOn,
            "clicking a service pilot selects its native tab without toggling any processing preference");
        io.AddMousePosEvent(scaleBounds.GetCenter().x,scaleBounds.GetCenter().y);neuralFrame();
        io.AddMouseButtonEvent(0,true);neuralFrame();io.AddMouseButtonEvent(0,false);neuralFrame();
        Check(GImGui->OpenPopupStack.Size>0,"bottom-right Scaling opens its native dropdown");
        io.AddKeyEvent(ImGuiKey_Escape,true);neuralFrame();io.AddKeyEvent(ImGuiKey_Escape,false);neuralFrame();
        io.AddMousePosEvent(languageBounds.GetCenter().x,languageBounds.GetCenter().y);neuralFrame();
        io.AddMouseButtonEvent(0,true);neuralFrame();io.AddMouseButtonEvent(0,false);neuralFrame();
        Check(GImGui->OpenPopupStack.Size>0,"right-aligned Language opens its native dropdown");
        io.AddKeyEvent(ImGuiKey_Escape,true);neuralFrame();io.AddKeyEvent(ImGuiKey_Escape,false);neuralFrame();
        io.AddMousePosEvent(-100,-100);
        bool newPreview=false;
        auto previewFrame=[&]() {
            ImGui::NewFrame();Scope scope;
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({500,300});
            ImGui::Begin("Selected value transition");
            auto* draw=ImGui::GetWindowDrawList();const int start=draw->VtxBuffer.Size;
            const auto textRgb=ImGui::GetColorU32(ImGuiCol_Text)&~IM_COL32_A_MASK;
            if(ImGui::BeginCombo("##Value",newPreview?"Natural":"Cinematic")) {ImGui::Selectable("Natural");ImGui::EndCombo();}
            const ImRect bounds(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
            unsigned alpha=255;
            for(int vertex=start;vertex<draw->VtxBuffer.Size;++vertex) {
                const auto& v=draw->VtxBuffer[vertex];
                if(bounds.Contains(v.pos) && v.pos.x<bounds.Max.x-ImGui::GetFrameHeight() && (v.col&~IM_COL32_A_MASK)==textRgb)
                    alpha=(std::min)(alpha,(v.col>>IM_COL32_A_SHIFT)&255);
            }
            ImGui::End();ImGui::Render();return alpha;
        };
        previewFrame();previewFrame();newPreview=true;
        const unsigned changingAlpha=previewFrame();
        Check(changingAlpha>0 && changingAlpha<230,"selected dropdown values visibly ease into their new caption");
        for(int settle=0;settle<40;++settle) previewFrame();
        Check(previewFrame()==255,"selected dropdown value settles to normal text without changing layout");
        {
            float draft=1.f, committed=1.f; bool released=false; ImVec2 track;
            auto cardSliderFrame=[&]() {
                ImGui::NewFrame();Scope scope;
                ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({500,240});
                ImGui::Begin("Card slider input",nullptr,FixedShellFlags);
                ImGui::SetNextItemWidth(350);
                CardSliderFloat("##DeferredCard",&draft,0,2,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                track=ImGui::GetItemRectMin()+ImVec2(ImGui::GetItemRectSize().x*.8f,ImGui::GetItemRectSize().y*.5f);
                if(ImGui::IsItemDeactivatedAfterEdit()){committed=draft;released=true;}
                Check(!railSlider,"card slider restores scoped drawing mode for Basic sliders");
                ImGui::SameLine();ImGui::SmallButton("Reset");
                ImGui::End();ImGui::Render();
            };
            io.AddMousePosEvent(-100,-100);cardSliderFrame();cardSliderFrame();
            io.AddMousePosEvent(track.x,track.y);cardSliderFrame();
            io.AddMouseButtonEvent(0,true);cardSliderFrame();cardSliderFrame();
            Check(draft>1.4f&&committed==1.f&&!released,"card slider previews without committing while held");
            io.AddMouseButtonEvent(0,false);cardSliderFrame();
            Check(released&&committed==draft,"card slider preserves release-to-commit last-item state");
        }
        for (float width : {480.f,1180.f}) {
            ImVec2 cardPoint{}; float scroll=0;
            auto cardFrame=[&]() {
                ImGui::NewFrame();Scope scope;
                ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,500});
                ImGui::Begin("Static card scrolling",nullptr,FixedShellFlags);
                ImGui::BeginChild("Page",{0,350});
                scroll=ImGui::GetScrollY();
                const bool grid=width>700 && ImGui::BeginTable("Grid",2,ImGuiTableFlags_SizingStretchSame);
                for(int column=0;column<2;++column) {
                    if(grid)ImGui::TableNextColumn();
                    ImGui::PushID(column);
                    {
                        ContentCard card("Card","Owner observations");
                        if(column==0)cardPoint=ImGui::GetCursorScreenPos()+ImVec2(25,35);
                        Check(ImGui::GetScrollY()==0,"content cards never retain independent wheel scrolling");
                        for(int row=0;row<35;++row)ImGui::Text("Observed row %d",row);
                    }
                    ImGui::PopID();
                }
                if(grid)ImGui::EndTable();
                ImGui::EndChild();Check(ImGui::GetScrollY()==0,"card layout never scrolls the outer shell");
                ImGui::End();ImGui::Render();
            };
            io.AddMousePosEvent(-100,-100);cardFrame();cardFrame();cardFrame();
            io.AddMousePosEvent(cardPoint.x,cardPoint.y);cardFrame();
            io.AddMouseWheelEvent(0,-3);cardFrame();cardFrame();
            Check(scroll>0,"wheel over a content card scrolls only its containing page");
        }
#endif
        ImGui::DestroyContext();
#if HAS_SLEEK_UI
        for(int recreation=0;recreation<8;++recreation) {
            ImGui::CreateContext();auto& recreated=ImGui::GetIO();
            recreated.IniFilename=recreated.LogFilename=nullptr;recreated.DisplaySize={800,600};
            recreated.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;recreated.Fonts->AddFontDefault();
            ImGui::NewFrame();Scope scope;ImGui::Begin("Recreated");bool checked=true;
            ImGui::Checkbox("Already enabled",&checked);
            Check(Animate(1234,2,1)==1,"a recreated context starts in its actual state without stale motion");
            ImGui::End();ImGui::Render();ImGui::DestroyContext();
        }
#endif
        std::printf("PASS: %d real UI and motion checks\n",checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr,"FAIL: %s\n",error.what());
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        return 1;
    }
}
