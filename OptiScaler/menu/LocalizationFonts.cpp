#include "Localization.h"
#include <imgui/imgui.h>
#include <filesystem>
#include <Windows.h>

namespace Neurotic
{
void AddLanguageFonts(ImFontAtlas* atlas, float size)
{
    if (atlas->Fonts.empty())
        atlas->AddFontDefault();
    // ImGui 1.92 loads glyphs on demand. Arial supplies the accented Latin
    // characters used by the authored translations without a bundled font download.
    wchar_t windows[MAX_PATH] {};
    if (!GetWindowsDirectoryW(windows, MAX_PATH))
        return;
    const auto fonts = std::filesystem::path(windows) / L"Fonts";
    for (const auto* name : { L"arial.ttf" })
    {
        const auto file = fonts / name;
        if (!std::filesystem::exists(file))
            continue;
        ImFontConfig config;
        config.MergeMode = true;
        atlas->AddFontFromFileTTF(file.string().c_str(), size, &config);
    }
}
} // namespace Neurotic
