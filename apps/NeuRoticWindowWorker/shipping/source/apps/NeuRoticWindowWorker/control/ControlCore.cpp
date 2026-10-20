#include "ControlCore.h"
#include "ImportDirectoryLeases.h"
#include "../../common/ModelFolderPolicy.h"
#include <bcrypt.h>
#include <shlobj.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <set>
#include <stdexcept>

namespace nrw {
namespace {
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h){}
    void Reset(){if(value!=INVALID_HANDLE_VALUE && value)CloseHandle(value);value=INVALID_HANDLE_VALUE;}
    ~Handle(){Reset();}
    Handle(const Handle&)=delete;
    Handle(Handle&& x) noexcept:value(x.value){x.value=INVALID_HANDLE_VALUE;}
    Handle& operator=(Handle&& x) noexcept {if(this!=&x){Reset();value=x.value;x.value=INVALID_HANDLE_VALUE;}return *this;}
    explicit operator bool() const{return value!=INVALID_HANDLE_VALUE && value;}
};
std::string Error(const char* action) {return std::string(action)+" (Windows "+std::to_string(GetLastError())+")";}
bool PlainAncestors(const std::filesystem::path& path,std::string& reason) {
    for(auto part=path;!part.empty();part=part.parent_path()) {
        DWORD attrs=GetFileAttributesW(part.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES) {
            auto error=GetLastError();
            if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND) {reason=Error("Cannot inspect model path");return false;}
        } else if(attrs&FILE_ATTRIBUTE_REPARSE_POINT) {reason="Model path contains a junction or symbolic link";return false;}
        if(part==part.parent_path())break;
    } return true;
}
std::filesystem::path Absolute(const std::wstring& path) {
    if(path.empty() || path.size()>32000 || path.find(L'\0')!=std::wstring::npos)throw std::runtime_error("Model path is empty or too long");
    auto p=std::filesystem::absolute(std::filesystem::path(path)).lexically_normal();
    auto s=p.wstring();
    if(s.rfind(L"\\\\",0)==0 || s.find(L':',2)!=std::wstring::npos)throw std::runtime_error("Models require a local ordinary file path");
    return p;
}
std::string Digest(HANDLE file,std::string& reason) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    auto cleanup=[&]{if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0){reason="SHA256 provider unavailable";return {};}
    DWORD objectBytes=0,actual=0;
    if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectBytes),sizeof(objectBytes),&actual,0)<0){cleanup();reason="SHA256 setup failed";return {};}
    std::vector<unsigned char> object(objectBytes),buffer(1024*1024);
    if(BCryptCreateHash(algorithm,&hash,object.data(),objectBytes,nullptr,0,0)<0){cleanup();reason="SHA256 creation failed";return {};}
    LARGE_INTEGER zero{};
    if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN)){cleanup();reason=Error("Cannot seek model");return {};}
    DWORD bytes=0;
    for(;;) {
        if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&bytes,nullptr)){cleanup();reason=Error("Cannot read model");return {};}
        if(!bytes)break;
        if(BCryptHashData(hash,buffer.data(),bytes,0)<0){cleanup();reason="SHA256 update failed";return {};}
    }
    std::array<unsigned char,32> digest{};
    if(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0){cleanup();reason="SHA256 finalization failed";return {};}
    cleanup();std::ostringstream output;output<<std::hex<<std::setfill('0');
    for(auto b:digest)output<<std::setw(2)<<unsigned(b);
    return output.str();
}
Handle OpenRead(const std::filesystem::path& path,std::string& reason) {
    if(!PlainAncestors(path,reason))return Handle{};
    Handle h(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    if(!h){reason=Error("Cannot open model");return h;}
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(h.value,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){reason="Model is not an ordinary local file";return Handle{};}
    return h;
}
ModelInfo InspectModel(const std::filesystem::path& path,HANDLE h,bool source=false) {
    ModelInfo result;result.path=path.wstring();BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(h,&info)){result.reason=Error("Cannot identify model");return result;}
    result.bytes=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
    result.fileId=(uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
    result.modified=(uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime;
    if(source) {
        if(_wcsicmp(path.extension().c_str(),L".dll")!=0){result.reason="Select a DLL file";return result;}
    } else if(_wcsicmp(path.filename().c_str(),neurotic::model::FileName)!=0){result.reason="Select nvngx_dlssnr.dll";return result;}
    // Source filename and language do not decide compatibility. Digest equality
    // binds chosen/copied bytes; runtime lookup keeps its canonical name.
    result.sha256=Digest(h,result.reason);
    if(result.sha256.empty())return result;
    result.valid=true;result.reason="NR model file detected";return result;
}
constexpr const wchar_t* ModelOwnerName=L".neurotic-model-owner.json";
constexpr const wchar_t* ModelRecoveryName=L".neurotic-model-recovery.json";
Json ImportFileStamp(HANDLE handle,const ModelInfo& model) {
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(handle,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
        throw std::runtime_error("Managed model identity unavailable");
    return {{"volume",info.dwVolumeSerialNumber},{"file",(uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow},
        {"bytes",model.bytes},{"sha256",model.sha256}};
}
Json ImportDirectoryStamp(const detail::ImportDirectoryLeases& directories) {
    BY_HANDLE_FILE_INFORMATION info{};
    if(!directories.Identity(info))throw std::runtime_error("Managed directory identity unavailable");
    return {{"volume",info.dwVolumeSerialNumber},{"file",(uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow}};
}
Handle OpenImportTarget(const std::filesystem::path& path,std::string& reason) {
    if(!PlainAncestors(path,reason))return Handle{};
    // Write access rejects active executable-image sections even without a
    // surviving image file handle. Share zero excludes competing read/write/delete.
    Handle file(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    if(!file){reason=Error("Cannot exclusively update model");return file;}
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file.value,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) {
        reason="Update target is not an ordinary local file";return Handle{};
    }
    return file;
}
bool ExplicitUpdateAuthorized(const ExplicitModelUpdate& update,const std::filesystem::path& destination,const detail::ImportDirectoryLeases& directories) {
    if(update.persistedModelPath.empty() || update.managedModelsRoot.empty())return false;
    auto configured=Absolute(update.persistedModelPath),managed=Absolute(update.managedModelsRoot);
    std::string reason;
    if(!PlainAncestors(configured,reason) || !PlainAncestors(managed,reason) ||
        _wcsicmp(configured.filename().c_str(),neurotic::model::FileName)!=0)return false;
    auto equal=[](std::filesystem::path a,std::filesystem::path b) {
        while(!a.has_filename() && a!=a.root_path())a=a.parent_path();
        while(!b.has_filename() && b!=b.root_path())b=b.parent_path();
        const auto x=a.wstring(),y=b.wstring();
        return CompareStringOrdinal(x.c_str(),int(x.size()),y.c_str(),int(y.size()),TRUE)==CSTR_EQUAL;
    };
    return equal(configured,destination) && equal(managed,directories.Root()) && equal(configured.parent_path(),directories.Root());
}
struct ImportTemporary {
    Handle file;
    std::filesystem::path path;
};
ImportTemporary CreateImportTemporary(const std::filesystem::path& folder) {
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot create import transaction identity");
    wchar_t id[40]{};StringFromGUID2(guid,id,40);
    ImportTemporary output;output.path=folder/(std::wstring(L"import-")+id+L".tmp");
    output.file.value=CreateFileW(output.path.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(!output.file)throw std::runtime_error(Error("Cannot create model import"));
    return output;
}
void CopyImportBytes(HANDLE source,HANDLE output) {
    LARGE_INTEGER zero{};
    if(!SetFilePointerEx(source,zero,nullptr,FILE_BEGIN))throw std::runtime_error(Error("Cannot read import source"));
    std::vector<unsigned char> buffer(1024*1024);DWORD read=0,written=0;
    for(;;) {
        if(!ReadFile(source,buffer.data(),DWORD(buffer.size()),&read,nullptr))throw std::runtime_error(Error("Cannot read import source"));
        if(!read)break;
        if(!WriteFile(output,buffer.data(),read,&written,nullptr)||written!=read)throw std::runtime_error(Error("Cannot write model import"));
    }
    if(!FlushFileBuffers(output))throw std::runtime_error(Error("Cannot flush model import"));
}
void WriteImportOwnership(HANDLE output,const Json& receipt) {
    const auto bytes=receipt.dump();DWORD written=0;
    if(bytes.size()>4096 || !WriteFile(output,bytes.data(),DWORD(bytes.size()),&written,nullptr)||written!=bytes.size()||!FlushFileBuffers(output))
        throw std::runtime_error(Error("Cannot stage managed model ownership"));
}

uint64_t UInt(const Json& j,const char* key,bool nonzero=true) {
    if(!j.contains(key) || !j.at(key).is_number_unsigned())throw std::runtime_error(std::string(key)+" must be an unsigned integer");
    auto value=j.at(key).get<uint64_t>();if(nonzero && !value)throw std::runtime_error(std::string(key)+" must be greater than zero");return value;
}
}
std::string Utf8(const std::wstring& s) {
    if(s.empty())return {};auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if(!n)throw std::runtime_error("Invalid Unicode text");std::string output(n,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),output.data(),n,nullptr,nullptr);return output;
}
std::wstring Wide(const std::string& s) {
    if(s.empty())return {};auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n)throw std::runtime_error("Invalid UTF-8 text");std::wstring output(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),output.data(),n);return output;
}
std::string HashFile(const std::wstring& path,std::string& reason) {
    reason.clear();try{auto p=Absolute(path);auto file=OpenRead(p,reason);return file?Digest(file.value,reason):std::string{};}
    catch(const std::exception& e){reason=e.what();return {};}
}
ModelInfo VerifyModel(const std::wstring& path,bool source) {
    ModelInfo result;try {
        auto p=Absolute(path);result.path=p.wstring();
        if(!source && std::filesystem::exists(p.parent_path()/ModelRecoveryName)) {
            result.reason="Resolve the pending model update recovery record before starting NR: "+Utf8((p.parent_path()/ModelRecoveryName).wstring());return result;
        }
        auto file=OpenRead(p,result.reason);if(file)result=InspectModel(p,file.value,source);
    } catch(const std::exception& e){result.reason=e.what();}return result;
}
std::wstring DefaultModelsRoot() {
    PWSTR path=nullptr;auto hr=SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&path);
    if(FAILED(hr))throw std::runtime_error("Local application data directory unavailable");
    std::filesystem::path base(path);CoTaskMemFree(path);return neurotic::model::Folder(base/L"NeuRotic/Hub/Runtime").wstring();
}
#ifdef NRW_IMPORT_TEST_HOOKS
namespace detail {
static void (*importCheckpointHook)(ImportCheckpoint)=nullptr;
void SetImportCheckpointHook(void (*hook)(ImportCheckpoint)){importCheckpointHook=hook;}
static void ImportCheck(ImportCheckpoint point){if(importCheckpointHook)importCheckpointHook(point);}
}
#endif
ModelInfo ImportModel(const std::wstring& source,const std::wstring& modelsRoot,const ExplicitModelUpdate& update) {
    ModelInfo result;detail::ImportDirectoryLeases directories;Handle transaction,previous,recovery;
    ImportTemporary output,record;
    std::filesystem::path destination,oldLocation,recoveryPath;
    bool oldMoved=false,newPublished=false,recordPublished=false;
    auto checkpoint=[](detail::ImportCheckpoint point) {
#ifdef NRW_IMPORT_TEST_HOOKS
        detail::ImportCheck(point);
#else
        (void)point;
#endif
    };
    try {
        auto sourcePath=Absolute(source);auto input=OpenRead(sourcePath,result.reason);if(!input)return result;
        result=InspectModel(sourcePath,input.value,true);if(!result.valid)return result;
        const auto selected=result;std::string reason;
        if(!directories.Open(Absolute(modelsRoot),reason))throw std::runtime_error(reason);
        const auto& folder=directories.Root();
        transaction=Handle(CreateFileW((folder/L".neurotic-model-import.lock").c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,
            OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_DELETE_ON_CLOSE,nullptr));
        BY_HANDLE_FILE_INFORMATION lockInfo{};
        if(!transaction || !GetFileInformationByHandle(transaction.value,&lockInfo) ||
            (lockInfo.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
            throw std::runtime_error("Another model import is active, or the import lock is unavailable");
        recoveryPath=folder/ModelRecoveryName;
        // Never replace an unresolved recovery record or trust it to delete files.
        // Even a same-byte request stops here, before creating any new model copy.
        if(std::filesystem::exists(recoveryPath))
            throw std::runtime_error("Resolve the previous model update recovery record before retrying: "+Utf8(recoveryPath.wstring()));
        neurotic::model::EnsureReadme(folder);destination=folder/neurotic::model::FileName;
        const auto directory=ImportDirectoryStamp(directories);
        const bool replacing=std::filesystem::exists(destination);ModelInfo observed;Json expected;
        if(replacing) {
            auto read=OpenRead(destination,reason);if(!read)throw std::runtime_error(reason);
            observed=InspectModel(destination,read.value);if(!observed.valid)throw std::runtime_error(observed.reason);
            if(observed.bytes==selected.bytes && observed.sha256==selected.sha256)return observed;
            // Editable records never grant overwrite authority. This is the
            // current explicit App action on its exact saved managed destination.
            if(!ExplicitUpdateAuthorized(update,destination,directories))
                throw std::runtime_error("Unowned runtime DLL differs from selection; use an explicit App update of its saved managed model path");
            expected=ImportFileStamp(read.value,observed);
        }
        output=CreateImportTemporary(folder);CopyImportBytes(input.value,output.file.value);
        auto copied=InspectModel(destination,output.file.value),sourceNow=InspectModel(sourcePath,input.value,true);
        if(!copied.valid || copied.sha256!=selected.sha256 || copied.bytes!=selected.bytes || !sourceNow.valid ||
            sourceNow.fileId!=selected.fileId || sourceNow.sha256!=selected.sha256 || sourceNow.bytes!=selected.bytes)
            throw std::runtime_error("Imported model identity did not match selected source");
        checkpoint(detail::ImportCheckpoint::Staged);
        if(replacing) {
            checkpoint(detail::ImportCheckpoint::BeforeTargetAcquire);
            previous=OpenImportTarget(destination,reason);
            if(!previous)throw std::runtime_error(reason+"; Close NR sessions and other users of this DLL, then retry");
            const auto locked=InspectModel(destination,previous.value);
            if(!locked.valid || ImportFileStamp(previous.value,locked)!=expected)
                throw std::runtime_error("Runtime DLL changed before the exclusive update lease; no target was replaced");
            oldLocation=folder/(output.path.stem().wstring()+L".previous.dll");
        }
        Json recoveryRecord={{"record","NeuRoticWindowWorker/ModelRecovery/1"},{"directory",directory},
            {"phase","prepared; inspect actual identities before any recovery"},
            {"new",{{"identity",ImportFileStamp(output.file.value,copied)},{"stagingLeaf",Utf8(output.path.filename().wstring())},{"canonicalLeaf","nvngx_dlssnr.dll"}}}};
        if(replacing)recoveryRecord["previous"]={{"identity",expected},{"rollbackLeaf",Utf8(oldLocation.filename().wstring())},{"canonicalLeaf","nvngx_dlssnr.dll"}};
        recovery=Handle(CreateFileW(recoveryPath.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(!recovery)throw std::runtime_error(Error("Cannot create model recovery record"));
        WriteImportOwnership(recovery.value,recoveryRecord);
        if(replacing) {
            checkpoint(detail::ImportCheckpoint::BeforeOldMove);
            if(!directories.Publish(previous.value,oldLocation.filename().wstring(),reason))throw std::runtime_error(reason);
            oldMoved=true;
        }
        // Two object-bound atomic moves, with a short availability gap while NR
        // is closed. No unconditional replacement or close-before-overwrite.
        checkpoint(detail::ImportCheckpoint::BeforeNewPublish);
        if(!directories.Publish(output.file.value,L"nvngx_dlssnr.dll",reason))throw std::runtime_error(reason);
        newPublished=true;checkpoint(detail::ImportCheckpoint::ModelPublished);
        if(!FlushFileBuffers(output.file.value))throw std::runtime_error(Error("Cannot flush published model import"));
        result=InspectModel(destination,output.file.value);
        if(!result.valid || result.sha256!=selected.sha256 || result.bytes!=selected.bytes)
            throw std::runtime_error("Managed model changed after publication");
        // Optional historical action record, written only after model commit.
        // Existing records are kept unchanged and never treated as authority.
        if(!std::filesystem::exists(folder/ModelOwnerName)) {
            record=CreateImportTemporary(folder);
            WriteImportOwnership(record.file.value,{{"record","NeuRoticWindowWorker/ModelAction/1"},{"directory",directory},
                {"authority",replacing?"explicit-configured-update":"new-destination"},{"identity",ImportFileStamp(output.file.value,result)}});
            if(!directories.Publish(record.file.value,ModelOwnerName,reason))throw std::runtime_error(reason);
            recordPublished=true;
        }
        checkpoint(detail::ImportCheckpoint::OwnershipPublished);
        if(oldMoved) {
            bool retired=false;
            try {
                checkpoint(detail::ImportCheckpoint::BeforeRetirePrevious);
                retired=detail::ImportDirectoryLeases::Rollback(previous.value,reason);
            } catch(const std::exception& e){reason=e.what();}
            if(!retired) {
                result.reason+="; Previous DLL retained at "+Utf8(oldLocation.wstring())+"; "+reason+"; recovery record: "+Utf8(recoveryPath.wstring());
                return result;
            }
            previous.Reset();oldMoved=false;
        }
        if(!detail::ImportDirectoryLeases::Rollback(recovery.value,reason))result.reason+="; Recovery record retained: "+reason;
    } catch(const std::exception& e) {
        result.valid=false;result.reason=e.what();std::string reason;bool restored=true;
        if(newPublished) {
            // Move only our exact new object. A subsequently created foreign
            // canonical name is never removed or overwritten by path.
            bool moved=false;
            try {
                checkpoint(detail::ImportCheckpoint::BeforeRollbackRemoveNew);
                moved=directories.Publish(output.file.value,output.path.filename().wstring(),reason);
            } catch(const std::exception& failure){reason=failure.what();}
            if(moved)newPublished=false;
            else {restored=false;result.reason+="; Cannot withdraw exact new DLL: "+reason;}
        }
        if(oldMoved && !newPublished) {
            bool moved=false;
            try {
                checkpoint(detail::ImportCheckpoint::BeforeRestoreOld);
                moved=directories.Publish(previous.value,L"nvngx_dlssnr.dll",reason);
            } catch(const std::exception& failure){reason=failure.what();}
            if(moved)oldMoved=false;
            else {restored=false;result.reason+="; Previous DLL retained at "+Utf8(oldLocation.wstring())+": "+reason;}
        }
        if(output.file && !newPublished) {
            if(!detail::ImportDirectoryLeases::Rollback(output.file.value,reason)){restored=false;result.reason+="; New staging file retained at "+Utf8(output.path.wstring())+": "+reason;}
            output.file.Reset();
        }
        if(record.file && (!recordPublished || restored)) {
            if(!detail::ImportDirectoryLeases::Rollback(record.file.value,reason)){restored=false;result.reason+="; "+reason;}
            record.file.Reset();
        }
        if(recovery) {
            if(restored && !oldMoved && !newPublished) {
                if(!detail::ImportDirectoryLeases::Rollback(recovery.value,reason))result.reason+="; Recovery record retained: "+reason;
            } else result.reason+="; Resolve recovery record before another update: "+Utf8(recoveryPath.wstring());
        }
    }
    return result;
}

Json ModelJson(const ModelInfo& model) {return {{"ready",model.valid},{"path",Utf8(model.path)},{"bytes",model.bytes},{"sha256",model.sha256},{"reason",model.reason},{"cta",model.valid?"":"Please provide your DLSS NR file."}};}
Json WindowJson(const WindowIdentity& w) {return {{"hwnd",w.hwnd},{"pid",w.pid},{"processCreation",w.processCreation},{"title",Utf8(w.title)},{"executable",Utf8(w.executable)},{"windowClass",Utf8(w.windowClass)},{"x",w.x},{"y",w.y},{"width",w.width},{"height",w.height}};}
WindowIdentity ParseWindow(const Json& j) {
    if(!j.is_object())throw std::runtime_error("Window identity must be an object");WindowIdentity w;
    w.hwnd=UInt(j,"hwnd");w.processCreation=UInt(j,"processCreation");auto pid=UInt(j,"pid");
    if(pid>UINT32_MAX)throw std::runtime_error("Window PID out of range");w.pid=static_cast<uint32_t>(pid);
    w.title=Wide(j.value("title",std::string{}));w.executable=Wide(j.value("executable",std::string{}));w.windowClass=Wide(j.value("windowClass",std::string{}));
    w.x=j.value("x",0);w.y=j.value("y",0);w.width=j.value("width",0u);w.height=j.value("height",0u);return w;
}
depth::Settings ParseDepthSettings(const Json& j) {
    if(j.value("depthProvider",std::string("off"))!="off" || j.value("depthPreview",false))
        throw std::runtime_error("Depth estimation is unavailable in NR Anything.");
    return {};
}
Json ParseCommand(const std::string& line) {
    if(line.size()>MaxCommandBytes)throw std::runtime_error("Command exceeds 65536 bytes");
    auto j=Json::parse(line);if(!j.is_object() || !j.contains("command") || !j["command"].is_string())throw std::runtime_error("Command must be an object with a command name");
    auto command=j["command"].get<std::string>();
    const std::set<std::string> known={"catalog","status","verify-model","import-model","set-model","start","stop","cancel","set-comparison","set-processing","snapshot","measure-frame","quit"};
    if(!known.contains(command))throw std::runtime_error("Unknown command");
    if(command=="measure-frame") {
        if(j.size()!=2 || !j.contains("requestId") || !j["requestId"].is_number_unsigned() || UInt(j,"requestId")==0)
            throw std::runtime_error("Measurement requires only a positive requestId; rendering settings are unchanged");
        return j;
    }
    if(command!="catalog" && command!="status" && command!="verify-model" && command!="quit")UInt(j,"revision");
    if(j.contains("revision"))UInt(j,"revision");
    for(const auto* key:{"path","modelsRoot","forwarder","depthBundle","depthRoot"})if(j.contains(key) && !j[key].is_string())throw std::runtime_error(std::string(key)+" must be a string");
    if((command=="verify-model" || command=="set-model" || command=="import-model") && (!j.contains("path") || j["path"].get<std::string>().empty()))throw std::runtime_error("Model path is required");
    if(j.contains("modelUpdateContext")) {
        const auto& update=j["modelUpdateContext"];
        if((command!="set-model" && command!="import-model") || !update.is_object() || update.size()!=2 ||
            !update.contains("persistedModelPath") || !update["persistedModelPath"].is_string() ||
            !update.contains("managedModelsRoot") || !update["managedModelsRoot"].is_string())
            throw std::runtime_error("Invalid explicit model update context");
        for(const auto* key:{"persistedModelPath","managedModelsRoot"})
            if(update[key].get_ref<const std::string&>().empty() || update[key].get_ref<const std::string&>().size()>32000)
                throw std::runtime_error("Model update path is empty or too long");
    }
    if(command=="start") {
        ParseDepthSettings(j);
        auto outputMode=j.value("outputMode",std::string("overlay"));if(outputMode!="overlay"&&outputMode!="preview")throw std::runtime_error("Output mode must be overlay or preview");
        auto mode=j.value("mode",std::string("countdown"));if(mode!="countdown" && mode!="selected")throw std::runtime_error("Selection mode must be countdown or selected");
        if(mode=="selected"){if(!j.contains("window"))throw std::runtime_error("Selected mode requires catalog window identity");ParseWindow(j["window"]);}
        if(j.contains("seconds")){auto seconds=UInt(j,"seconds");if(seconds>30)throw std::runtime_error("Countdown must be 1 to 30 seconds");}
        auto guides=j.value("guides",std::string("off"));if(guides!="off" && guides!="shadow" && guides!="experimental")throw std::runtime_error("Guides mode must be off, shadow or experimental");
        if(j.contains("workWidth")!=j.contains("workHeight"))throw std::runtime_error("Both working dimensions are required");
        if(j.contains("nrScalePercent")) {
            auto percent=UInt(j,"nrScalePercent");
            if(percent<25 || percent>100 || j.contains("workWidth") || j.contains("workHeight"))
                throw std::runtime_error("NR resolution must be 25 to 100 percent without an explicit work size");
        }
        if(j.contains("workWidth")){if(UInt(j,"workWidth")>8192 || UInt(j,"workHeight")>8192)throw std::runtime_error("Working dimensions exceed worker limit");}
    }
    for(const auto* key:{"transferStrength","colourStrength","split"})if(j.contains(key)) {
        if(!j[key].is_number())throw std::runtime_error(std::string(key)+" must be numeric");auto f=j[key].get<double>();
        if(!std::isfinite(f)||f<0||f>(std::string(key)=="split"?1.0:2.0))throw std::runtime_error(std::string(key)+" is outside supported range");
    }
    if(j.contains("stripes") && (!j["stripes"].is_number_integer() || (j["stripes"].get<int>()!=0 && (j["stripes"].get<int>()<2 || j["stripes"].get<int>()>32))))throw std::runtime_error("Stripes must be zero or 2 to 32");
    if(j.contains("modelStyle") && (!j["modelStyle"].is_number_integer() || j["modelStyle"].get<int>()<0 || j["modelStyle"].get<int>()>2))throw std::runtime_error("Model style must be 0 to 2");
    if(j.contains("comparisonDirection")&&(!j["comparisonDirection"].is_number_integer()||j["comparisonDirection"].get<int>()<0||j["comparisonDirection"].get<int>()>7))throw std::runtime_error("Comparison direction must be 0 to 7");
    if(command=="set-processing"){
        const std::set<std::string> fields={"command","revision","nrScalePercent","modelStyle","transferStrength","colourStrength"};
        for(const auto& entry:j.items())if(!fields.contains(entry.key()))throw std::runtime_error("Live processing cannot change source, model, guides or output ownership");
        for(const char* field:{"nrScalePercent","modelStyle","transferStrength","colourStrength"})if(!j.contains(field))throw std::runtime_error("Live processing requires a complete settings snapshot");
        const auto scale=UInt(j,"nrScalePercent");if(scale<25||scale>100)throw std::runtime_error("NR resolution must be 25 to 100 percent");
    }
    if(command=="set-comparison" && !j.contains("split"))throw std::runtime_error("Comparison split is required");
    return j;
}
bool SelectionState::Request(uint64_t revision,std::string& reason) {
    if(!revision || revision<=requested_) {reason="Revision must be newer than requestedRevision";return false;}
    requested_=revision;reason.clear();return true;
}
bool SelectionState::Countdown(uint64_t revision,unsigned seconds,uint64_t nowMs,bool modelReady,std::string& reason) {
    if(!modelReady){reason="Please provide your DLSS NR file.";return false;}
    if(!seconds || seconds>30 || nowMs>UINT64_MAX-uint64_t(seconds)*1000){reason="Countdown must be 1 to 30 seconds";return false;}
    if(!Request(revision,reason))return false;deadline_=nowMs+uint64_t(seconds)*1000;pending_=true;return true;
}
bool SelectionState::Due(uint64_t nowMs) {if(!pending_ || nowMs<deadline_)return false;pending_=false;return true;}
void SelectionState::Cancel(){pending_=false;deadline_=0;}
}
