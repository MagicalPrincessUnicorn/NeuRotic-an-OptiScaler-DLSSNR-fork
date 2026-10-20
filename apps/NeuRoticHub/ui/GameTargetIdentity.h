#pragma once
#include "ManualLibrary.h"
#include "languages/LanguageMessages.h"
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
namespace nh {
struct QueueIdentityHandle {
 HANDLE value;~QueueIdentityHandle(){CloseHandle(value);}
};
inline std::string QueueIdentityHex(unsigned long long value,unsigned digits){
 static constexpr char alphabet[]="0123456789abcdef";std::string result(digits,'0');
 for(unsigned i=0;i<digits;++i){result[digits-i-1]=alphabet[value&15];value>>=4;}return result;
}
inline std::string QueueHandleIdentity(HANDLE file){
 FILE_ID_INFO info{};
 if(GetFileInformationByHandleEx(file,FileIdInfo,&info,sizeof(info))){
  // The complete opaque ID is required: ReFS basic 64-bit indexes can collide.
  // API success, including an all-zero value, is distinct from unavailable data.
  auto identity="id128:"+QueueIdentityHex(info.VolumeSerialNumber,16)+":";
  for(const auto byte:info.FileId.Identifier)identity+=QueueIdentityHex(byte,2);
  return identity;
 }
 // Preserve the authentic normalized name when the full-ID API is unavailable.
 // Case folding can merge distinct names on case-sensitive filesystems. Encode
 // UTF-16 code units without a lossy character conversion or invented sentinel.
 const DWORD modes[]={FILE_NAME_NORMALIZED|VOLUME_NAME_DOS,FILE_NAME_NORMALIZED|VOLUME_NAME_GUID};
 for(const auto mode:modes){const auto length=GetFinalPathNameByHandleW(file,nullptr,0,mode);
  if(length){std::wstring final(length+1,L'\0');const auto used=GetFinalPathNameByHandleW(file,final.data(),static_cast<DWORD>(final.size()),mode);
   if(used&&used<final.size()){std::string identity="path:";
    for(DWORD i=0;i<used;++i)identity+=QueueIdentityHex(static_cast<unsigned short>(final[i]),4);
    return identity;}}}
 return {};
}
// Session and operation association survive replacement/removal at the same
// accepted path. Physical identity is checked separately at execution/receipt
// and advisory-profile boundaries; it is not a stable UI/draft session key.
inline std::wstring GameTargetKey(const std::string& executable){
 const auto path=std::filesystem::path(Wide(executable));
 if(!path.is_absolute()||path.wstring().starts_with(L"\\\\"))throw std::runtime_error(Neurotic::UiMessage("desktop.gamegrouping.a_local_absolute_game_path_is_required_356caedf","A local absolute game path is required"));
 auto lexical=path.lexically_normal().wstring();std::replace(lexical.begin(),lexical.end(),L'/',L'\\');
 return L"path:"+lexical;
}
// Per-executable state is distinct from lexical search-root containment.
// Include the directory owner: hard-linked executables in different installation
// directories must not share installation state. Unavailable advisory metadata
// falls back to the exact lexical spelling without blocking selection/rendering.
inline std::wstring GamePhysicalTargetKey(const std::string& executable){
 const auto path=std::filesystem::path(Wide(executable));const auto fallback=GameTargetKey(executable);
 HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(file==INVALID_HANDLE_VALUE)return fallback;QueueIdentityHandle ownedFile{file};
 HANDLE directory=CreateFileW(path.parent_path().c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(directory==INVALID_HANDLE_VALUE)return fallback;QueueIdentityHandle ownedDirectory{directory};
 const auto folder=QueueHandleIdentity(directory),identity=QueueHandleIdentity(file);
 if(folder.empty()||identity.empty())return fallback;
 const auto key="physical:"+folder+"|"+identity;return std::wstring(key.begin(),key.end());
}
inline bool GameTargetMatches(const std::string& a,const std::string& b){
 if(GameTargetKey(a)==GameTargetKey(b))return true;
 return GamePhysicalTargetKey(a)==GamePhysicalTargetKey(b);
}

}
