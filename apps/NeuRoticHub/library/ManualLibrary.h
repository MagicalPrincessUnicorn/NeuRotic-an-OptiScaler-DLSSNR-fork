#pragma once
#include <string>
#include <filesystem>
#include <cstdint>
namespace nh {
struct Target { std::string path; std::string name; bool suitable=false; std::string reason; int bitness=0; uint16_t machine=0; std::string proxyRecommendation; };
Target InspectExecutable(const std::filesystem::path& path);
std::wstring Wide(const std::string& value);
std::string Utf8(const std::wstring& value);
}
