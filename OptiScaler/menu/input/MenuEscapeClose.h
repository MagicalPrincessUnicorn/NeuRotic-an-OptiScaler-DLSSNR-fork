#pragma once

#include "MenuInputPolicy.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace OptiInput
{
inline bool HandleEscapeClose(EscapeCloseGesture& gesture, bool enabled, bool visible,
                              bool focused, bool capturingKey, bool pressed, bool released)
{
    const bool available = enabled && visible && focused && !capturingKey;
    // ActiveId is current even when WantTextInput still reflects the frame before activation.
    const bool activeControl = ImGui::IsAnyItemActive();
    const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool eligible = available && !activeControl && !popupOpen && !ImGui::GetIO().WantTextInput;

    // Allowing gameplay keyboard input disables ImGui keyboard handling. Preserve Escape's
    // usual active-control/dropdown dismissal here without enabling the other navigation keys.
    if (available && pressed && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoKeyboard))
    {
        if (activeControl)
            ImGui::ClearActiveID();
        else if (popupOpen)
        {
            auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
            const auto* window = popups.back().Window;
            if (window && !(window->Flags & ImGuiWindowFlags_Modal))
                ImGui::ClosePopupToLevel(popups.Size - 1, true);
        }
    }
    return gesture.Update(eligible, pressed, released);
}
}
