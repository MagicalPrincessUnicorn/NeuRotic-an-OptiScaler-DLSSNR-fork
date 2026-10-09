#pragma once
#include <string>
#include <filesystem>
namespace nh {
struct Target { std::string path; std::string name; bool suitable=false; std::string reason; int bitness=0; };
Target InspectExecutable(const std::filesystem::path& path);
std::wstring Wide(const std::string& value);
std::string Utf8(const std::wstring& value);
}
