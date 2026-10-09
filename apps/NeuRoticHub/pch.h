#pragma once
#include <windows.h>
inline HINSTANCE dllModule = GetModuleHandleW(nullptr);
// The current Win32 ImGui backend includes pch.h but uses no injected-runtime
// symbols. Standalone host intentionally supplies an empty include boundary.
