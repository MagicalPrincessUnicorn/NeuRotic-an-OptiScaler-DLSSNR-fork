#include "ui/ProfileCache.h"
#include "ManualLibrary.h"
#include <fstream>
#include <iostream>

int RunProfileCacheTests() {
    int failed = 0;
    auto check = [&](bool ok, const char* label) {
        std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
        if (!ok) ++failed;
    };
    auto fixture = nh::UserRoot() / L"profile-cache-tests";
    if (std::filesystem::exists(fixture)) throw std::runtime_error("Prior profile fixture must be inspected first");
    std::filesystem::create_directories(fixture / L"Game Ω" / L"NeuRotic" / L"Installer");
    auto exe = fixture / L"Game Ω" / L"Game.exe";
    auto ini = exe.parent_path() / L"OptiScaler.ini";
    auto state = exe.parent_path() / L"NeuRotic" / L"Installer" / L"Current-Install.json";
    auto target = nh::Utf8(exe.wstring());
    wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);
    std::filesystem::copy_file(std::filesystem::path(system)/L"cmd.exe",exe);
    std::ofstream(ini, std::ios::binary) << "[Menu]\nScale=auto\n";
    std::ofstream(state, std::ios::binary) << "{\"status\":\"installed-verified\"}";
    nh::Json inspection = {{"target", {{"executable", target}, {"directory", nh::Utf8(exe.parent_path().wstring())}}},
        {"state", {{"status", "installed-verified"}, {"selected_proxy", "dxgi.dll"}}},
        {"settings", nh::Json::array({{{"section", "Menu"}, {"key", "Scale"}, {"value", "auto"}}})},
        {"recoveryRequired", false}};
    check(!nh::LoadGameProfile(target), "NH-PROFILE missing cache is an ordinary miss");
    check(nh::SaveGameProfile(target, inspection), "NH-PROFILE verified inspection persists");
    auto wrongArchitecture=inspection;wrongArchitecture["target"]["bitness"]=32;
    check(!nh::SaveGameProfile(target,wrongArchitecture),"NH-PROFILE architecture-mismatched inspection refused");
    auto loaded = nh::LoadGameProfile(target);
    check(loaded && loaded->inspection == inspection && !loaded->checkedUtc.empty(),
          "NH-PROFILE new reader restores target-bound observed settings and timestamp");
    wchar_t windows[MAX_PATH];GetWindowsDirectoryW(windows,MAX_PATH);
    auto x86=std::filesystem::path(windows)/L"SysWOW64/cmd.exe";
    std::filesystem::copy_file(x86,exe,std::filesystem::copy_options::overwrite_existing);
    check(!nh::LoadGameProfile(target),"NH-PROFILE replacing selected game with PE32 invalidates x64 cache");
    check(nh::SaveGameProfile(target,wrongArchitecture)&&nh::LoadGameProfile(target).has_value(),"NH-PROFILE matching PE32 game and inspection persist");
    std::filesystem::copy_file(std::filesystem::path(system)/L"cmd.exe",exe,std::filesystem::copy_options::overwrite_existing);
    check(!nh::LoadGameProfile(target),"NH-PROFILE fresh PE validation refuses old PE32 cache after architecture change");
    nh::SaveGameProfile(target,inspection);
    auto large = inspection;
    // A valid 2 MiB Object Rules document occupies up to 4 MiB when encoded.
    // The cache also includes the other projected settings and identity metadata.
    large["settings"].push_back({{"section", "ObjectRules"}, {"key", "ProfileHex"},
                                {"value", std::string(4 * 1024 * 1024, 'a')}});
    check(nh::SaveGameProfile(target, large), "NH-PROFILE maximum-size rules plus metadata persist");
    loaded = nh::LoadGameProfile(target);
    check(loaded && loaded->inspection == large,
          "NH-PROFILE full large rules projection round-trips without truncation");
    auto oversized = large;
    oversized["settings"].back()["value"] = std::string(16 * 1024 * 1024, 'b');
    check(!nh::SaveGameProfile(target, oversized), "NH-PROFILE over-limit inspection is refused");
    loaded = nh::LoadGameProfile(target);
    check(loaded && loaded->inspection == large,
          "NH-PROFILE oversize refusal preserves the last valid cached inspection");
    nh::SaveGameProfile(target, inspection);
    auto changed = inspection;
    changed["target"]["executable"] = nh::Utf8((fixture/L"Other.exe").wstring());
    check(!nh::SaveGameProfile(target, changed) && nh::LoadGameProfile(target).has_value(),
          "NH-PROFILE mismatched receipt cannot replace this game's snapshot");
    std::ofstream(ini, std::ios::binary | std::ios::app) << "; external setting edit\n";
    check(!nh::LoadGameProfile(target), "NH-PROFILE external settings edit invalidates cached details");
    check(nh::SaveGameProfile(target, inspection), "NH-PROFILE a fresh inspection can replace a stale snapshot");
    std::ofstream(state, std::ios::binary | std::ios::app) << " ";
    check(!nh::LoadGameProfile(target), "NH-PROFILE changed installation receipt invalidates cached state");
    nh::SaveGameProfile(target, inspection);
    std::ofstream(exe, std::ios::binary | std::ios::app) << "new version";
    check(!nh::LoadGameProfile(target), "NH-PROFILE replaced executable invalidates cached game identity");
    nh::SaveGameProfile(target, inspection);
    std::ofstream(exe.parent_path()/L"dxgi.dll", std::ios::binary) << "new proxy";
    check(!nh::LoadGameProfile(target), "NH-PROFILE changed proxy invalidates cached installation summary");
    nh::SaveGameProfile(target, inspection);
    nh::RemoveGameProfile(target);
    check(!nh::LoadGameProfile(target), "NH-PROFILE explicit invalidation survives a new reader");
    auto profileDir = nh::UserRoot()/L"profiles";
    nh::SaveGameProfile(target, inspection);
    std::filesystem::path cache;
    if (std::filesystem::exists(profileDir)) for (auto& entry:std::filesystem::directory_iterator(profileDir)) {
        if (!entry.is_regular_file() || entry.path().extension()!=L".json") continue;
        std::ifstream file(entry.path());
        try {auto value=nh::Json::parse(file);if(value.value("executable","")==target)cache=entry.path();}catch(...){}
    }
    if (!cache.empty()) {
        {std::ofstream file(cache, std::ios::binary);file << std::string(16 * 1024 * 1024 + 1, 'x');}
        check(!nh::LoadGameProfile(target), "NH-PROFILE over-limit cache on disk is refused");
        nh::SaveGameProfile(target, inspection);
        {std::ofstream file(cache);file << "{broken json";}
        check(!nh::LoadGameProfile(target), "NH-PROFILE malformed cache falls back without throwing");
        nh::SaveGameProfile(target, inspection);
        nh::Json value;{std::ifstream file(cache);value=nh::Json::parse(file);}value["schemaVersion"]=999;
        {std::ofstream file(cache);file<<value.dump();}
        check(!nh::LoadGameProfile(target), "NH-PROFILE incompatible schema falls back to fresh inspection");
        check(!nh::SaveGameProfile(target, inspection), "AUDIT-RL05: newer profile schema refuses an older writer");
        nh::RemoveGameProfile(target);check(std::filesystem::exists(cache),"AUDIT-RL05: ordinary invalidation preserves newer profile schema");
        auto alias=fixture/L"cache-alias.json";
        if (CreateHardLinkW(alias.c_str(),cache.c_str(),nullptr)) {
            check(!nh::LoadGameProfile(target), "NH-PROFILE linked cache file is refused");
            std::filesystem::remove(alias);
        } else check(false,"NH-PROFILE hardlink fixture could not be created");
        nh::RemoveGameProfile(target);
    } else check(false,"NH-PROFILE cache file was not found for corruption fixtures");
    // This fixture contains only ordinary files we created; never follow links.
    for(auto& entry:std::filesystem::recursive_directory_iterator(fixture))
        if(GetFileAttributesW(entry.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error("Linked profile fixture must be preserved");
    std::filesystem::remove_all(fixture);
    return failed;
}

#ifdef NEUROTIC_PROFILE_CACHE_STANDALONE
namespace nh {std::filesystem::path UserRoot(){return L"C:/NeuRotic/flagship-all-merges/builds/hub/profile-cache-fixture";}}
int main(){try{return RunProfileCacheTests()?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
