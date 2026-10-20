#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <optional>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <thread>
#include <atomic>
#include "../OptiScaler/dlssnr/NrExperimentalPolicy.h"
#include "../OptiScaler/mfg/ExperimentalMfgPolicy.h"

enum class Fault { None, Write, ShortWrite, Flush, Close, Replace, PartialReplace, Copy, Security };
static Fault fault = Fault::None;
static FILE* readOnlyStream = nullptr;
static bool observedOsFailure = false;
static thread_local bool wroteTemporary = false;
static int FaultFputs(const char* text, FILE* file)
{
    if (fault == Fault::Write) {
        const int result = std::fputs(text, readOnlyStream);
        observedOsFailure |= result == EOF && std::ferror(readOnlyStream) != 0;
        return result;
    }
    return std::fputs(text, file);
}
#define fputs FaultFputs
#include <SimpleIni.h>
#undef fputs

inline BOOL WINAPI FaultWrite(HANDLE file, LPCVOID data, DWORD count, LPDWORD written, LPOVERLAPPED overlap)
{
    wroteTemporary = true;
    if (fault == Fault::Write) {
        const BOOL result = WriteFile(INVALID_HANDLE_VALUE, data, count, written, overlap);
        observedOsFailure |= result == FALSE;
        return result;
    }
    if (fault == Fault::ShortWrite) count /= 2;
    return WriteFile(file, data, count, written, overlap);
}
inline BOOL WINAPI FaultFlush(HANDLE file)
{
    return FlushFileBuffers(fault == Fault::Flush ? INVALID_HANDLE_VALUE : file);
}
inline BOOL WINAPI FaultClose(HANDLE file)
{
    const BOOL closed = CloseHandle(file);
    // Close a real handle, then model a reported close failure. No handle is leaked.
    return fault == Fault::Close && wroteTemporary ? FALSE : closed;
}
inline BOOL WINAPI FaultMove(LPCWSTR from, LPCWSTR to, DWORD flags)
{
    // The fixture directory exists, so the real OS refuses this replacement.
    const auto directory = std::filesystem::path(to).parent_path();
    return MoveFileExW(from, fault == Fault::Replace || fault == Fault::PartialReplace ? directory.c_str() : to, flags);
}
inline BOOL WINAPI FaultReplace(LPCWSTR to, LPCWSTR from, LPCWSTR backup, DWORD flags, LPVOID exclude, LPVOID reserved)
{
    if (fault == Fault::PartialReplace) {
        // Documented ReplaceFileW ERROR_UNABLE_TO_MOVE_REPLACEMENT without a
        // backup: the replaced file can already be gone when failure returns.
        DeleteFileW(to);
        SetLastError(ERROR_UNABLE_TO_MOVE_REPLACEMENT);
        return FALSE;
    }
    const auto directory = std::filesystem::path(to).parent_path();
    return ReplaceFileW(fault == Fault::Replace ? directory.c_str() : to, from, backup, flags, exclude, reserved);
}
inline BOOL WINAPI FaultCopy(LPCWSTR from, LPCWSTR to, BOOL failIfExists)
{
    const auto missing = std::filesystem::path(from).parent_path() / L"absent-copy-source";
    return CopyFileW(fault == Fault::Copy ? missing.c_str() : from, to, failIfExists);
}
inline BOOL WINAPI FaultSecurity(LPCWSTR path, SECURITY_INFORMATION information, PSECURITY_DESCRIPTOR descriptor)
{
    const auto missing = std::filesystem::path(path).parent_path() / L"absent-security-target";
    return SetFileSecurityW(fault == Fault::Security ? missing.c_str() : path, information, descriptor);
}
#define WriteFile FaultWrite
#define FlushFileBuffers FaultFlush
#define CloseHandle FaultClose
#define MoveFileExW FaultMove
#define ReplaceFileW FaultReplace
#define CopyFileW FaultCopy
#define SetFileSecurityW FaultSecurity
#if __has_include("../OptiScaler/CheckedIniFile.h")
#include "../OptiScaler/CheckedIniFile.h"
#define HAS_CHECKED_INI 1
#endif
#undef WriteFile
#undef FlushFileBuffers
#undef CloseHandle
#undef ReplaceFileW
#undef CopyFileW
#undef SetFileSecurityW
// Keep the rename fault active for the original production subset body too.

static CSimpleIniA ini;
namespace Neurotic::Mfg::Experimental {
static SessionPreferences preferences;
void SettingsSaved(Preferences value, bool success) { preferences.Saved(value, success); }
}
struct Config {
    static Config* current;
    static Config* Instance() { return current; }
    std::filesystem::path absoluteFileName;
    bool AllowGameMouse = false, AllowGameKeyboard = false, AllowGameController = false;
    NrOptional<bool> FGXeFGDepthInverted{false}, FGXeFGJitteredMV{false}, FGXeFGHighResMV{false};
    NrOptional<bool> FGDLSSGExperimentalUnlockRTX30{false}, FGDLSSGExperimentalUnlockRTX20{false};
    NrOptional<bool> DlssNrExperimentalMode{false}, DlssNrOverrideMultipassGuardrails{false},
        DlssNrOverrideHdrGuardrails{false}, DlssNrOverrideFgGuardrails{false},
        DlssNrPreSrSoftReset{false}, DlssNrPreparedDepth{false};
    bool SaveMenuInputSettings(bool, bool, bool);
    bool SaveExperimentalSettings(bool, bool, bool, bool, bool);
    bool SaveXeFG();
    bool SaveGlobalTail();
};
Config* Config::current = nullptr;
static std::string GetBoolValue(std::optional<bool> value) { return value ? (*value ? "true" : "false") : "auto"; }
#define LOG_INFO(...) ((void)0)
#include "CheckedIniFixture.inc"
#undef MoveFileExW

static int checks = 0;
static void Need(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
static std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
static void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << bytes;
    file.close();
    Need(!file.fail(), "fixture write");
}
static void NoTemps(const std::filesystem::path& root) {
    for (const auto& entry : std::filesystem::directory_iterator(root))
        Need(entry.path().extension() != L".tmp", "transaction removed its temporary file");
}
static std::wstring Dacl(const std::filesystem::path& path) {
    DWORD size = 0;
    GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &size);
    Need(size != 0, "security descriptor size");
    std::vector<unsigned char> descriptor(size);
    Need(GetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION, descriptor.data(), size, &size) != FALSE, "read file DACL");
    LPWSTR text = nullptr;
    Need(ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor.data(), SDDL_REVISION_1,
         DACL_SECURITY_INFORMATION, &text, nullptr) != FALSE, "DACL to SDDL");
    std::wstring value(text);
    LocalFree(text);
    return value;
}
int main(int argc, char**) {
    const auto root = std::filesystem::absolute("builds/ui-release/ini-tests/fixture");
    std::filesystem::create_directories(root);
    const auto path = root / L"profile.ini";
    const std::string original = "; user comment\r\n[Unknown]\r\nKeep = literal\r\n[Menu]\r\nAllowGameMouse = false\r\n[DlssNr]\r\nRoute = auto\r\n";
    try {
        Config config;
        Config::current = &config;
        config.absoluteFileName = path;
        config.FGDLSSGExperimentalUnlockRTX30 = true;
        Write(root / "readonly-stream.txt", "fixture");
        readOnlyStream = _wfopen((root / "readonly-stream.txt").c_str(), L"rb");
        Need(readOnlyStream != nullptr, "real read-only stream opened");
#ifdef HAS_CHECKED_INI
        auto collision = path;
        collision += L".neurotic-" + std::to_wstring(GetCurrentProcessId()) + L"-0.tmp";
        Write(collision, "belongs to another writer");
#endif
        for (Fault stage : {Fault::Write, Fault::ShortWrite, Fault::Flush, Fault::Close, Fault::Replace, Fault::PartialReplace, Fault::Copy, Fault::Security}) {
            Write(path, original);
            ini.Reset(); Need(ini.LoadFile(path.c_str()) == SI_OK, "load preimage");
            fault = stage;
            wroteTemporary = false;
            Need(!config.SaveMenuInputSettings(true, true, true), "failed I/O must return false, not publish success");
            Need(Read(path) == original, "failure must preserve original bytes");
            Need(!config.AllowGameMouse && !config.AllowGameKeyboard && !config.AllowGameController,
                 "failed save must not publish menu intent");
            DlssNr::ExperimentalPolicy::SessionReady.store(true);
            const auto generation = DlssNr::ExperimentalPolicy::Generation.load();
            wroteTemporary = false;
            Need(!config.SaveExperimentalSettings(false, false, false, false, true), "experimental save refuses I/O failure");
            Need(!config.DlssNrPreparedDepth.value_or_default(), "failed experimental save preserves intent");
            Need(DlssNr::ExperimentalPolicy::SessionReady.load() && DlssNr::ExperimentalPolicy::Generation.load() == generation,
                 "failed save preserves readiness and generation");
            Need(Read(path) == original, "experimental save preserves preimage");
            wroteTemporary = false;
            Need(!config.SaveXeFG(), "XeFG save refuses I/O failure");
            Need(Read(path) == original, "XeFG failure preserves preimage");
            wroteTemporary = false;
            Need(!config.SaveGlobalTail(), "global save boundary refuses I/O failure");
            Need(Read(path) == original, "global save failure preserves preimage");
            Need(!Neurotic::Mfg::Experimental::preferences.saved.rtx30, "failed global save does not publish saved MFG intent");
            fault = Fault::None;
#ifdef HAS_CHECKED_INI
            if (stage == Fault::Write) {
                Need(Read(collision) == "belongs to another writer", "CREATE_NEW collision never truncates or deletes another writer's file");
                std::filesystem::remove(collision);
            }
#endif
            NoTemps(root);
            std::cout << "PASS fault " << static_cast<int>(stage) << '\n';
            if (argc > 1) break;
        }
        Need(observedOsFailure, "write test reached a real OS/CRT write error");
        std::fclose(readOnlyStream); readOnlyStream = nullptr;
        std::filesystem::remove(root / "readonly-stream.txt");
        fault = Fault::None;
        Write(path, original);
        Need(config.SaveMenuInputSettings(true, false, true), "menu save succeeds");
        Need(config.AllowGameMouse && !config.AllowGameKeyboard && config.AllowGameController, "menu intent published");
        CSimpleIniA result;
        Need(result.LoadFile(path.c_str()) == SI_OK, "success reparses");
        Need(std::string(result.GetValue("Unknown", "Keep", "")) == "literal", "unknown key preserved");
        Need(std::string(result.GetValue("DlssNr", "Route", "")) == "auto", "targeted save preserves auto");
        Need(Read(path).find("; user comment") != std::string::npos, "comment preserved");
        Need(result.GetBoolValue("Menu", "AllowGameMouse") && !result.GetBoolValue("Menu", "AllowGameKeyboard", true), "saved menu values");
        Need(config.SaveExperimentalSettings(false, false, false, false, true), "experimental success");
        Need(config.DlssNrPreparedDepth.value_or_default() && !DlssNr::ExperimentalPolicy::SessionReady.load(), "experimental marker gate remains intact");
        config.FGXeFGDepthInverted = true;
        Need(config.SaveXeFG(), "XeFG success");
        result.Reset(); Need(result.LoadFile(path.c_str()) == SI_OK, "XeFG result loads");
        Need(result.GetBoolValue("XeFG", "DepthInverted") && std::string(result.GetValue("Unknown", "Keep", "")) == "literal",
             "XeFG value saved with existing unknown key");
        Need(config.SaveGlobalTail() && Neurotic::Mfg::Experimental::preferences.saved.rtx30,
             "global save success publishes saved MFG intent");
#ifdef HAS_CHECKED_INI
        // Compare the real serializer's old pathname save and checked output for
        // BOM/MBCS, comments, multiline/order and every tracked config fixture.
        std::vector<std::filesystem::path> profiles;
        for (const auto& entry : std::filesystem::directory_iterator("tests/nr/config/fixtures"))
            if (entry.path().extension() == L".ini") profiles.push_back(entry.path());
        const auto utf8 = root / "utf8.ini";
        Write(utf8, "\xef\xbb\xbf; UTF-8 comment\r\n[Custom]\r\nName = caf\xc3\xa9\r\n");
        profiles.push_back(utf8);
        for (const auto& profile : profiles) {
            CSimpleIniA parsed;
            Need(parsed.LoadFile(profile.c_str()) == SI_OK, "existing profile parses");
            Need(parsed.SaveFile((root / "legacy.ini").c_str()) == SI_OK, "legacy successful serializer oracle");
            Need(Neurotic::CheckedIniFile::Save(parsed, path), "checked profile save");
            Need(Read(path) == Read(root / "legacy.ini"), "successful bytes match legacy exactly");
        }
        Write(path, original);
        Need(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "mark original read-only");
        const bool readOnlySaved = Neurotic::CheckedIniFile::Save(result, path);
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        Need(!readOnlySaved && Read(path) == original, "read-only original remains unchanged");
        const HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Need(locked != INVALID_HANDLE_VALUE, "lock original without delete sharing");
        const bool lockedSaved = Neurotic::CheckedIniFile::Save(result, path);
        CloseHandle(locked);
        Need(!lockedSaved && Read(path) == original, "sharing refusal preserves bytes");
        PSECURITY_DESCRIPTOR customSecurity = nullptr;
        Need(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;WD)", SDDL_REVISION_1,
             &customSecurity, nullptr) != FALSE, "owned fixture custom DACL");
        const bool setSecurity = SetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                                 customSecurity) != FALSE;
        LocalFree(customSecurity);
        Need(setSecurity, "set owned fixture protected DACL");
        const auto originalDacl = Dacl(path);
        Write(std::filesystem::path(path.wstring() + L":owner-note"), "retained metadata");
        Need(Neurotic::CheckedIniFile::Save(result, path), "existing file replacement");
        Need(Read(std::filesystem::path(path.wstring() + L":owner-note")) == "retained metadata", "replacement preserves named stream");
        Need(Dacl(path) == originalDacl, "replacement preserves custom protected DACL");
        const auto fresh = root / L"fresh.ini";
        std::filesystem::remove(fresh);
        Need(Neurotic::CheckedIniFile::Save(result, fresh), "new file creation");
        CSimpleIniA freshParsed;
        Need(freshParsed.LoadFile(fresh.c_str()) == SI_OK && freshParsed.GetBoolValue("XeFG", "DepthInverted"), "new file roundtrip");
        Need(!Neurotic::CheckedIniFile::Save(result, root / L"absent" / L"profile.ini"), "missing parent refuses");
        std::atomic<int> successful = 0;
        std::vector<std::thread> writers;
        for (int i = 0; i < 8; ++i) writers.emplace_back([&, i] {
            CSimpleIniA independent;
            independent.SetLongValue("Concurrent", "Writer", i);
            if (Neurotic::CheckedIniFile::Save(independent, path)) ++successful;
        });
        for (auto& writer : writers) writer.join();
        Need(successful.load() > 0, "concurrent independent writers make progress");
        CSimpleIniA concurrent;
        Need(concurrent.LoadFile(path.c_str()) == SI_OK && concurrent.GetLongValue("Concurrent", "Writer", -1) >= 0,
             "concurrent file is one complete profile");
#endif
        NoTemps(root);
        for (const auto& entry : std::filesystem::directory_iterator(root)) std::filesystem::remove(entry.path());
        std::filesystem::remove(root);
        std::cout << "PASS checked INI: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " (checks=" << checks << ", realWriteFailure=" << observedOsFailure << ")\n";
        if (readOnlyStream) std::fclose(readOnlyStream);
        return 1;
    }
}
