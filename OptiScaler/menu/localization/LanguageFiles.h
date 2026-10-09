#pragma once
#include "../Localization.h"
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
namespace Neurotic::Localization {
class LanguageDirectoryGuard {
 std::vector<HANDLE> handles;
 public:
 explicit LanguageDirectoryGuard(const std::filesystem::path& path){
  if(!path.is_absolute()||path.lexically_normal()!=path||path.wstring().starts_with(L"\\\\")||path.wstring().find(L':',2)!=std::wstring::npos)throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.local_directory", "Language directory must be an ordinary local path."));
  auto pin=[&](const std::filesystem::path& directory){auto h=CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.directory_unavailable", "Language directory unavailable."));handles.push_back(h);BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.linked_directory", "Linked language directory refused."));};
  try{auto walk=path.root_path();pin(walk);for(const auto& part:path.relative_path()){walk/=part;pin(walk);}}catch(...){for(auto h:handles)CloseHandle(h);handles.clear();throw;}
 }
 ~LanguageDirectoryGuard(){for(auto h:handles)CloseHandle(h);}
 LanguageDirectoryGuard(const LanguageDirectoryGuard&)=delete;
};
inline void DeleteLanguageFile(const std::filesystem::path& path){
 LanguageDirectoryGuard parents(path.parent_path());HANDLE file=CreateFileW(path.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(file==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return;throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.remove_failed", "Language pack could not be removed."));}
 struct Close{HANDLE handle;~Close(){CloseHandle(handle);}} close{file};BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file,&info)||info.nNumberOfLinks!=1||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.linked_pack", "Linked language pack cannot be removed here."));FILE_DISPOSITION_INFO disposition{TRUE};if(!SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof(disposition)))throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.pack_in_use", "Language pack is in use or cannot be removed."));
}
inline std::string ReadLanguageFile(const std::filesystem::path& path){
 if(!path.is_absolute()||path.lexically_normal()!=path||path.wstring().starts_with(L"\\\\")||path.wstring().find(L':',2)!=std::wstring::npos)throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.local_file", "Choose an ordinary local language file."));
 struct Handles{std::vector<HANDLE> values;~Handles(){for(auto h:values)CloseHandle(h);}} handles;
 auto pin=[&](const std::filesystem::path& file,bool directory){HANDLE h=CreateFileW(file.c_str(),directory?FILE_READ_ATTRIBUTES:GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.file_unavailable", "Language file is unavailable."));handles.values.push_back(h);BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory||(!directory&&info.nNumberOfLinks!=1))throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.linked_file", "Linked language files are unsupported."));return h;};
 auto walk=path.root_path();pin(walk,true);for(const auto& part:path.parent_path().relative_path()){walk/=part;pin(walk,true);}auto file=pin(path,false);LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart<2||size.QuadPart>16777216)throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.file_size", "Language file must be between 2 bytes and 16 MiB."));std::string bytes(size_t(size.QuadPart),'\0');DWORD read=0;if(!ReadFile(file,bytes.data(),DWORD(bytes.size()),&read,nullptr)||read!=bytes.size())throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.read_failed", "Language file read failed."));return bytes;
}
inline std::filesystem::path SharedLanguageRoot(){wchar_t path[32768]{};auto size=GetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",path,32768);if(size&&size<32768)return std::filesystem::path(path)/L"Languages";size=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);if(!size||size>=32768)throw std::runtime_error(Neurotic::UiMessage("desktop.languagefiles.user_root_unavailable", "Language user-data root unavailable."));return std::filesystem::path(path)/L"NeuRotic"/L"HubData-v2"/L"Languages";}
}
