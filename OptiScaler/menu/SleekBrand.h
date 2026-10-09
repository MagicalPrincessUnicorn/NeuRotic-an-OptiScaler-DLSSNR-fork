#pragma once
#include <imgui/imgui.h>

namespace Neurotic::Brand
{
// One embedded transparent lockup; native text tint affects only its wordmark.
void Draw(ImDrawList* draw, ImVec2 origin, float height, float available,
          bool markOnly, ImU32 wordmarkTint, ImU32 markTint);
}
