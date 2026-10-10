#pragma once
#include "../NeuRoticWindowWorker/control/ImportDirectoryLeases.h"
#include <stdexcept>
#include <string_view>

namespace neurotic::model {
inline constexpr wchar_t FolderName[]=L"Place NVNGX DLSS NR File Here";
inline constexpr wchar_t FileName[]=L"nvngx_dlssnr.dll";
inline constexpr std::string_view Instructions=
 "Place your compatible nvngx_dlssnr.dll directly in this folder.\r\n"
 "NeuRotic verifies it automatically before Neural Rendering can start.\r\n"
 "Keep the file here; nested folders and linked paths are not supported.\r\n"
 "NeuRotic does not distribute this private model file.\r\n";
inline std::filesystem::path Folder(const std::filesystem::path& runtimeRoot){return runtimeRoot/FolderName;}
// Caller holds the ordinary directory namespace for the lifetime of this write.
// CREATE_NEW preserves an existing README, including the user's edits.
inline void EnsureReadme(const std::filesystem::path& folder){
 auto path=folder/L"README.txt";
 HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(file==INVALID_HANDLE_VALUE){
  if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error("Model folder instructions could not be created");
  auto attrs=GetFileAttributesW(path.c_str());
  if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))throw std::runtime_error("Linked model folder instructions are refused");
  return;
 }
 DWORD written=0;bool okay=WriteFile(file,Instructions.data(),DWORD(Instructions.size()),&written,nullptr)&&written==Instructions.size()&&FlushFileBuffers(file);
 CloseHandle(file);if(!okay)throw std::runtime_error("Model folder instructions could not be saved");
}
inline void Prepare(const std::filesystem::path& folder){
 nrw::detail::ImportDirectoryLeases directories;std::string reason;
 if(!directories.Open(folder,reason))throw std::runtime_error(reason);
 EnsureReadme(directories.Root());
}
}
