#include <menu/Localization.h>
#pragma once
#include "SleekShell.h"
#include <optional>

namespace Neurotic::Sleek
{
inline thread_local unsigned measuringDescendants = 0;
inline void BeginSectionLayout()
{
    if(!Enabled()) return;
    auto* parent=ImGui::GetCurrentWindow();
    auto& state=parent->StateStorage;
    const float bottom=state.GetFloat(Key(parent->ID,32,2),0);
    // Spacing() has no item ID. Normalize only adjacent cards in the same flow;
    // a real intervening widget, separator or table column keeps its own layout.
    if(state.GetInt(Key(parent->ID,32,0),-1)==ImGui::GetFrameCount() &&
       static_cast<ImGuiID>(state.GetInt(Key(parent->ID,32,1),0))==GImGui->LastItemData.ID &&
       parent->DC.CursorPos.y>=bottom &&
       std::abs(parent->DC.CursorPos.x-state.GetFloat(Key(parent->ID,32,3),0))<.1f)
    {
        parent->DC.CursorPos.y=bottom+ImGui::GetStyle().ItemSpacing.y;
        parent->DC.CursorMaxPos.y=(std::min)(parent->DC.CursorMaxPos.y,parent->DC.CursorPos.y);
    }
}
inline void EndSectionLayout()
{
    if(!Enabled()) return;
    auto* parent=ImGui::GetCurrentWindow();
    auto& state=parent->StateStorage;
    state.SetInt(Key(parent->ID,32,0),ImGui::GetFrameCount());
    state.SetInt(Key(parent->ID,32,1),static_cast<int>(GImGui->LastItemData.ID));
    state.SetFloat(Key(parent->ID,32,2),ImGui::GetItemRectMax().y);
    state.SetFloat(Key(parent->ID,32,3),ImGui::GetItemRectMin().x);
}
inline bool InstantLayout()
{
    return !Enabled() || reducedMotion || (GImGui->NavCursorVisible && GImGui->NavInputSource==ImGuiInputSource_Keyboard);
}
// A content-sized region whose visible height follows its measured layout. Its state belongs
// to the parent ImGui window, so nested panels, languages and recreated contexts stay isolated.
class AnimatedRegion
{
    ImGuiWindow* parent;
    ImGuiID id;
    bool open, begun = false;
    bool instant = false;
    bool userInstant = false, measureChildren = false;
    float height = 0;
  public:
    AnimatedRegion(const char* label, bool requested) : parent(ImGui::GetCurrentWindow()),
        id(ImGui::GetID(label)), open(requested)
    {
        auto& state = parent->StateStorage;
        userInstant = InstantLayout();
        instant = userInstant || measuringDescendants != 0;
        const auto measured = Key(id,20,2);
        const float target = open ? state.GetFloat(measured,0) : 0;
        height = AnimateLinear(id,20,target);
        if(instant) height=target;
        if (!open && height < .5f) {
            state.SetFloat(Key(id,20,0),0); height=0; return;
        }
        begun = true;
        // AlwaysAutoResize measures even when the body is clipped to its first pixel.
        // The constraints determine the visible height, never the natural content height.
        const float visible = (std::max)(1.0f,height);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(0,0,0,0));
        if(!instant) ImGui::SetNextWindowSizeConstraints({0,visible},{FLT_MAX,visible});
        ImGui::BeginChild(id,{0,0},ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize |
            ImGuiChildFlags_NavFlattened,ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor(); ImGui::PopStyleVar();
        if(!instant) {
            auto* body=ImGui::GetCurrentWindow();
            // Native children retain a minimum height (including rounded corners).
            // This transparent animation region must reserve and clip the actual
            // visible height, otherwise collapse stalls at that minimum then jumps.
            body->Size.y=body->SizeFull.y=height;
            body->OuterRectClipped.Max.y=(std::min)(body->OuterRectClipped.Max.y,body->Pos.y+height);
            body->ClipRect.Max.y=(std::min)(body->ClipRect.Max.y,body->Pos.y+height);
            body->DrawList->PopClipRect();
            body->DrawList->PushClipRect(body->ClipRect.Min,body->ClipRect.Max,false);
        }
        if(userInstant) {
            auto* ancestor=parent;
            while(ancestor->ParentWindow && ancestor->ChildFlags & ImGuiChildFlags_AutoResizeY) ancestor=ancestor->ParentWindow;
            auto* body=ImGui::GetCurrentWindow();
            body->ClipRect.Max.y=ancestor->ClipRect.Max.y;
            body->DrawList->PopClipRect();
            body->DrawList->PushClipRect(body->ClipRect.Min,body->ClipRect.Max,false);
        }
        ImGui::BeginDisabled(!open);
        // During a parent's reveal, descendant layout settles immediately behind
        // that parent's clip. Cascaded transitions otherwise discover new rows late.
        measureChildren=instant || !open || target<=0 || height<target-.5f;
        if(measureChildren) ++measuringDescendants;
    }
    bool Visible() const { return begun; }
    bool ImmediateLayout() const { return instant; }
    ~AnimatedRegion()
    {
        if (!begun) return;
        if(measureChildren) --measuringDescendants;
        auto* body = ImGui::GetCurrentWindow();
        const float measured=(std::max)(0.0f,body->DC.CursorMaxPos.y-body->DC.CursorStartPos.y);
        parent->StateStorage.SetFloat(Key(id,20,2),measured);
        if(instant) {
            body->Size.y=body->SizeFull.y=measured;
            parent->StateStorage.SetFloat(Key(id,20,0),measured);
            parent->StateStorage.SetInt(Key(id,20,1),ImGui::GetFrameCount());
            parent->StateStorage.SetFloat(Key(id,20,3),measured);
            parent->StateStorage.SetFloat(Key(id,20,4),0);
        }
        ImGui::EndDisabled(); ImGui::EndChild();
        // EndChild adds an item gap, but the region already lives inside a section's gap.
        parent->DC.CursorPos.y-=ImGui::GetStyle().ItemSpacing.y;
    }
    AnimatedRegion(const AnimatedRegion&)=delete;
    AnimatedRegion& operator=(const AnimatedRegion&)=delete;
};
class ScopedTreeNode
{
    bool tree = false;
    float followingRowY = 0;
    std::optional<AnimatedRegion> body;
  public:
    explicit ScopedTreeNode(const char* label)
    {
        const bool open=ImGui::TreeNodeEx(label,ImGuiTreeNodeFlags_NoTreePushOnOpen);
        // Preserve the native indentation/ID stack, including during the disabled close tail.
        if(open || Enabled()) {
            ImGui::TreePush(label); tree=true;
            followingRowY=ImGui::GetCursorScreenPos().y;
            if(Enabled()) ImGui::GetCurrentWindow()->DC.CursorPos.y-=ImGui::GetStyle().ItemSpacing.y;
            body.emplace("##TreeBody",open);
            if(Enabled() && body->Visible()) ImGui::Spacing();
        }
    }
    bool IsOpen() const { return body && body->Visible(); }
    ~ScopedTreeNode()
    {
        body.reset();
        if(tree) {
            ImGui::TreePop();
            // Retain the native row gap after a closed tree. Only cursor flow
            // needs it; keeping it in CursorMax would leave a blank body row.
            if(Enabled()) {
                auto& cursor=ImGui::GetCurrentWindow()->DC.CursorPos.y;
                cursor=(std::max)(cursor,followingRowY);
            }
        }
    }
};
}

class ScopedCollapsingHeader
{
  public:
    explicit ScopedCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0,
                                   bool* enabled = nullptr, const char* toggleLabel = Neurotic::UiLiteral("ingame.objectruleeditor.enabled_90f9fd68", "Enabled"),
                                   bool orangeBold = false, const ImVec4* boldColor = nullptr)
    {
        Neurotic::Sleek::BeginSectionLayout();
        ImGui::PushID(label);

        // Preserve child identity and reveal ownership without decorative padding and borders.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##CollapsingHeaderChild", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        const bool inlineToggle = enabled && ImGui::GetContentRegionAvail().x >=
            ImGui::CalcTextSize(label, nullptr, true).x + ImGui::CalcTextSize(toggleLabel).x + ImGui::GetFrameHeight() * 3;
        if (inlineToggle && ImGui::BeginTable("##HeaderControls", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.provider.dfca5da56b8c", "Section"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.provider.3d03a1dea561", "Toggle"), ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableNextColumn();
            _headerOpen = ImGui::CollapsingHeader(label, flags);
            ImGui::TableNextColumn();
            ImGui::Checkbox(toggleLabel, enabled);
            ImGui::EndTable();
        }
        else
        {
            if (orangeBold) ImGui::PushStyleColor(ImGuiCol_Text,
                boldColor ? *boldColor : ImVec4(1.0f, 0.48f, 0.10f, 1.0f));
            auto* draw = ImGui::GetWindowDrawList();
            const auto titlePosition = ImGui::GetCursorScreenPos();
            _headerOpen = ImGui::CollapsingHeader(label, flags);
            if (orangeBold)
            {
                // A second subpixel-offset text stroke gives the current localized font a bold face.
                const ImU32 orange = ImGui::GetColorU32(ImGuiCol_Text);
                const auto padding = ImGui::GetStyle().FramePadding;
                draw->PushClipRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true);
                // ImGui hides ##/### IDs on its own pass. Hide them on this bold stroke too.
                const char* labelEnd = label;
                while (*labelEnd && !(labelEnd[0] == '#' && labelEnd[1] == '#')) ++labelEnd;
                const std::string displayed(label, labelEnd);
                draw->AddText(ImVec2(titlePosition.x + ImGui::GetFontSize() + padding.x * 3.0f + 0.6f,
                                    titlePosition.y + padding.y), orange,
                              Neurotic::Translate(displayed.c_str()).c_str());
                draw->PopClipRect();
                ImGui::PopStyleColor();
            }
            if (enabled) ImGui::Checkbox(toggleLabel, enabled);
        }
        if (Neurotic::Sleek::Enabled()) {
            // Put the header-to-content gap inside the shrinking region as well.
            // Leaving it outside causes a final full-spacing jump when the body ends.
            ImGui::SetCursorPosY(ImGui::GetCursorPosY()-ImGui::GetStyle().ItemSpacing.y);
            _body.emplace("##AnimatedBody",_headerOpen);
            _headerOpen = _body->Visible();
            if(_headerOpen) ImGui::Spacing();
        } else if (_headerOpen) ImGui::Spacing();
        _active = true;
    }

    bool IsHeaderOpen() const { return _headerOpen; }

    ~ScopedCollapsingHeader()
    {
        if (_active)
        {
            const bool immediate=Neurotic::Sleek::InstantLayout() || (_body && _body->ImmediateLayout());
            _body.reset();
            if(immediate) {
                auto* window=ImGui::GetCurrentWindow();
                window->Size.y=window->SizeFull.y=ImGui::CalcWindowNextAutoFitSize(window).y;
            }
            ImGui::EndChild();
            Neurotic::Sleek::EndSectionLayout();
            ImGui::PopID();
        }
    }

  private:
    std::optional<Neurotic::Sleek::AnimatedRegion> _body;
    bool _active = false;
    bool _headerOpen = false;
};
