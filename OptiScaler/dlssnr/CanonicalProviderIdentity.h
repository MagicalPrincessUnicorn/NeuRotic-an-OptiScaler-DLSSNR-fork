#pragma once
#include "CanonicalProviderPolicy.h"
#include <windows.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <utility>
#include <sstream>
#include <iomanip>
#pragma comment(lib, "advapi32.lib")

// Package identity only. This owns file locks, never features, GPU resources,
// history, completion or a provider lifetime contract.
namespace DlssNr::Canonical {
namespace fs=std::filesystem;
inline std::string Utf8(const std::wstring& text) {
    if(text.empty())return {};
    int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(!count)throw std::runtime_error("PATH_ENCODING");
    std::string out(static_cast<size_t>(count),'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),count,nullptr,nullptr))throw std::runtime_error("PATH_ENCODING");
    return out;
}
inline std::string Quote(const std::string& text) {
    std::string out="\"";
    constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:text) {
        if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
        else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
inline bool SamePath(const fs::path& a,const fs::path& b) {
    return _wcsicmp(a.lexically_normal().c_str(),b.lexically_normal().c_str())==0;
}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE v=INVALID_HANDLE_VALUE):value(v){}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other)noexcept:value(std::exchange(other.value,INVALID_HANDLE_VALUE)){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
inline std::string Hash(HANDLE file) {
    struct Crypto {
        HCRYPTPROV provider=0;HCRYPTHASH hash=0;
        ~Crypto(){if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);}
    } crypto;
    if(!CryptAcquireContextW(&crypto.provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)||
       !CryptCreateHash(crypto.provider,CALG_SHA_256,0,0,&crypto.hash))throw std::runtime_error("HASH_INIT");
    LARGE_INTEGER zero{};
    if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))throw std::runtime_error("HASH_SEEK");
    std::vector<BYTE> block(1024*1024);
    for(;;) {
        DWORD bytes=0;
        if(!ReadFile(file,block.data(),static_cast<DWORD>(block.size()),&bytes,nullptr))throw std::runtime_error("HASH_READ");
        if(!bytes)break;
        if(!CryptHashData(crypto.hash,block.data(),bytes,0))throw std::runtime_error("HASH_UPDATE");
    }
    BYTE digest[32]{};DWORD size=sizeof(digest);
    if(!CryptGetHashParam(crypto.hash,HP_HASHVAL,digest,&size,0)||size!=sizeof(digest))throw std::runtime_error("HASH_FINAL");
    std::ostringstream out;out<<std::hex<<std::setfill('0');
    for(BYTE b:digest)out<<std::setw(2)<<static_cast<unsigned>(b);
    return out.str();
}
struct LockedFile {
    Handle handle;fs::path path;std::string hash;std::uint64_t bytes=0;
    explicit LockedFile(const fs::path& requested):
        handle(CreateFileW(requested.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)) {
        if(handle.value==INVALID_HANDLE_VALUE) {
            const auto error=GetLastError();
            throw std::runtime_error("FILE_LOCK:"+Utf8(requested.wstring())+":win32="+std::to_string(error));
        }
        std::wstring name(32768,L'\0');
        DWORD n=GetFinalPathNameByHandleW(handle.value,name.data(),static_cast<DWORD>(name.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!n||n>=name.size())throw std::runtime_error("FILE_PATH");
        name.resize(n);
        if(name.rfind(L"\\\\?\\UNC\\",0)==0)name=L"\\\\"+name.substr(8);
        else if(name.rfind(L"\\\\?\\",0)==0)name=name.substr(4);
        path=fs::path(name).lexically_normal();
        if(!SamePath(path,requested))throw std::runtime_error("REDIRECTED_FILE");
        LARGE_INTEGER length{};
        if(GetFileType(handle.value)!=FILE_TYPE_DISK||!GetFileSizeEx(handle.value,&length)||length.QuadPart<0)throw std::runtime_error("FILE_SIZE");
        bytes=static_cast<std::uint64_t>(length.QuadPart);hash=Hash(handle.value);
    }
};
enum class VerificationScope { DirectNr, MatchedReference };
struct LoadedModule {
    std::wstring name;fs::path path;std::string hash;bool matches=false;
    bool requiredForRoute=false;std::string observationError;
};
class Package {
    std::vector<LockedFile> files_;
    std::vector<LoadedModule> loaded_;
    fs::path root_,provider_;
    std::string error_;
    std::shared_ptr<void> observedProviderHold_;
    bool prepared_=false,observed_=false,providerObserved_=false,failed_=false;
    HMODULE observedProviderModule_=nullptr; // observation only; the forwarder retains the loader reference
    const VerificationScope scope_;
    unsigned long long snapshotTick_=0;
    void Fail(const char* error)noexcept{try{if(!failed_)error_=error;}catch(...) {}failed_=true;}
    bool ObservationUnavailable(const char* stage,DWORD error) {
        // An unavailable OS snapshot refuses this observation, not the pinned
        // identity for the rest of the process. Never publish partial admission.
        observed_=false;providerObserved_=false;observedProviderModule_=nullptr;loaded_.clear();
        error_=std::string(stage)+":win32="+std::to_string(error);
        return false;
    }
    static fs::path ModulePath(HMODULE module) {
        std::wstring value(32768,L'\0');
        DWORD n=GetModuleFileNameW(module,value.data(),static_cast<DWORD>(value.size()));
        if(!n||n>=value.size())throw std::runtime_error("LOADED_MODULE_PATH");
        value.resize(n);return fs::absolute(value).lexically_normal();
    }
  public:
    explicit Package(VerificationScope scope=VerificationScope::DirectNr):scope_(scope){}
    Package(const Package&)=delete;Package& operator=(const Package&)=delete;
    bool Open(const fs::path& provider)noexcept {
        try {
            if(failed_)return false;
            if(!provider.is_absolute()||_wcsicmp(provider.filename().c_str(),L"nvngx_dlssnr.dll")!=0)throw std::runtime_error("EXPLICIT_CANONICAL_PROVIDER_PATH_REQUIRED");
            if(prepared_) {
                if(!SamePath(provider,provider_))throw std::runtime_error("PACKAGE_SWITCH_REFUSED");
                return Observe();
            }
            provider_=provider.lexically_normal();root_=provider_.parent_path();
            files_.reserve(1+sizeof(Members)/sizeof(Members[0]));
            auto lock=[&](const Member& member) {
                files_.emplace_back(root_/member.name);
                const auto& file=files_.back();
                if(file.bytes!=member.bytes||file.hash!=member.sha256)throw std::runtime_error("PACKAGE_IDENTITY:"+Utf8(member.name));
            };
            if(scope_==VerificationScope::MatchedReference) {
                lock(Archive);
                for(const auto& member:Members)lock(member);
            } else {
                // A game's direct NR route selects the installed provider by
                // path. Retain and record its actual bytes; the reference
                // package's hash is not a compatibility requirement.
                files_.emplace_back(provider_);
            }
            prepared_=true;
            return Observe();
        } catch(const std::exception& error){Fail(error.what());return false;}
          catch(...){Fail("PACKAGE_EXCEPTION");return false;}
    }
    bool Observe(HMODULE requiredProvider=nullptr,bool requireSelectedPath=false)noexcept {
        try {
            if(failed_||!prepared_)return false;
            observed_=false;providerObserved_=false;observedProviderModule_=nullptr;loaded_.clear();error_.clear();snapshotTick_=GetTickCount64();
            Handle snapshot;
            DWORD snapshotError=ERROR_SUCCESS;
            // Windows documents BAD_LENGTH during loader-list changes. Bound
            // immediate retries on the render thread; later probes start fresh.
            for(unsigned attempt=0;attempt<4;++attempt) {
                snapshot.value=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
                if(snapshot.value!=INVALID_HANDLE_VALUE)break;
                snapshotError=GetLastError();
                if(snapshotError!=ERROR_BAD_LENGTH)break;
            }
            if(snapshot.value==INVALID_HANDLE_VALUE)return ObservationUnavailable("MODULE_SNAPSHOT",snapshotError);
            MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
            if(!Module32FirstW(snapshot.value,&entry))return ObservationUnavailable("MODULE_ENUMERATION",GetLastError());
            bool requiredFound=requiredProvider==nullptr;
            do {
                if(scope_==VerificationScope::DirectNr&&_wcsicmp(entry.szModule,L"nvngx_dlssnr.dll")!=0) {
                    // Toolhelp is a snapshot: an unrelated module may unload
                    // before a live path lookup. It has no authority over the
                    // direct provider, and must not poison its session. Keep
                    // optional diagnostics from the snapshot without opening
                    // or hashing unrelated DLLs on every evaluation.
                    const std::wstring name=entry.szModule;
                    bool listed=false;
                    for(const auto& member:Members)if(_wcsicmp(name.c_str(),member.name)==0){listed=true;break;}
                    if(listed||(name.size()>=7&&_wcsnicmp(name.c_str(),L"sl.",3)==0&&
                                _wcsicmp(fs::path(name).extension().c_str(),L".dll")==0))
                        loaded_.push_back({name,fs::path(entry.szExePath),{},false,false,
                                           "SNAPSHOT_ONLY_NOT_REQUIRED_FOR_DIRECT_NR"});
                    continue;
                }
                HMODULE held=nullptr;
                if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    reinterpret_cast<LPCWSTR>(entry.hModule),&held)||held!=entry.hModule) {
                    if(held)FreeLibrary(held);
                    throw std::runtime_error("MODULE_OBSERVATION_UNLOADED");
                }
                auto hold=std::shared_ptr<void>(held,[](void* value){FreeLibrary(static_cast<HMODULE>(value));});
                const fs::path actual=ModulePath(held);
                if(requireSelectedPath && _wcsicmp(actual.filename().c_str(),L"nvngx_dlssnr.dll")==0 && !SamePath(actual,provider_))
                    throw std::runtime_error("COMPETING_PROVIDER_PATH");
                bool known=false;
                for(const auto& member:Members) {
                    if(_wcsicmp(actual.filename().c_str(),member.name)!=0)continue;
                    known=true;
                    const bool required=scope_==VerificationScope::MatchedReference||
                        _wcsicmp(member.name,L"nvngx_dlssnr.dll")==0;
                    LoadedModule record{member.name,actual,{},false,required,{}};
                    const LockedFile* pinned=nullptr;
                    for(const auto& file:files_)if(SamePath(actual,file.path)){pinned=&file;break;}
                    if(pinned){record.hash=pinned->hash;record.matches=
                        scope_==VerificationScope::DirectNr || record.hash==member.sha256;}
                    else {
                        try {
                            LockedFile unexpected(actual);record.hash=unexpected.hash;
                            record.matches=scope_==VerificationScope::MatchedReference&&
                                record.hash==member.sha256&&SamePath(actual,root_/member.name);
                            if(scope_==VerificationScope::DirectNr&&required) {
                                // Game loaders may relocate an identical DLL (for
                                // example into _storage_). Compare against the
                                // selected file's actual bytes, never a build allowlist.
                                for(const auto& selected:files_)if(SamePath(selected.path,provider_)) {
                                    record.matches=unexpected.bytes==selected.bytes&&unexpected.hash==selected.hash;
                                    break;
                                }
                                if(record.matches)files_.push_back(std::move(unexpected));
                            }
                        } catch(const std::exception& error) {
                            record.observationError=error.what();
                            if(required){loaded_.push_back(record);throw;}
                        }
                    }
                    loaded_.push_back(record);
                    if(required&&!record.matches)throw std::runtime_error("MIXED_MODULE:"+Utf8(member.name));
                    const bool selectedProvider=record.matches&&_wcsicmp(member.name,L"nvngx_dlssnr.dll")==0;
                    if(selectedProvider) {
                        providerObserved_=true;
                        if(!observedProviderModule_||SamePath(actual,provider_)){observedProviderModule_=entry.hModule;observedProviderHold_=hold;}
                        if(entry.hModule==requiredProvider)requiredFound=true;
                    }
                }
                const auto name=actual.filename().wstring();
                if(!known&&name.size()>=7&&_wcsnicmp(name.c_str(),L"sl.",3)==0&&_wcsicmp(actual.extension().c_str(),L".dll")==0) {
                    const bool required=scope_==VerificationScope::MatchedReference;
                    LoadedModule record{name,actual,{},false,required,{}};
                    try {LockedFile unexpected(actual);record.hash=unexpected.hash;}
                    catch(const std::exception& error) {
                        record.observationError=error.what();
                        if(required){loaded_.push_back(record);throw;}
                    }
                    loaded_.push_back(record);
                    if(required)throw std::runtime_error("MIXED_MODULE_UNLISTED:"+Utf8(name));
                }
            } while(Module32NextW(snapshot.value,&entry));
            const auto enumerationError=GetLastError();
            if(enumerationError!=ERROR_NO_MORE_FILES)return ObservationUnavailable("MODULE_ENUMERATION_INCOMPLETE",enumerationError);
            if(!requiredFound)throw std::runtime_error("ACTUAL_PROVIDER_MODULE_MISSING");
            observed_=true;return true;
        } catch(const std::exception& error){Fail(error.what());return false;}
          catch(...){Fail("MODULE_OBSERVATION_EXCEPTION");return false;}
    }
    const std::string& Error()const noexcept{return error_;}
    bool ProviderObserved()const noexcept{return !failed_&&observed_&&providerObserved_;}
    HMODULE ObservedProviderModule()const noexcept {
        return !failed_&&observed_?observedProviderModule_:nullptr;
    }
    bool MatchesReviewedProvider()const noexcept {
        if(failed_||!observed_||!providerObserved_)return false;
        for(const auto& file:files_)if(SamePath(file.path,provider_))
            return file.bytes==Members[4].bytes&&file.hash==Members[4].sha256;
        return false;
    }
    std::string Json()const {
        std::ostringstream out;
        out<<"{\"schema_version\":2,\"kind\":\"CANONICAL_PROVIDER_IDENTITY\",\"route\":"
           <<Quote(scope_==VerificationScope::DirectNr?"DIRECT_FEATURE18_D3D12":"MATCHED_REFERENCE_INSPECTION")
           <<",\"verification_scope\":"<<Quote(scope_==VerificationScope::DirectNr?"DIRECT_NR":"MATCHED_REFERENCE")
           <<",\"pid\":"<<GetCurrentProcessId()
           <<",\"snapshot_tick_ms\":"<<snapshotTick_<<",\"archive_expected_sha256\":"
           <<(scope_==VerificationScope::MatchedReference?Quote(Archive.sha256):"null")
           <<",\"archive_path\":"<<(scope_==VerificationScope::MatchedReference?Quote(Utf8((root_/Archive.name).wstring())):"null")
           <<",\"selection_verified\":"<<(prepared_?"true":"false")
           <<",\"package_verified\":"<<(prepared_&&scope_==VerificationScope::MatchedReference?"true":"false")
           <<",\"module_snapshot_complete\":"<<(observed_?"true":"false")
           <<",\"provider_loaded\":"<<(ProviderObserved()?"true":"false")<<",\"refused\":"<<((failed_||!error_.empty())?"true":"false")<<",\"error\":"<<Quote(error_)
           <<",\"observation_retryable\":"<<(!failed_&&!error_.empty()?"true":"false")
           <<",\"hash_scope\":\"locked on-disk files at observed module paths; not executable-memory attestation\""
           <<",\"observation_scope\":\"admission snapshot; does not control future foreign DLL loads\",\"members\":[";
        bool first=true;
        for(const auto& file:files_) {
            if(!first)out<<',';first=false;
            out<<"{\"path\":"<<Quote(Utf8(file.path.wstring()))<<",\"bytes\":"<<file.bytes<<",\"sha256\":"<<Quote(file.hash)<<'}';
        }
        out<<"],\"loaded_modules\":[";first=true;
        for(const auto& module:loaded_) {
            if(!first)out<<',';first=false;
            out<<"{\"name\":"<<Quote(Utf8(module.name))<<",\"path\":"<<Quote(Utf8(module.path.wstring()))
               <<",\"sha256\":"<<Quote(module.hash)<<",\"matches\":"<<(module.matches?"true":"false")
               <<",\"required_for_route\":"<<(module.requiredForRoute?"true":"false")
               <<",\"observation_error\":"<<Quote(module.observationError)<<'}';
        }
        out<<"],\"dependency_completeness_established\":false,\"lifetime_contract_granted\":false,\"qualification\":false}";return out.str();
    }
};
}
