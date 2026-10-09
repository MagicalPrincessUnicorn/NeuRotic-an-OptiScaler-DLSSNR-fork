#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace nh {
struct ArtworkPixels {unsigned width=0,height=0;std::vector<uint8_t> rgba;explicit operator bool()const{return width&&height&&!rgba.empty();}};
bool AllowedArtworkUrl(const std::wstring& url);
ArtworkPixels DecodeArtwork(const std::vector<uint8_t>& bytes,const std::string& role);
ArtworkPixels ExtractGameIcon(const std::wstring& path);
}
