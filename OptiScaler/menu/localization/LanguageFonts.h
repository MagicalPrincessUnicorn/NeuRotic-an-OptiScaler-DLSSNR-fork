#pragma once
#include <string_view>
#include <string>
struct ImFontAtlas;
struct ImFont;
namespace Neurotic::Localization {
size_t MissingGlyphs(ImFont*,float size,std::string_view text);
void AddFontFallbacks(ImFontAtlas* atlas,float size,std::string_view locale,bool useInstalled=true);
bool QualifiedInitialScript(std::string_view locale);
std::string FontNotices();
}
