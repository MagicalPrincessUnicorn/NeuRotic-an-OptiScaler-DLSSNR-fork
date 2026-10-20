#include "NrDllProfiles.h"
#include <windows.h>
#include <objbase.h>
#include <bcrypt.h>
#include <json.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")

namespace nh {
NrDllError::NrDllError(NrDllErrorCode c, unsigned long e)
    : std::runtime_error("nr_dll_profiles"), code(c), systemError(e) {}
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
// UserDataSession supplies the process-wide writer authority. This mutex also
// serializes background tasks across separate store objects in that process.
std::mutex storeMutex;
constexpr wchar_t dllName[] = L"nvngx_dlssnr.dll";
constexpr std::size_t metadataLimit = 256 * 1024;

[[noreturn]] void Fail(NrDllErrorCode code, DWORD error = 0) { throw NrDllError(code, error); }
[[noreturn]] void Io(DWORD error = GetLastError()) {
    Fail(error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION
         ? NrDllErrorCode::InUse : NrDllErrorCode::IoError, error);
}
void Cancel(std::stop_token stop) { if (stop.stop_requested()) Fail(NrDllErrorCode::Cancelled); }
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { Close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { Close(); value = std::exchange(other.value, INVALID_HANDLE_VALUE); }
        return *this;
    }
    void Close() noexcept { if (value != INVALID_HANDLE_VALUE) CloseHandle(std::exchange(value, INVALID_HANDLE_VALUE)); }
};
BY_HANDLE_FILE_INFORMATION Info(const Handle& handle) {
    BY_HANDLE_FILE_INFORMATION result{};
    if (!GetFileInformationByHandle(handle.value, &result)) Io();
    return result;
}
std::uint64_t Size(const BY_HANDLE_FILE_INFORMATION& i) {
    return (std::uint64_t(i.nFileSizeHigh) << 32) | i.nFileSizeLow;
}
bool SameFile(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
           a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}
bool Exists(const fs::path& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    const auto error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
    Io(error);
}
void OrdinaryRoot(const fs::path& path) {
    const auto name = path.wstring();
    if (!path.is_absolute() || path == path.root_path() || path.lexically_normal() != path ||
        path.filename().empty() || name.starts_with(L"\\\\") ||
        name.find(L':', 2) != name.npos || name.find(L'\0') != name.npos)
        Fail(NrDllErrorCode::InvalidPath);
}
bool IdValid(const std::string& id) {
    return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
void CheckId(const std::string& id) { if (!IdValid(id)) Fail(NrDllErrorCode::InvalidPath); }
void CheckName(const std::string& name) {
    // A generous metadata bound, independent of any locale, is not a filename
    // restriction. Names may contain non-Latin scripts, emoji, slashes, etc.
    if (name.empty() || name.size() > 4096) Fail(NrDllErrorCode::InvalidName);
    auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), int(name.size()), nullptr, 0);
    if (!count) Fail(NrDllErrorCode::InvalidName);
    std::wstring wide(std::size_t(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), int(name.size()), wide.data(), count))
        Fail(NrDllErrorCode::InvalidName);
    std::vector<WORD> types(wide.size());
    if (!GetStringTypeW(CT_CTYPE1, wide.data(), count, types.data())) Io();
    bool visible = false;
    for (std::size_t n = 0; n < wide.size(); ++n) {
        if (wide[n] == 0 || (types[n] & C1_CNTRL) || wide[n] == 0x2028 || wide[n] == 0x2029)
            Fail(NrDllErrorCode::InvalidName);
        if (!(types[n] & C1_SPACE)) visible = true;
    }
    if (!visible) Fail(NrDllErrorCode::InvalidName);
}
std::string NewId() {
    GUID guid{}; wchar_t text[40]{};
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, text, 40)) Io(ERROR_GEN_FAILURE);
    std::string id;
    for (wchar_t c : text) {
        if (c >= L'0' && c <= L'9') id += char(c);
        else if (c >= L'a' && c <= L'f') id += char(c);
        else if (c >= L'A' && c <= L'F') id += char(c - L'A' + L'a');
    }
    CheckId(id); return id;
}
Handle Managed(const fs::path& path, bool directory, DWORD access = FILE_READ_ATTRIBUTES) {
    auto raw = CreateFileW(path.c_str(), access, FILE_SHARE_READ | (directory ? FILE_SHARE_WRITE : 0),
                           nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT |
                           (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
    if (raw == INVALID_HANDLE_VALUE) Io();
    Handle handle(raw); auto info = Info(handle);
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory ||
        (!directory && (info.nNumberOfLinks != 1 || GetFileType(raw) != FILE_TYPE_DISK)))
        Fail(NrDllErrorCode::UnsafePath);
    return handle;
}
std::vector<Handle> Parents(const fs::path& path, bool create, bool& complete) {
    complete = false;
    std::vector<Handle> pins;
    auto walk = path.root_path(); pins.push_back(Managed(walk, true));
    for (const auto& part : path.relative_path()) {
        walk /= part;
        if (!Exists(walk)) {
            if (!create) return pins;
            if (!CreateDirectoryW(walk.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) Io();
        }
        pins.push_back(Managed(walk, true));
    }
    complete = true; return pins;
}
std::vector<fs::path> Children(const fs::path& path) {
    std::vector<fs::path> names;
    WIN32_FIND_DATAW data{};
    auto raw = FindFirstFileW((path / L"*").c_str(), &data);
    if (raw == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return names;
        Io();
    }
    struct Find { HANDLE h; ~Find() { FindClose(h); } } find{raw};
    do {
        if (wcscmp(data.cFileName,L".") && wcscmp(data.cFileName,L"..")) names.emplace_back(data.cFileName);
    } while (FindNextFileW(raw, &data));
    if (GetLastError() != ERROR_NO_MORE_FILES) Io();
    return names;
}
void Write(const Handle& file, const char* data, std::size_t size) {
    while (size) {
        const auto chunk = DWORD(std::min<std::size_t>(size, 1024 * 1024)); DWORD written = 0;
        if (!WriteFile(file.value, data, chunk, &written, nullptr)) Io();
        if (!written) Io(ERROR_WRITE_FAULT);
        data += written; size -= written;
    }
}
void Flush(const Handle& file) { if (!FlushFileBuffers(file.value)) Io(); }
void RenameHandle(const Handle& file, const fs::path& destination, bool replace) {
    auto name = destination.wstring();
    if (name.size() > (std::numeric_limits<DWORD>::max() - sizeof(FILE_RENAME_INFO)) / sizeof(wchar_t))
        Fail(NrDllErrorCode::InvalidPath);
    const auto bytes = name.size() * sizeof(wchar_t);
    std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + bytes, 0);
    auto info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->ReplaceIfExists = replace ? TRUE : FALSE;
    info->FileNameLength = DWORD(bytes); std::memcpy(info->FileName, name.data(), bytes);
    if (!SetFileInformationByHandle(file.value, FileRenameInfo, info, DWORD(buffer.size()))) Io();
}
void DeleteHandle(Handle& file) {
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (!SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition))) Io();
    file.Close();
}
void QuietDelete(Handle& file) noexcept {
    if (file.value == INVALID_HANDLE_VALUE) return;
    FILE_DISPOSITION_INFO disposition{TRUE};
    SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition));
    file.Close();
}
Json ReadJson(const fs::path& path) {
    if (!Exists(path)) Fail(NrDllErrorCode::CorruptMetadata);
    auto file = Managed(path, false, GENERIC_READ); const auto length = Size(Info(file));
    if (length > metadataLimit) Fail(NrDllErrorCode::CorruptMetadata);
    std::string bytes(std::size_t(length), '\0'); DWORD read = 0;
    if (!ReadFile(file.value, bytes.data(), DWORD(bytes.size()), &read, nullptr)) Io();
    if (read != bytes.size()) Fail(NrDllErrorCode::CorruptMetadata);
    try {
        auto result = Json::parse(bytes);
        if (!result.is_object() || result.at("schemaVersion") != 1) Fail(NrDllErrorCode::CorruptMetadata);
        return result;
    } catch (const Json::exception&) { Fail(NrDllErrorCode::CorruptMetadata); }
}
void SaveJson(const fs::path& path, const Json& document) {
    auto bytes = document.dump(); if (bytes.size() > metadataLimit) Fail(NrDllErrorCode::CorruptMetadata);
    const auto temporary = path.parent_path() / (L".pending-" + fs::path(NewId()).wstring());
    Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE, 0, nullptr,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) Io();
    try { Write(file, bytes.data(), bytes.size()); Flush(file); RenameHandle(file, path, true); }
    catch (...) { QuietDelete(file); throw; }
}
NrDllProfile ReadProfile(const fs::path& root, const std::string& id, Json* document = nullptr) {
    auto json = ReadJson(root / fs::path(id) / L"profile.json");
    try {
        NrDllProfile profile;
        profile.id = json.at("id").get<std::string>(); profile.name = json.at("name").get<std::string>();
        profile.sha256 = json.at("sha256").get<std::string>();
        if (profile.id != id || !json.at("bytes").is_number_unsigned() || profile.sha256.size() != 64 ||
            !std::all_of(profile.sha256.begin(),profile.sha256.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))
            Fail(NrDllErrorCode::CorruptMetadata);
        profile.bytes = json.at("bytes").get<std::uint64_t>();
        try { CheckName(profile.name); } catch (const NrDllError&) { Fail(NrDllErrorCode::CorruptMetadata); }
        profile.dllPath = root / fs::path(id) / dllName;
        if (document) *document = std::move(json);
        return profile;
    } catch (const Json::exception&) { Fail(NrDllErrorCode::CorruptMetadata); }
}
std::string ReadDefault(const fs::path& root) {
    const auto path = root / L"default.json";
    if (!Exists(path)) return {};
    auto json = ReadJson(path);
    try {
        auto id = json.at("profileId").get<std::string>();
        if (!id.empty() && !IdValid(id)) Fail(NrDllErrorCode::CorruptMetadata);
        return id;
    } catch (const Json::exception&) { Fail(NrDllErrorCode::CorruptMetadata); }
}
void SaveDefault(const fs::path& root, const std::string& id) {
    // An explicit choice/clear may recover malformed default metadata. Preserve
    // future fields whenever the existing document is readable and supported.
    Json json = { {"schemaVersion",1}, {"profileId",id} };
    if (Exists(root / L"default.json")) {
        try { json = ReadJson(root / L"default.json"); }
        catch (const NrDllError& e) { if (e.Code() != NrDllErrorCode::CorruptMetadata) throw; }
    }
    json["profileId"] = id; SaveJson(root / L"default.json", json);
}
class Hash {
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<unsigned char> object_;
public:
    Hash() {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) Io(ERROR_GEN_FAILURE);
        DWORD length = 0, returned = 0;
        if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &returned, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm_,0); algorithm_ = nullptr; Io(ERROR_GEN_FAILURE);
        }
        try { object_.resize(length); } catch (...) { BCryptCloseAlgorithmProvider(algorithm_,0); throw; }
        if (BCryptCreateHash(algorithm_, &hash_, object_.data(), length, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm_,0); algorithm_ = nullptr; Io(ERROR_GEN_FAILURE);
        }
    }
    ~Hash() { if (hash_) BCryptDestroyHash(hash_); if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_,0); }
    Hash(const Hash&) = delete;
    void Add(PUCHAR data, DWORD bytes) { if (BCryptHashData(hash_, data, bytes, 0) < 0) Io(ERROR_GEN_FAILURE); }
    std::string Finish() {
        std::array<unsigned char,32> digest{};
        if (BCryptFinishHash(hash_,digest.data(),DWORD(digest.size()),0) < 0) Io(ERROR_GEN_FAILURE);
        constexpr char hex[] = "0123456789abcdef"; std::string result; result.reserve(64);
        for (auto c : digest) { result += hex[c >> 4]; result += hex[c & 15]; } return result;
    }
};
std::pair<std::string,std::uint64_t> Stream(const Handle& source, const Handle* destination, std::stop_token stop) {
    Hash hash; std::vector<unsigned char> buffer(1024 * 1024); std::uint64_t total = 0;
    for (;;) {
        Cancel(stop); DWORD read = 0;
        if (!ReadFile(source.value,buffer.data(),DWORD(buffer.size()),&read,nullptr)) Io();
        if (!read) break;
        if (destination) Write(*destination,reinterpret_cast<const char*>(buffer.data()),read);
        hash.Add(buffer.data(),read); total += read;
    }
    Cancel(stop); return {hash.Finish(),total};
}
// Cleanup only objects this import created, by identity/handle. Never recurse
// through a staging folder or erase unexpected entries after a failed import.
struct Stage {
    fs::path path;
    Handle directory, payload, metadata;
    BY_HANDLE_FILE_INFORMATION payloadId{}, metadataId{};
    bool payloadCreated = false, metadataCreated = false, committed = false;
    void Clean(const wchar_t* name, Handle& handle, bool created, const BY_HANDLE_FILE_INFORMATION& identity) noexcept {
        if (!created) return;
        if (handle.value != INVALID_HANDLE_VALUE) { QuietDelete(handle); return; }
        try {
            auto current = Managed(path / name,false,FILE_READ_ATTRIBUTES | DELETE);
            if (SameFile(Info(current),identity)) QuietDelete(current);
        } catch (...) {}
    }
    ~Stage() {
        if (committed) return;
        Clean(L"profile.json",metadata,metadataCreated,metadataId);
        Clean(dllName,payload,payloadCreated,payloadId);
        QuietDelete(directory);
    }
};
void Writable(bool writable) { if (!writable) Fail(NrDllErrorCode::ReadOnly); }
} // namespace

class NrDllAssetLease {
public:
    std::vector<Handle> parents;
    Handle file;
};
namespace {
NrDllSelection AcquireInternal(const fs::path& root, const std::string& id, std::stop_token stop) {
    CheckId(id); Cancel(stop);
    auto lease = std::make_shared<NrDllAssetLease>(); bool complete = false;
    lease->parents = Parents(root / fs::path(id), false, complete);
    if (!complete) Fail(NrDllErrorCode::MissingProfile);
    auto profile = ReadProfile(root,id);
    if (!Exists(profile.dllPath)) Fail(NrDllErrorCode::ChangedAsset);
    lease->file = Managed(profile.dllPath,false,GENERIC_READ);
    if (Size(Info(lease->file)) != profile.bytes) Fail(NrDllErrorCode::ChangedAsset);
    const auto [hash,bytes] = Stream(lease->file,nullptr,stop);
    if (bytes != profile.bytes || hash != profile.sha256) Fail(NrDllErrorCode::ChangedAsset);
    profile.available = true;
    return {std::move(profile),std::move(lease)};
}
} // namespace

NrDllProfiles::NrDllProfiles(fs::path root, bool writable) : root_(std::move(root)), writable_(writable) {
    OrdinaryRoot(root_);
}
NrDllCatalog NrDllProfiles::List() const {
    std::lock_guard lock(storeMutex); NrDllCatalog result; bool complete = false;
    auto parents = Parents(root_,false,complete); if (!complete) return result;
    try { result.defaultId = ReadDefault(root_); }
    catch (const NrDllError& e) { result.issues.push_back({{},e.Code()}); }
    for (const auto& name : Children(root_)) {
        const auto wide = name.wstring();
        if (wide.size() != 32 || !std::all_of(wide.begin(),wide.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');})) continue;
        std::string id; id.reserve(wide.size());
        for (wchar_t c : wide) id += static_cast<char>(c); // ASCII was validated above.
        try {
            auto directory = Managed(root_/name,true); auto profile = ReadProfile(root_,id);
            try {
                if (Exists(profile.dllPath)) { auto file = Managed(profile.dllPath,false); profile.available = Size(Info(file)) == profile.bytes; }
                if (!profile.available) result.issues.push_back({id,NrDllErrorCode::ChangedAsset});
            } catch (const NrDllError& e) { result.issues.push_back({id,e.Code()}); }
            result.profiles.push_back(std::move(profile));
        } catch (const NrDllError& e) { result.issues.push_back({id,e.Code()}); }
    }
    std::sort(result.profiles.begin(),result.profiles.end(),[](const auto& a,const auto& b){return a.id < b.id;});
    if (!result.defaultId.empty() && std::none_of(result.profiles.begin(),result.profiles.end(),[&](const auto& p){return p.id==result.defaultId;}))
        result.issues.push_back({result.defaultId,NrDllErrorCode::MissingProfile});
    return result;
}
NrDllProfile NrDllProfiles::Import(const fs::path& source, const std::string& name, std::stop_token stop) {
    std::lock_guard lock(storeMutex); Writable(writable_); CheckName(name); Cancel(stop);
    if (_wcsicmp(source.filename().c_str(),dllName)) Fail(NrDllErrorCode::WrongFilename);
    // User source aliases/network locations may be copied. Only the owned vault
    // rejects links; the source handle follows an alias then locks the real file.
    Handle input(CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
    if (input.value == INVALID_HANDLE_VALUE) Io();
    const auto sourceInfo = Info(input);
    if ((sourceInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || GetFileType(input.value) != FILE_TYPE_DISK)
        Fail(NrDllErrorCode::InvalidPath);
    bool complete = false; auto parents = Parents(root_,true,complete);
    const auto id = NewId(); Stage stage; stage.path = root_ / (L".import-" + fs::path(id).wstring());
    if (!CreateDirectoryW(stage.path.c_str(),nullptr)) Io();
    stage.directory = Managed(stage.path,true,FILE_READ_ATTRIBUTES | DELETE);
    stage.payload = Handle(CreateFileW((stage.path/dllName).c_str(),GENERIC_WRITE | DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if (stage.payload.value == INVALID_HANDLE_VALUE) Io();
    stage.payloadCreated = true; stage.payloadId = Info(stage.payload);
    const auto [hash,bytes] = Stream(input,&stage.payload,stop); Flush(stage.payload);
    if (bytes != Size(sourceInfo)) Fail(NrDllErrorCode::ChangedAsset);
    auto document = Json{{"schemaVersion",1},{"id",id},{"name",name},{"sha256",hash},{"bytes",bytes}}.dump();
    stage.metadata = Handle(CreateFileW((stage.path/L"profile.json").c_str(),GENERIC_WRITE | DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if (stage.metadata.value == INVALID_HANDLE_VALUE) Io();
    stage.metadataCreated = true; stage.metadataId = Info(stage.metadata);
    Write(stage.metadata,document.data(),document.size()); Flush(stage.metadata); Cancel(stop);
    stage.payload.Close(); stage.metadata.Close();
    const auto committed = root_ / fs::path(id);
    RenameHandle(stage.directory,committed,false); stage.committed = true;
    // Commit is the final cancellation boundary. Never report cancellation after
    // publishing a profile, and never implicitly change the separate default.
    return {id,name,hash,committed/dllName,bytes,true};
}
void NrDllProfiles::Rename(const std::string& id, const std::string& name) {
    std::lock_guard lock(storeMutex); Writable(writable_); CheckId(id); CheckName(name);
    bool complete = false; auto pins = Parents(root_/fs::path(id),false,complete);
    if (!complete) Fail(NrDllErrorCode::MissingProfile);
    Json document; ReadProfile(root_,id,&document); document["name"] = name;
    SaveJson(root_/fs::path(id)/L"profile.json",document);
}
void NrDllProfiles::SetDefault(const std::string& id) {
    std::lock_guard lock(storeMutex); Writable(writable_); CheckId(id);
    bool complete = false; auto pins = Parents(root_/fs::path(id),false,complete);
    if (!complete) Fail(NrDllErrorCode::MissingProfile);
    ReadProfile(root_,id); SaveDefault(root_,id);
}
void NrDllProfiles::ClearDefault() {
    std::lock_guard lock(storeMutex); Writable(writable_);
    bool complete = false; auto pins = Parents(root_,false,complete);
    if (complete) SaveDefault(root_,{});
}
void NrDllProfiles::Remove(const std::string& id) {
    std::lock_guard lock(storeMutex); Writable(writable_); CheckId(id);
    bool complete = false; auto pins = Parents(root_,false,complete);
    if (!complete || !Exists(root_/fs::path(id))) Fail(NrDllErrorCode::MissingProfile);
    const auto folder = root_/fs::path(id);
    // DELETE access fails while any queue selection pins this directory.
    auto directory = Managed(folder,true,FILE_READ_ATTRIBUTES | DELETE);
    ReadProfile(root_,id);
    for (const auto& name : Children(folder))
        if (name != L"profile.json" && name != dllName) Fail(NrDllErrorCode::UnsafePath);
    auto metadata = Managed(folder/L"profile.json",false,FILE_READ_ATTRIBUTES | DELETE);
    Handle payload;
    if (Exists(folder/dllName)) payload = Managed(folder/dllName,false,FILE_READ_ATTRIBUTES | DELETE);
    // All ownership/busy checks precede changes. Clear only this default, never
    // pick a replacement. Partial I/O failure stays visible on subsequent List.
    if (ReadDefault(root_) == id) SaveDefault(root_,{});
    if (payload.value != INVALID_HANDLE_VALUE) DeleteHandle(payload);
    DeleteHandle(metadata); DeleteHandle(directory);
}
NrDllSelection NrDllProfiles::Acquire(const std::string& id, std::stop_token stop) const {
    std::lock_guard lock(storeMutex); return AcquireInternal(root_,id,stop);
}
std::optional<NrDllSelection> NrDllProfiles::AcquireDefault(std::stop_token stop) const {
    std::lock_guard lock(storeMutex); Cancel(stop);
    bool complete = false; auto parents = Parents(root_,false,complete);
    if (!complete) return std::nullopt;
    auto id = ReadDefault(root_); if (id.empty()) return std::nullopt;
    return AcquireInternal(root_,id,stop);
}
} // namespace nh
