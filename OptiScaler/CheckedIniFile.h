#pragma once

#include <windows.h>
#include <SimpleIni.h>
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>
#pragma comment(lib, "advapi32.lib")

namespace Neurotic::CheckedIniFile
{
struct Result { bool succeeded=false; DWORD errorCode=ERROR_SUCCESS; std::string stage; };
// SimpleIni's pathname writer does not propagate stdio write/close errors.
// Keep its serialization, including the pathname API's default UTF-8 signature,
// but publish only a completely written, flushed and closed sibling file.
inline Result SaveDetailed(const CSimpleIniA& ini, const std::filesystem::path& path)
{
    const auto fail=[](const char* stage,DWORD error=GetLastError()) -> Result {
        return {false,error==ERROR_SUCCESS?ERROR_GEN_FAILURE:error,stage};
    };
    std::string bytes;
    if (ini.Save(bytes, true) < 0) return fail("serialize",ERROR_INVALID_DATA);

    const DWORD attributes = GetFileAttributesW(path.c_str());
    const bool exists = attributes != INVALID_FILE_ATTRIBUTES;
    std::vector<unsigned char> security;
    DWORD securityInformation = DACL_SECURITY_INFORMATION;
    if (!exists && GetLastError() != ERROR_FILE_NOT_FOUND) return fail("inspect");
    if (exists)
    {
        // Do not replace a read-only file, directory or the link itself. Check
        // the old writer's write permission without changing any existing bytes.
        if (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            return fail("inspect",ERROR_ACCESS_DENIED);
        const HANDLE access = CreateFileW(path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (access == INVALID_HANDLE_VALUE) return fail("access");
        if (!CloseHandle(access)) return fail("close-access");
        DWORD size = 0;
        if (GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &size) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) return fail("read-security");
        security.resize(size);
        if (!GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, security.data(), size, &size)) return fail("read-security");
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        if (!GetSecurityDescriptorControl(security.data(), &control, &revision)) return fail("read-security");
        securityInformation |= (control & SE_DACL_PROTECTED)
            ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION;
    }

    struct Temporary
    {
        std::filesystem::path path;
        HANDLE file = INVALID_HANDLE_VALUE;
        ~Temporary()
        {
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            if (!path.empty()) DeleteFileW(path.c_str());
        }
    } temporary;

    static std::atomic<unsigned long long> sequence { 0 };
    for (unsigned attempt = 0; attempt < 64; ++attempt)
    {
        auto candidate = path;
        candidate += L".neurotic-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)) + L".tmp";
        if (exists)
        {
            // CopyFile preserves attributes and named streams. Preserve the
            // DACL explicitly below; CopyFile can inherit the parent DACL.
            // Only the default stream is replaced by the checked write.
            if (!CopyFileW(path.c_str(), candidate.c_str(), TRUE))
            {
                const DWORD error = GetLastError();
                if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
                // A failed copy does not establish ownership of candidate.
                // Never delete a potentially independent file at that path.
                return fail("copy-staging",error);
            }
            temporary.path = std::move(candidate);
            temporary.file = CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr,
                TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (temporary.file == INVALID_HANDLE_VALUE) return fail("open-staging");
            break;
        }
        const HANDLE file = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            temporary.path = std::move(candidate);
            temporary.file = file;
            break;
        }
        // A stale or simultaneous writer's file belongs to that writer.
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return fail("open-staging");
    }
    if (temporary.file == INVALID_HANDLE_VALUE) return fail("open-staging",ERROR_FILE_EXISTS);

    Result failure;
    bool complete = true;
    for (size_t offset = 0; offset < bytes.size();)
    {
        const size_t remaining = bytes.size() - offset;
        const DWORD count = remaining > MAXDWORD ? MAXDWORD : static_cast<DWORD>(remaining);
        DWORD written = 0;
        const bool wrote=WriteFile(temporary.file, bytes.data() + offset, count, &written, nullptr)!=FALSE;
        if (!wrote || written != count)
        {
            failure=fail("write",wrote?ERROR_WRITE_FAULT:GetLastError());
            complete = false;
            break;
        }
        offset += written;
    }
    if (complete && !FlushFileBuffers(temporary.file)) { complete=false;failure=fail("flush"); }
    const bool closed = CloseHandle(temporary.file) != FALSE;
    const DWORD closeError=closed?ERROR_SUCCESS:GetLastError();
    temporary.file = INVALID_HANDLE_VALUE;
    if (!complete) return failure;
    if (!closed) return fail("close",closeError);
    if (exists && !SetFileSecurityW(temporary.path.c_str(), securityInformation, security.data())) return fail("restore-security");

    // One same-volume rename publishes the completed file. ReplaceFile can
    // delete the original before reporting some failures; do not use it here.
    // A new file must not overwrite a destination created during this save.
    const bool published = MoveFileExW(temporary.path.c_str(), path.c_str(),
        MOVEFILE_WRITE_THROUGH | (exists ? MOVEFILE_REPLACE_EXISTING : 0)) != FALSE;
    if (published) temporary.path.clear();
    return published?Result{true,ERROR_SUCCESS,"complete"}:fail("publish");
}
inline bool Save(const CSimpleIniA& ini, const std::filesystem::path& path) { return SaveDetailed(ini,path).succeeded; }
}
