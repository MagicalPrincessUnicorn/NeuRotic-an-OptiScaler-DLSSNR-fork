#include "Localization.h"
#include "localization/LanguageFonts.h"
#include "localization/LanguageRuntime.h"
#include <imgui/imgui.h>
#include <filesystem>
#include <Windows.h>

namespace Neurotic
{
ImFont* AddInterfaceFont(ImFontAtlas* atlas, float size)
{
    wchar_t windows[MAX_PATH] {};
    if (!GetWindowsDirectoryW(windows, MAX_PATH)) return nullptr;
    const auto file = std::filesystem::path(windows) / L"Fonts" / L"segoeui.ttf";
    if (!std::filesystem::exists(file)) return nullptr;
    return atlas->AddFontFromFileTTF(file.string().c_str(),size);
}
void AddLanguageFonts(ImFontAtlas* atlas, float size)
{
    Localization::AddFontFallbacks(atlas,size,Localization::SelectedLocale());
}
} // namespace Neurotic
