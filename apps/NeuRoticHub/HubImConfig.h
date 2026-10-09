#pragma once
#include "../../OptiScaler/include/imgui/imconfig.h"
#undef IMGUI_ENABLE_FREETYPE
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#define IMGUI_USE_WCHAR32
// The shared 1.92 WIP text renderer misidentifies 16-bit VtxOffset changes as
// font texture changes. DX11 supports 32-bit indices; keep this choice Hub-only.
#define ImDrawIdx unsigned int
