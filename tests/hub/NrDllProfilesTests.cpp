#include "storage/NrDllProfiles.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;
static std::string currentOperation = "startup";
static void Trace(const std::string& operation) {
    currentOperation = operation;
    std::cout << "BEGIN " << operation << '\n' << std::flush;
}
static const char* CodeName(nh::NrDllErrorCode code) {
    switch(code) {
    case nh::NrDllErrorCode::ReadOnly: return "ReadOnly";
    case nh::NrDllErrorCode::InvalidPath: return "InvalidPath";
    case nh::NrDllErrorCode::InvalidName: return "InvalidName";
    case nh::NrDllErrorCode::WrongFilename: return "WrongFilename";
    case nh::NrDllErrorCode::MissingProfile: return "MissingProfile";
    case nh::NrDllErrorCode::NoDefault: return "NoDefault";
    case nh::NrDllErrorCode::InUse: return "InUse";
    case nh::NrDllErrorCode::UnsafePath: return "UnsafePath";
    case nh::NrDllErrorCode::ChangedAsset: return "ChangedAsset";
    case nh::NrDllErrorCode::CorruptMetadata: return "CorruptMetadata";
    case nh::NrDllErrorCode::IoError: return "IoError";
    case nh::NrDllErrorCode::Cancelled: return "Cancelled";
    }
    return "Unknown";
}
static std::string PathText(const fs::path& path) {
    const auto wide = path.wstring();
    const auto size = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),int(wide.size()),nullptr,0,nullptr,nullptr);
    if (!size) return "<path encoding unavailable>";
    std::string text(std::size_t(size),'\0');
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),int(wide.size()),text.data(),size,nullptr,nullptr))
        return "<path encoding unavailable>";
    return text;
}
static void Check(bool ok, const char* name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok) throw std::runtime_error(name);
}
static void Put(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out << value;
    if (!out) throw std::runtime_error("fixture write failed");
}
static std::string Get(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("fixture read failed");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
template<class Action> static void Reject(Action action, nh::NrDllErrorCode code, const char* name) {
    Trace(name);
    try { action(); } catch (const nh::NrDllError& e) {
        if(e.Code()!=code) std::cerr << "UNEXPECTED NrDllError code=" << CodeName(e.Code())
                                    << " systemError=" << e.systemError << " operation=" << currentOperation << '\n';
        Check(e.Code() == code, name); return;
    }
    Check(false, name);
}
static nh::NrDllProfile ImportProfile(nh::NrDllProfiles& store,const fs::path& source,const std::string& name) {
    Trace("Import profile: " + name);
    auto profile = store.Import(source,name);
    std::cout << "END Import id=" << profile.id << '\n' << std::flush;
    return profile;
}
static void Hardlink(const fs::path& newLink,const fs::path& existing,const char* name) {
    Trace(name);
    const auto created = CreateHardLinkW(newLink.c_str(),existing.c_str(),nullptr);
    // Capture immediately, before diagnostic I/O or any other Windows call.
    const auto error = created ? ERROR_SUCCESS : GetLastError();
    if(!created) {
        const auto newAttributes=GetFileAttributesW(newLink.c_str());
        const auto existingAttributes=GetFileAttributesW(existing.c_str());
        std::cerr << "CreateHardLinkW systemError=" << error << " operation=" << name
                  << " new=" << PathText(newLink) << " existing=" << PathText(existing)
                  << " newAttributes=" << newAttributes << " existingAttributes=" << existingAttributes << '\n';
    }
    Check(created!=FALSE,name);
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        auto root = fs::absolute(argv[1]);
        if (fs::exists(root)) throw std::runtime_error("supply a NEW disposable fixture root");
        // Break caught: visiting the manager must not create preferences/assets.
        nh::NrDllProfiles reader(root / L"profiles");
        Check(reader.List().profiles.empty() && !fs::exists(root), "read-only visit has no writes");
        const auto source = root / L"download" / L"nvngx_dlssnr.dll";
        Put(source, "abc");
        Reject([&]{reader.Import(source,"read only");}, nh::NrDllErrorCode::ReadOnly, "second session cannot import");
        nh::NrDllProfiles store(root / L"profiles", true);
        // Break caught: retaining a source path instead of an independent copy.
        const std::string unicode = "日本語 / العربية / Polski 🧪";
        auto first = ImportProfile(store,source,unicode);
        Check(!first.id.empty() && first.name == unicode && first.dllPath != source &&
              fs::exists(first.dllPath) && Get(first.dllPath) == "abc", "import owns an independent Unicode-named copy");
        Check(first.bytes == 3 && first.sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "actual arbitrary provider bytes recorded without admission whitelist");
        Check(store.List().defaultId.empty(), "import does not silently change default");
        fs::remove(source);
        Trace("Acquire first profile after download deletion");
        { auto selection = store.Acquire(first.id); Check(Get(selection.profile.dllPath) == "abc", "original download can be deleted"); }
        Trace("SetDefault first profile");
        store.SetDefault(first.id);
        auto defaultDocument = Get(root/L"profiles"/L"default.json");
        defaultDocument.insert(1,"\"futurePreference\":\"keep default metadata\",");
        Put(root/L"profiles"/L"default.json",defaultDocument);
        nh::NrDllProfiles restarted(root / L"profiles", true);
        Trace("AcquireDefault after store restart");
        Check(restarted.AcquireDefault()->profile.id == first.id, "default persists independently of original download");
        Put(source, "different DLL bytes");
        auto second = ImportProfile(store,source,unicode);
        Check(first.id != second.id && Get(first.dllPath) == "abc", "duplicate labels and filenames retain independent assets");
        Trace("Acquire queued first profile");
        auto pinned = store.Acquire(first.id);
        Trace("Rename queued first profile");
        store.Rename(first.id, "Renamed Δ");
        Trace("SetDefault second profile while first queued");
        store.SetDefault(second.id);
        Check(Get(root/L"profiles"/L"default.json").find("keep default metadata")!=std::string::npos,
              "default change preserves future preference fields");
        Check(pinned.profile.name == unicode && pinned.profile.id == first.id && Get(pinned.profile.dllPath) == "abc",
              "queued selection remains immutable after rename and default change");
        HANDLE writer = CreateFileW(first.dllPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
        Check(writer == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION, "lease blocks changing selected bytes");
        if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
        Reject([&]{store.Remove(first.id);}, nh::NrDllErrorCode::InUse, "lease blocks removing queued profile");
        auto shared = pinned;
        pinned = {};
        Reject([&]{store.Remove(first.id);}, nh::NrDllErrorCode::InUse, "copied lease remains owned until all readers drain");
        shared = {};
        const auto gameDll = root / L"game" / L"nvngx_dlssnr.dll";
        Put(gameDll, "installed provider remains here");
        Trace("Remove first profile after both lease copies drain");
        store.Remove(first.id);
        Check(!fs::exists(first.dllPath) && Get(gameDll) == "installed provider remains here", "profile removal cannot touch installed game DLL");
        Reject([&]{store.Acquire(first.id);}, nh::NrDllErrorCode::MissingProfile, "deleted reference never falls back to another DLL");
        Trace("Remove second/default profile");
        store.Remove(second.id);
        Check(store.List().defaultId.empty(), "removing default clears selection without choosing another");
        Check(Get(root/L"profiles"/L"default.json").find("keep default metadata")!=std::string::npos,
              "default clear preserves future preference fields");
        Check(!store.AcquireDefault().has_value(), "empty default preserves optional manual flow");
        Put(root / L"bad.dll", "abc");
        Reject([&]{store.Import(root / L"bad.dll", "bad");}, nh::NrDllErrorCode::WrongFilename, "expected DLL filename required");
        Reject([&]{store.Import(source, " \t ");}, nh::NrDllErrorCode::InvalidName, "blank label rejected");
        Reject([&]{store.Acquire("../../download");}, nh::NrDllErrorCode::InvalidPath, "profile ID cannot escape managed root");
        std::stop_source cancelled; cancelled.request_stop();
        const auto before = store.List().profiles.size();
        Reject([&]{store.Import(source,"cancelled",cancelled.get_token());}, nh::NrDllErrorCode::Cancelled, "cancelled import is not committed");
        Check(store.List().profiles.size() == before, "cancel leaves catalog unchanged");
        // Break caught: inherited SaveUserFile 16 MiB payload limit or fixed slots.
        Put(source, std::string(17 * 1024 * 1024, 'x'));
        auto large = ImportProfile(store,source,"large arbitrary provider");
        Check(large.bytes == 17 * 1024 * 1024 && fs::file_size(large.dllPath) == large.bytes, "streamed import exceeds preference-file limit");
        Put(source, "one");
        for (int i = 0; i < 40; ++i) ImportProfile(store,source,"Profile " + std::to_string(i));
        Trace("List after forty-profile catalog growth");
        Check(store.List().profiles.size() == 41, "catalog scales beyond fixed profile slots");
        Put(large.dllPath, "tampered");
        Reject([&]{store.Acquire(large.id);}, nh::NrDllErrorCode::ChangedAsset, "changed managed asset requires explicit reimport");
        auto missing = ImportProfile(store,source,"missing later");
        fs::remove(missing.dllPath);
        Trace("SetDefault missing-asset profile metadata");
        store.SetDefault(missing.id);
        Reject([&]{store.AcquireDefault();}, nh::NrDllErrorCode::ChangedAsset, "missing default never selects a different profile");
        auto catalog = store.List();
        auto unavailable = std::find_if(catalog.profiles.begin(),catalog.profiles.end(),[&](const auto& p){return p.id==missing.id;});
        Check(unavailable != catalog.profiles.end() && !unavailable->available, "missing asset preserves saved profile identity");
        auto foreign = ImportProfile(store,source,"unexpected extra file");
        Put(foreign.dllPath.parent_path() / L"user-notes.txt", "keep me");
        Reject([&]{store.Remove(foreign.id);}, nh::NrDllErrorCode::UnsafePath, "removal refuses unexpected files");
        Check(Get(foreign.dllPath.parent_path() / L"user-notes.txt") == "keep me", "foreign file remains untouched");
        auto sameSize = ImportProfile(store,source,"same-size tamper");
        Put(sameSize.dllPath,"two");
        Reject([&]{store.Acquire(sameSize.id);},nh::NrDllErrorCode::ChangedAsset,"continuity detects changed bytes of identical length");
        auto emptySource = root / L"empty" / L"NVNGX_DLSSNR.DLL"; Put(emptySource, "");
        auto empty = ImportProfile(store,emptySource,"empty is not an admission policy");
        Trace("Acquire empty arbitrary provider");
        Check(empty.bytes == 0 && store.Acquire(empty.id).profile.id == empty.id, "filename matching is case insensitive and no reference size required");
        auto extensible = ImportProfile(store,source,"future fields");
        auto metadataPath = extensible.dllPath.parent_path() / L"profile.json";
        auto metadata = Get(metadataPath); metadata.insert(1,"\"future\":{\"author\":\"retained\"},"); Put(metadataPath,metadata);
        Trace("Rename profile preserving unknown future metadata");
        store.Rename(extensible.id,"new label");
        Check(Get(metadataPath).find("retained") != std::string::npos, "rename preserves unknown future metadata");
        Put(metadataPath,"{broken");
        Reject([&]{store.Acquire(extensible.id);},nh::NrDllErrorCode::CorruptMetadata,"corrupt profile cannot be selected");
        Check(!store.List().issues.empty(),"corrupt profile is visible as a catalog issue");
        auto hardlinked = ImportProfile(store,source,"managed hardlink");
        auto outside = root/L"foreign-preserved.dll"; Put(outside,"foreign");
        fs::remove(hardlinked.dllPath);
        Hardlink(hardlinked.dllPath,outside,"managed hardlink fixture created");
        Reject([&]{store.Remove(hardlinked.id);},nh::NrDllErrorCode::UnsafePath,"managed hardlink deletion refused");
        Check(Get(outside)=="foreign"&&fs::exists(hardlinked.dllPath),"hardlink referent and entry remain intact");
        // Valid source links are copied, never retained as managed dependencies.
        auto linkedSource=root/L"linked-source"/L"nvngx_dlssnr.dll"; fs::create_directories(linkedSource.parent_path());
        Hardlink(linkedSource,source,"source hardlink fixture created");
        auto copiedLink=ImportProfile(store,linkedSource,"user source alias");
        fs::remove(linkedSource);
        Check(Get(copiedLink.dllPath)=="one","source alias imports independent ordinary bytes");
        Put(root/L"profiles"/L"default.json","{broken");
        Reject([&]{store.AcquireDefault();},nh::NrDllErrorCode::CorruptMetadata,"malformed configured default never becomes manual fallback");
        Reject([&]{reader.ClearDefault();},nh::NrDllErrorCode::ReadOnly,"second session cannot clear default");
        Reject([&]{reader.Rename(copiedLink.id,"blocked");},nh::NrDllErrorCode::ReadOnly,"second session cannot rename");
        Reject([&]{reader.SetDefault(copiedLink.id);},nh::NrDllErrorCode::ReadOnly,"second session cannot set default");
        Reject([&]{reader.Remove(copiedLink.id);},nh::NrDllErrorCode::ReadOnly,"second session cannot remove");
        Trace("ClearDefault explicit corrupt-metadata recovery");
        store.ClearDefault();
        Check(!store.AcquireDefault(),"explicit default clear recovers malformed preference without fallback");
        std::cout << "ALL NR DLL PROFILE TESTS PASSED\n";
        return 0;
    } catch (const nh::NrDllError& e) {
        std::cerr << "FAILED NrDllError code=" << CodeName(e.Code()) << " numericCode=" << int(e.Code())
                  << " systemError=" << e.systemError << " operation=" << currentOperation << '\n'; return 1;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << " operation=" << currentOperation << '\n'; return 1;
    }
}
