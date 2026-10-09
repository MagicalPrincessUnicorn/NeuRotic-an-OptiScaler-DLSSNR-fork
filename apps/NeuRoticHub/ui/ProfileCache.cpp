#include <menu/Localization.h>
#include "ProfileCache.h"
#include "ManualLibrary.h"
#include <objbase.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "storage/UserFile.h"

namespace nh {
namespace {
bool RecordedInstall(const Json& state){return state.is_object()&&((state.value("kind",std::string{})=="neurotic-game-install"&&state.value("schemaVersion",0)==3&&(state.value("status",std::string{})=="Installed"||state.value("status",std::string{})=="Partial"))||(state.value("kind",std::string{})=="neurotic-public-install"&&state.value("schema_version",0)==2&&state.value("status",std::string{})=="installed-verified"));}
// Match the bounded installer response: a 4 MiB Object Rules hex value still
// needs room for the other settings, installation receipt and cache metadata.
constexpr size_t MaximumProfileBytes = 16 * 1024 * 1024;
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;
};
std::wstring Key(const std::string& executable) {
    auto path=std::filesystem::path(Wide(executable));
    if(!path.is_absolute() || path.wstring().starts_with(L"\\\\"))
        throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.local_absolute_profile_path_required_987752e0", "Local absolute profile path required"));
    auto key=path.lexically_normal().wstring();
    std::replace(key.begin(),key.end(),L'/',L'\\');
    std::transform(key.begin(),key.end(),key.begin(),[](wchar_t c){return (wchar_t)towlower(c);});
    return key;
}
void NoLinks(const std::filesystem::path& path) {
    for(auto walk=std::filesystem::absolute(path);!walk.empty();) {
        auto attributes=GetFileAttributesW(walk.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES) {
            if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.linked_profile_path_refused_5b1b1f1e", "Linked profile path refused"));
        } else if(GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND)
            throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.profile_path_unavailable_af881d35", "Profile path unavailable"));
        auto parent=walk.parent_path();if(parent==walk)break;walk=parent;
    }
}
Json Stamp(const std::filesystem::path& path) {
    NoLinks(path);
    Handle file(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                           nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(file.value==INVALID_HANDLE_VALUE) {
        if(GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND)return nullptr;
        throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.profile_input_unavailable_e9ac0974", "Profile input unavailable"));
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.value,&info) || info.nNumberOfLinks!=1 ||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
        throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.profile_input_identity_unavailable_25bb705b", "Profile input identity unavailable"));
    auto pair=[](DWORD high,DWORD low){return (uint64_t(high)<<32)|low;};
    return {{"volume",info.dwVolumeSerialNumber},{"file",pair(info.nFileIndexHigh,info.nFileIndexLow)},
            {"bytes",pair(info.nFileSizeHigh,info.nFileSizeLow)},
            {"written",pair(info.ftLastWriteTime.dwHighDateTime,info.ftLastWriteTime.dwLowDateTime)}};
}
bool Matches(const std::string& executable,const Json& inspection) {
    auto target=InspectExecutable(Wide(executable));
    return target.suitable && inspection.is_object() && inspection.contains("target") && inspection["target"].is_object() &&
           inspection["target"].value("bitness",64)==target.bitness &&
           inspection["target"].contains("executable") && inspection["target"]["executable"].is_string() &&
           Key(inspection["target"]["executable"].get<std::string>())==Key(executable) &&
           inspection.contains("state") && (inspection["state"].is_null()||inspection["state"].is_object());
}
Json Inputs(const std::string& executable,const Json& inspection) {
    auto exe=std::filesystem::path(Wide(executable));auto root=exe.parent_path();
    auto executableStamp=Stamp(exe);if(executableStamp.is_null())throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.game_executable_missing_66c72fc6", "Game executable missing"));
    Json result={{"executable",executableStamp},{"settings",Stamp(root/L"OptiScaler.ini")},
                 {"installation",Stamp(root/L"NeuRotic/Installer/Current-Install.json")}};
    if(inspection["state"].is_object()) {
        auto proxy=inspection["state"].value("proxy",inspection["state"].value("selected_proxy",std::string()));
        if(!proxy.empty()) {
            static const char* allowed[]={Neurotic::UiLiteral("desktop.hubshell.dxgi_dll_2766e740", "dxgi.dll"),Neurotic::UiLiteral("desktop.option.d508058f7eba", "winmm.dll"),Neurotic::UiLiteral("desktop.option.e4c456927fa4", "version.dll"),Neurotic::UiLiteral("desktop.option.dcc94e2ae3ad", "dbghelp.dll"),Neurotic::UiLiteral("desktop.option.cbf80229b83a", "d3d12.dll"),Neurotic::UiLiteral("desktop.option.e3dbb87412e9", "wininet.dll"),Neurotic::UiLiteral("desktop.option.bf5d99b5c9ae", "winhttp.dll"),Neurotic::UiLiteral("desktop.option.fd4503a9892c", "OptiScaler.asi"),Neurotic::UiLiteral("desktop.option.5990097c3bc5", "OptiScaler.dll")};
            if(std::none_of(std::begin(allowed),std::end(allowed),[&](auto value){return proxy==value;}))
                throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.unexpected_cached_proxy_aebb0820", "Unexpected cached proxy"));
            result["proxy"]=Stamp(root/Wide(proxy));
        }
    }
    return result;
}
std::filesystem::path CachePath(const std::string& executable) {
    auto key=Utf8(Key(executable));HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))
        throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.profile_key_unavailable_73e25cbe", "Profile key unavailable"));
    BYTE digest[32]{};DWORD size=sizeof(digest);
    bool ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash) &&
            CryptHashData(hash,reinterpret_cast<const BYTE*>(key.data()),(DWORD)key.size(),0) &&
            CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0);
    if(hash)CryptDestroyHash(hash);CryptReleaseContext(provider,0);
    if(!ok||size!=32)throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.profile_key_unavailable_73e25cbe", "Profile key unavailable"));
    std::wstring name;constexpr wchar_t digits[]=L"0123456789abcdef";
    for(auto byte:digest){name+=digits[byte>>4];name+=digits[byte&15];}
    return UserRoot()/L"profiles"/(name+L".json");
}
std::string Read(const std::filesystem::path& path) {
    auto stamp=Stamp(path);if(stamp.is_null())throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.no_saved_profile_8034e18f", "No saved profile"));
    auto length=stamp["bytes"].get<uint64_t>();if(!length||length>MaximumProfileBytes)throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.profile_size_refused_c3d9762a", "Profile size refused"));
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(file.value==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.profile_cannot_be_opened_a5fea98f", "Profile cannot be opened"));
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.value,&info)||info.nNumberOfLinks!=1||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
       ((uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow)!=length)
        throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.profile_changed_while_reading_d79e62ce", "Profile changed while reading"));
    std::string bytes(size_t(length),'\0');DWORD read=0;
    if(!ReadFile(file.value,bytes.data(),(DWORD)bytes.size(),&read,nullptr)||read!=bytes.size())
        throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.profile_read_failed_ab63bdba", "Profile read failed"));
    return bytes;
}
std::string Now() {
    SYSTEMTIME time{};GetSystemTime(&time);char text[40]{};
    std::snprintf(text,sizeof(text),"%04u-%02u-%02uT%02u:%02u:%02uZ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
    return text;
}
}
std::optional<CachedGameProfile> LoadGameProfile(const std::string& executable) noexcept {
    try {
        auto document=Json::parse(Read(CachePath(executable)));
        if(document.value("schemaVersion",0)!=1 || !document.contains("inspection") ||
           Key(document.at("executable").get<std::string>())!=Key(executable) ||
           !Matches(executable,document["inspection"]) ||
           document.at("inputs")!=Inputs(executable,document["inspection"]))return {};
        auto when=document.at("checkedUtc").get<std::string>();if(when.size()!=20)return {};
        return CachedGameProfile{document["inspection"],when};
    }catch(...){return {};}
}
bool SaveGameProfile(const std::string& executable,const Json& inspection) noexcept {
    try {
        if(!Matches(executable,inspection))return false;
        auto path=CachePath(executable);NoLinks(path);
        if(std::filesystem::exists(path)){try{auto previous=Json::parse(Read(path));if(previous.value("schemaVersion",0)>1)return false;}catch(const Json::parse_error&){/* A corrupt advisory cache can be replaced by a fresh inspection. */}}
        Json document={{"schemaVersion",1},{"executable",executable},{"checkedUtc",Now()},
                       {"inputs",Inputs(executable,inspection)},{"inspection",inspection}};
        auto bytes=document.dump();if(bytes.size()>MaximumProfileBytes)return false;
        SaveUserFile(path,bytes);
        const auto& state=inspection["state"];
        if(RecordedInstall(state))return RememberKnownInstallation(executable);
        return true;
    }catch(...){return false;}
}
void RemoveGameProfile(const std::string& executable) noexcept {
    try{auto path=CachePath(executable);NoLinks(path);if(std::filesystem::exists(path)){try{if(Json::parse(Read(path)).value("schemaVersion",0)>1)return;}catch(...){}}DeleteFileW(path.c_str());}catch(...){}
}
namespace {
constexpr size_t MaximumKnownBytes=4*1024*1024,MaximumEnumerationBytes=64*1024*1024;
std::filesystem::path KnownPath(){return UserRoot()/L"known-installations.json";}
Json KnownDocument(){
 auto path=KnownPath();NoLinks(path);if(!std::filesystem::exists(path))return {{"schemaVersion",1},{"targets",Json::array()}};
 auto bytes=Read(path);if(bytes.size()>MaximumKnownBytes)throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.known_install_registry_exceeds_its_safe_size_lim_9690dbea", "Known-install registry exceeds its safe size limit"));
 auto value=Json::parse(bytes);if(value.value("schemaVersion",0)!=1||!value.at("targets").is_array()||value["targets"].size()>BulkUninstall::MaximumTargets)throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.known_install_registry_has_an_unsupported_struct_c6b5f4ff", "Known-install registry has an unsupported structure"));return value;
}
BulkUninstallTarget ReadKnown(const Json& record,const std::string& source){
 auto executable=record.at("executable").get<std::string>();if(executable.empty()||executable.size()>32768||executable.find('\0')!=std::string::npos)throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.known_install_executable_is_invalid_a0d15adf", "Known-install executable is invalid"));
 auto key=Key(executable);if(key.find(L':',2)!=std::wstring::npos||std::filesystem::path(key).filename().empty())throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.known_install_executable_is_not_an_ordinary_loca_0836d902", "Known-install executable is not an ordinary local path"));
 auto title=record.value(Neurotic::UiLiteral("desktop.anythingview.title_07bed14a", "title"),std::string{});if(title.size()>4096)throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.known_install_title_is_too_long_125a715c", "Known-install title is too long"));return {executable,title,source};
}
}
bool RememberKnownInstallation(const std::string& executable,const std::string& title) noexcept {
 try{
  const auto candidate=ReadKnown(Json{{"executable",executable},{Neurotic::UiLiteral("desktop.anythingview.title_07bed14a", "title"),title}},Neurotic::UiLiteral("desktop.profilecache.known_install_d08b4249", "Known install"));auto document=KnownDocument();bool found=false;
  for(auto& item:document["targets"]){auto old=ReadKnown(item,Neurotic::UiLiteral("desktop.profilecache.known_install_d08b4249", "Known install"));if(Key(old.executable)==Key(executable)){if(!title.empty())item[Neurotic::UiLiteral("desktop.anythingview.title_07bed14a", "title")]=title;found=true;}}
  if(!found){if(document["targets"].size()>=BulkUninstall::MaximumTargets)return false;document["targets"].push_back({{"executable",candidate.executable},{Neurotic::UiLiteral("desktop.anythingview.title_07bed14a", "title"),candidate.title}});}
  auto bytes=document.dump();if(bytes.size()>MaximumKnownBytes)return false;SaveUserFile(KnownPath(),bytes);return true;
 }catch(...){return false;}
}
KnownInstallationInventory EnumerateKnownInstallations() noexcept {
 KnownInstallationInventory result;size_t bytesRead=0;
 try{auto document=KnownDocument();for(const auto& item:document["targets"]){try{result.targets.push_back(ReadKnown(item,Neurotic::UiLiteral("desktop.profilecache.known_install_d08b4249", "Known install")));}catch(const std::exception& e){result.issues.push_back(std::string(Neurotic::UiLiteral("desktop.profilecache.known_install_entry_4848614a", "Known-install entry: "))+e.what());}}}
 catch(const std::exception& e){result.issues.push_back(e.what());}
 try{
  const auto directory=UserRoot()/L"profiles";NoLinks(directory);if(std::filesystem::exists(directory)){
  size_t visited=0;
  for(const auto& item:std::filesystem::directory_iterator(directory)){
   if(++visited>BulkUninstall::MaximumTargets){result.issues.push_back(Neurotic::UiLiteral("desktop.profilecache.saved_profile_entry_limit_reached_remaining_entr_1f85f216", "Saved-profile entry limit reached; remaining entries were not inspected."));break;}
   if(item.path().extension()!=L".json")continue;
   try{
    auto stamp=Stamp(item.path());if(stamp.is_null())continue;const auto length=stamp.at("bytes").get<uint64_t>();
    if(length>MaximumEnumerationBytes-bytesRead){result.issues.push_back(Neurotic::UiLiteral("desktop.profilecache.saved_profile_metadata_read_limit_reached_remain_1b05b1e0", "Saved-profile metadata read limit reached; remaining entries were not inspected."));break;}
    bytesRead+=size_t(length);auto document=Json::parse(Read(item.path()));
    if(document.value("schemaVersion",0)!=1)throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.unsupported_saved_profile_schema_85a68ca1", "Unsupported saved-profile schema"));
    auto candidate=ReadKnown(document,Neurotic::UiLiteral("desktop.profilecache.saved_profile_1fe4e82b", "Saved profile"));if(!Matches(candidate.executable,document.at("inspection"))||CachePath(candidate.executable).filename()!=item.path().filename())throw std::runtime_error(Neurotic::UiMessage("desktop.profilecache.saved_profile_target_identity_does_not_match_its_0beb44c3", "Saved-profile target identity does not match its file"));
    const auto& state=document["inspection"]["state"];
    if(RecordedInstall(state))result.targets.push_back(std::move(candidate));
   }catch(const std::exception& e){result.issues.push_back(Utf8(item.path().filename().wstring())+": "+e.what());}
  }
  }
 }catch(const std::exception& e){result.issues.push_back(std::string(Neurotic::UiLiteral("desktop.profilecache.saved_profile_inventory_43068eb5", "Saved-profile inventory: "))+e.what());}
 // Older Apps could evict a profile after a mutation. Retained operation
 // receipts are also advisory knowledge; inspect their target afresh later.
 try{
  const auto directory=UserRoot()/L"requests";NoLinks(directory);if(std::filesystem::exists(directory)){
   size_t visited=0;
   for(const auto& item:std::filesystem::directory_iterator(directory)){
    if(++visited>BulkUninstall::MaximumTargets){result.issues.push_back(Neurotic::UiLiteral("desktop.profilecache.operation_record_entry_limit_reached_remaining_e_d32dcf88", "Operation-record entry limit reached; remaining entries were not inspected."));break;}
    if(!item.path().filename().wstring().ends_with(L".result.json"))continue;
    try{
     auto stamp=Stamp(item.path());if(stamp.is_null())continue;const auto length=stamp.at("bytes").get<uint64_t>();
     if(length>MaximumEnumerationBytes-bytesRead){result.issues.push_back(Neurotic::UiLiteral("desktop.profilecache.known_install_metadata_read_limit_reached_remain_542494bc", "Known-install metadata read limit reached; remaining operation records were not inspected."));break;}
     bytesRead+=size_t(length);auto document=Json::parse(Read(item.path()));
     if(!document.contains("inspection"))continue;const auto& inspection=document.at("inspection");
     const auto& state=inspection.at("state");if(!RecordedInstall(state))continue;
     result.targets.push_back(ReadKnown(inspection.at("target"),Neurotic::UiLiteral("desktop.profilecache.operation_record_3c457195", "Operation record")));
    }catch(const std::exception& e){result.issues.push_back(Utf8(item.path().filename().wstring())+": "+e.what());}
   }
  }
 }catch(const std::exception& e){result.issues.push_back(std::string(Neurotic::UiLiteral("desktop.profilecache.operation_record_inventory_deb98f85", "Operation-record inventory: "))+e.what());}
 return result;
}
}
