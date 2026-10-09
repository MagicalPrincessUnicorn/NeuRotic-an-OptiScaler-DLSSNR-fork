#include <menu/Localization.h>
#pragma once
#include "ManualLibrary.h"
#include <windows.h>
#include <vector>
namespace nh {
inline std::filesystem::path EnsureSelectedGameScreenshots(const Target& target){
 if(!target.suitable||target.path.empty())throw std::runtime_error(Neurotic::UiMessage("desktop.gamescreenshots.choose_an_available_game_executable_first_5c667073", "Choose an available game executable first."));
 auto root=std::filesystem::path(Wide(target.path)).parent_path();auto text=root.wstring();
 if(!root.is_absolute()||text.starts_with(L"\\\\")||text.find(L':',2)!=std::wstring::npos||root.lexically_normal()!=root)throw std::runtime_error(Neurotic::UiMessage("desktop.gamescreenshots.a_local_ordinary_game_folder_is_required_288ba07f", "A local ordinary game folder is required."));
 const bool legacy=target.bitness==32;
 if(target.bitness!=32&&target.bitness!=64)throw std::runtime_error(Neurotic::UiMessage("desktop.gamescreenshots.game_architecture_is_unavailable_8ce3f114", "Game architecture is unavailable."));
 auto folder=legacy?root/L"NeuRotic/Prepared/NeuRotic.GpuHost/NeuroticScreenshots":root/L"NeuroticScreenshots";
 struct Pins{std::vector<HANDLE> files;~Pins(){for(auto h:files)CloseHandle(h);}} pins;
 auto pin=[&](const std::filesystem::path& path){auto h=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.gamescreenshots.game_screenshots_folder_is_unavailable_or_inacce_e968e740", "Game screenshots folder is unavailable or inaccessible."));pins.files.push_back(h);BY_HANDLE_FILE_INFORMATION i{};if(!GetFileInformationByHandle(h,&i)||(i.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error(Neurotic::UiMessage("desktop.gamescreenshots.linked_or_unexpected_screenshots_folder_804d6584", "Linked or unexpected screenshots folder."));};
 auto walk=folder.root_path();pin(walk);
 for(const auto& part:folder.relative_path()){walk/=part;
  if(walk==folder&&!legacy&&GetFileAttributesW(walk.c_str())==INVALID_FILE_ATTRIBUTES){auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)throw std::runtime_error(Neurotic::UiMessage("desktop.gamescreenshots.screenshots_folder_access_failed_f1d51a80", "Screenshots folder access failed."));if(!CreateDirectoryW(walk.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error(Neurotic::UiMessage("desktop.gamescreenshots.screenshots_folder_could_not_be_created_616d0d31", "Screenshots folder could not be created."));}
  pin(walk);
 }
 return folder;
}
}
