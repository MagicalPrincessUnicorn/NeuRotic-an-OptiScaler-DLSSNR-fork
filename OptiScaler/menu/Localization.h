#pragma once

#include <string>
#include <string_view>
#include <optional>

struct ImFontAtlas;
struct ImFont;

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
// Stable-ID presentation binding. ImGui hashes the unchanged original literal;
// only its measured/drawn range resolves through the selected language layers.
const char* UiLiteral(std::string_view id,const char* original);
const char* UiOptions(std::string_view ids,const char* original);
std::string UiText(std::string_view id);
std::string UiMessage(std::string_view id,const char* english);
const std::string& FontNoticeText();
// A translated printf template is used only after exact canonical token
// validation; original compiled call sites retain their argument ABI/order.
class LocalizedFormat {
 public:explicit LocalizedFormat(const char*& format);
 private:std::string text;
};
class ScopedUiLiteral {
 public:ScopedUiLiteral(std::string_view id,const char* original);~ScopedUiLiteral();
 ScopedUiLiteral(const ScopedUiLiteral&)=delete;ScopedUiLiteral& operator=(const ScopedUiLiteral&)=delete;
 private:const char* pointer=nullptr;std::optional<std::string> previous;
};
void AddLanguageFonts(ImFontAtlas* atlas, float size);
ImFont* AddInterfaceFont(ImFontAtlas* atlas, float size);

// Presentation-only override; the saved menu language is never modified.
class EnglishPreview
{
  public:
    EnglishPreview();
    ~EnglishPreview();
    EnglishPreview(const EnglishPreview&) = delete;
    EnglishPreview& operator=(const EnglishPreview&) = delete;
};

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
