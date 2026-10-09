#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <set>
#include <vector>
#include <string>
#include <stdexcept>
#include <json.hpp>
#pragma comment(lib,"bcrypt.lib")
namespace Neurotic::Semantic::Character {
// Canonical JSON inventory identity for the native Version 1 assembly.
// Exact member/donor differences: overnight-universal-intake/inspector-cohort-change.json.
// Packaging must reject a changed worker cohort until this pin is updated and
// the host rebuilt. The editable manifest is never its own trust anchor.
inline constexpr char InspectorCohortDigest[]="ee2c72fdf1225bbc811d58c6c485f2b983270c44592dfbb8f18952d62e9794cc";
class CharacterCohort {
 std::vector<HANDLE> handles;std::set<std::filesystem::path> directories;
 HANDLE Pin(const std::filesystem::path& path,bool directory){
  auto h=CreateFileW(path.c_str(),directory?FILE_READ_ATTRIBUTES:GENERIC_READ,directory?FILE_SHARE_READ|FILE_SHARE_WRITE:FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
  if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Inspector component missing, changed or in use; reinstall Character Inspector.");
  handles.push_back(h);BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory||(!directory&&info.nNumberOfLinks!=1))throw std::runtime_error("Inspector requires ordinary unlinked components.");return h;
 }
 void Parents(const std::filesystem::path& path){
  if(!path.is_absolute()||path.wstring().starts_with(L"\\\\")||path.lexically_normal()!=path||path.wstring().find(L':',2)!=std::wstring::npos)throw std::runtime_error("Inspector path is not an ordinary local path.");
  auto current=path.root_path();if(directories.insert(current).second)Pin(current,true);for(auto& part:path.relative_path()){current/=part;if(directories.insert(current).second)Pin(current,true);}
 }
 public:
 CharacterCohort()=default;CharacterCohort(const CharacterCohort&)=delete;
 ~CharacterCohort(){for(auto h:handles)CloseHandle(h);}
 static std::string Digest(const std::string& bytes){
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;struct Cleanup{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{algorithm,hash};
  unsigned char digest[32];if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0||BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),ULONG(bytes.size()),0)<0||BCryptFinishHash(hash,digest,32,0)<0)throw std::runtime_error("Inspector integrity check unavailable.");
  std::string result;constexpr char hex[]="0123456789abcdef";for(auto value:digest){result+=hex[value>>4];result+=hex[value&15];}return result;
 }
 std::string ReadPinned(const std::filesystem::path& path,size_t maximum){
  Parents(path.parent_path());auto file=Pin(path,false);LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart<0||size.QuadPart>static_cast<LONGLONG>(maximum))throw std::runtime_error("Inspector component exceeds its bound.");
  std::string bytes(size_t(size.QuadPart),'\0');DWORD count=0;if(!ReadFile(file,bytes.data(),DWORD(bytes.size()),&count,nullptr)||count!=bytes.size())throw std::runtime_error("Inspector component read failed.");return bytes;
 }
 void Verify(const std::filesystem::path& root,const std::string& admitted=InspectorCohortDigest){
  auto manifest=nlohmann::json::parse(ReadPinned(root/L"DISTRIBUTION-MANIFEST.json",1048576));const auto& files=manifest.at("files");
  if(!files.is_array()||files.empty()||files.size()>1024||Digest(files.dump())!=admitted)throw std::runtime_error("Inspector distribution does not match this NeuRotic build. Reinstall Character Inspector.");
  std::set<std::filesystem::path> seen;uint64_t total=0;
  for(const auto& entry:files){auto relative=std::filesystem::path(entry.at("path").get<std::string>());if(relative.empty()||relative.is_absolute()||relative.has_root_name()||relative.lexically_normal()!=relative||relative.wstring().find(L':')!=std::wstring::npos||!seen.insert(relative).second)throw std::runtime_error("Invalid Inspector inventory path.");for(const auto& part:relative)if(part==L".."||part==L".")throw std::runtime_error("Invalid Inspector inventory path.");
   const auto count=entry.at("bytes").get<uint64_t>();total+=count;if(count>268435456||total>536870912)throw std::runtime_error("Inspector inventory exceeds its bound.");auto bytes=ReadPinned(root/relative,size_t(count));if(bytes.size()!=count||Digest(bytes)!=entry.at("sha256").get<std::string>())throw std::runtime_error("Inspector component changed: "+relative.generic_string()+". Reinstall Character Inspector.");
  }
  size_t visited=0;
  for(auto it=std::filesystem::recursive_directory_iterator(root);it!=std::filesystem::recursive_directory_iterator();++it){
   if(++visited>4096||it.depth()>16)throw std::runtime_error("Inspector directory exceeds its bound.");
   auto path=it->path();auto attributes=GetFileAttributesW(path.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Inspector contains a linked component.");
   if(it->is_directory())continue;auto extension=path.extension().wstring();for(auto& c:extension)c=towlower(c);
   if((extension==L".py"||extension==L".pyc"||extension==L".pyd"||extension==L".pth"||extension==L".dll"||extension==L".exe"||extension==L".zip")&&!seen.contains(path.lexically_relative(root)))throw std::runtime_error("Inspector contains unrecognized executable code. Reinstall Character Inspector.");
  }
 }
};
}
