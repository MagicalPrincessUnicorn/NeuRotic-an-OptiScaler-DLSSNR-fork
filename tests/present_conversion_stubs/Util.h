#pragma once
#include <filesystem>
namespace Util
{
inline std::filesystem::path DllPath() { return std::filesystem::temp_directory_path() / "OptiScaler.dll"; }
}
