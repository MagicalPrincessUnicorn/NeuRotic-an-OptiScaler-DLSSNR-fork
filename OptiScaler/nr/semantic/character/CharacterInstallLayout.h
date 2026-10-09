#pragma once
#include <filesystem>
namespace Neurotic::Semantic::Character {
inline constexpr wchar_t RequiredCharacterInspectorVersion[] = L"Version 1";
inline std::filesystem::path CharacterInspectorRootForModule(const std::filesystem::path& modulePath){
 auto root=modulePath.parent_path();
 // The retired prepared-helper location is a known installation layout,
 // never a new shipping route or an absolute pointer to an application folder.
 if(root.filename()==L"NeuRotic.GpuHost"&&root.parent_path().filename()==L"Prepared"&&
    root.parent_path().parent_path().filename()==L"NeuRotic")root=root.parent_path().parent_path().parent_path();
 return root/L"OptiScaler"/L"CharacterInspector"/RequiredCharacterInspectorVersion;
}
}
