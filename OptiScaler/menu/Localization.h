#pragma once

#include <string>
#include <string_view>

struct ImFontAtlas;

namespace Neurotic
{
struct Language
{
    const char* code;
    const char* name;
};
inline constexpr Language Languages[] = { { "en", "English" },
                                          { "es", "Espa\303\261ol" },
                                          { "fr", "Fran\303\247ais" },
                                          { "de", "Deutsch" },
                                          { "pt", "Portugu\303\252s" } };
inline constexpr int LanguageCount = sizeof(Languages) / sizeof(Languages[0]);
int LanguageIndex(std::string_view code);
void SetLanguage(std::string_view code);
std::string Translate(std::string_view source);
void AddLanguageFonts(ImFontAtlas* atlas, float size);

// Only presentation ranges are translated. Original ImGui labels/IDs and printf
// format strings never change. Nested measure/draw calls reuse the same text.
class LocalizedRange
{
  public:
    LocalizedRange(const char*& begin, const char*& end);
    ~LocalizedRange();
    LocalizedRange(const LocalizedRange&) = delete;
    LocalizedRange& operator=(const LocalizedRange&) = delete;

  private:
    std::string text;
    bool scoped = false;
};
} // namespace Neurotic
