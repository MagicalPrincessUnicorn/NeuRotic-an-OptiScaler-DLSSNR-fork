#include <menu/Localization.h>
#pragma once
#include "UserFile.h"
#include "install/OperationController.h"
#include <fstream>
namespace nh {
// Participating App versions have exactly one mutable state owner per user.
// A separate namespace isolates documents from uncooperative older releases.
class UserDataSession {
 HANDLE mutex=nullptr;bool owned=false;
 public:
 explicit UserDataSession(const std::filesystem::path& root){
  uint64_t hash=14695981039346656037ull;for(auto c:root.lexically_normal().wstring()){hash^=towlower(c);hash*=1099511628211ull;}
  auto name=L"Global\\NeuRotic-UserData-v2-"+std::to_wstring(hash);mutex=CreateMutexW(nullptr,FALSE,name.c_str());if(!mutex)throw std::runtime_error(Neurotic::UiMessage("desktop.userdatasession.app_data_admission_unavailable_a1dd237b", "App data admission unavailable."));
  auto result=WaitForSingleObject(mutex,0);owned=result==WAIT_OBJECT_0||result==WAIT_ABANDONED;
 }
 ~UserDataSession(){Release();if(mutex)CloseHandle(mutex);}
 void Release(){if(owned){ReleaseMutex(mutex);owned=false;}}
 UserDataSession(const UserDataSession&)=delete;
 bool Owns()const{return owned;}
};
inline void ImportLegacyUserDocuments(const std::filesystem::path& legacy,const std::filesystem::path& destination){
 // Keep the one-time import marker outside the clearable data directory so
 // Clear App Data cannot resurrect old preferences on the following launch.
 auto marker=destination.parent_path()/(destination.filename().wstring()+L".legacy-import.json");if(std::filesystem::exists(marker)||legacy==destination)return;
 Json receipt={{"schemaVersion",1},{"source",legacy.generic_string()},{"imported",Json::array()},{"skipped",Json::array()}};
 std::vector<std::filesystem::path> candidates{L"library.json",L"known-installations.json",L"Runtime/anything-profiles.json"};
 auto profiles=legacy/L"profiles";
 // Never walk a legacy link or recursively copy runtime/build trees.
 bool ordinary=true;for(auto walk=legacy;!walk.empty();walk=walk.parent_path()){auto a=GetFileAttributesW(walk.c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))ordinary=false;if(walk==walk.parent_path())break;}
 if(ordinary&&std::filesystem::is_directory(profiles)&&!(GetFileAttributesW(profiles.c_str())&FILE_ATTRIBUTE_REPARSE_POINT)){
  for(const auto& item:std::filesystem::directory_iterator(profiles)){if(candidates.size()>=10003)break;if(item.path().extension()==L".json")candidates.push_back(std::filesystem::path(L"profiles")/item.path().filename());}
 }
 for(const auto& relative:candidates){auto source=legacy/relative,target=destination/relative;if(!ordinary||std::filesystem::exists(target)||!std::filesystem::exists(source))continue;
  try{
   for(auto walk=source;walk!=legacy;walk=walk.parent_path()){auto a=GetFileAttributesW(walk.c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error(Neurotic::UiMessage("desktop.userdatasession.linked_legacy_document_da7ed84a", "Linked legacy document"));}
   HANDLE file=CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.userdatasession.legacy_document_in_use_9f4766a5", "Legacy document in use"));
   struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{file};BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
   if(!GetFileInformationByHandle(file,&info)||info.nNumberOfLinks!=1||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||!GetFileSizeEx(file,&size)||size.QuadPart<2||size.QuadPart>16777216)throw std::runtime_error(Neurotic::UiMessage("desktop.userdatasession.legacy_document_unavailable_or_too_large_0ccae32a", "Legacy document unavailable or too large"));
   std::string bytes(size_t(size.QuadPart),'\0');DWORD read=0;if(!ReadFile(file,bytes.data(),DWORD(bytes.size()),&read,nullptr)||read!=bytes.size())throw std::runtime_error(Neurotic::UiMessage("desktop.userdatasession.legacy_document_read_failed_cd8ca9a4", "Legacy document read failed"));
   auto document=Json::parse(bytes);if(!document.is_object())throw std::runtime_error(Neurotic::UiMessage("desktop.userdatasession.legacy_document_invalid_fd9fbde0", "Legacy document invalid"));SaveUserFile(target,bytes);receipt["imported"].push_back(relative.generic_string());
  }catch(const std::exception& error){receipt["skipped"].push_back({{Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),relative.generic_string()},{Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason"),error.what()}});}
 }
 SaveUserFile(marker,receipt.dump(2));
}
}
