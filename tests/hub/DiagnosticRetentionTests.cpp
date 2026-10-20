#include "install/DiagnosticRetention.h"
#include "storage/UserDataSession.h"
#include "ui/ProfileCache.h"
#include "ManualLibrary.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <stdexcept>
#include <utility>

// Native retention regression fixture. Link the real OperationController,
// ProfileCache, ManualLibrary and localization implementations; no root stubs.
// The executor owns the fixture root and prepares root/junction ->
// root/junction-target before invocation. Do not point this at user AppData.
namespace fs = std::filesystem;
using Json = nh::Json;
static int checks = 0, failed = 0;
static void Setup(bool ok, const char* label) {
    if (!ok) throw std::runtime_error(std::string("SETUP: ") + label);
}
static void Check(bool ok, const char* label) {
    ++checks; failed += !ok;
    std::cout << (ok ? "PASS " : "FAIL ") << label << std::endl;
}
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    Handle(const Handle&) = delete;
    ~Handle() { Reset(); }
    void Reset() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
};
struct Environment {
    std::wstring prior; bool present = false;
    explicit Environment(const fs::path& root) {
        wchar_t buffer[32768]{};
        const auto n = GetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT", buffer, 32768);
        Setup(n < 32768, "existing fixture environment length");
        present = n != 0; prior.assign(buffer, n);
        Setup(SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT", root.c_str()), "set fixture environment");
        Setup(nh::UserRoot() == root, "actual UserRoot uses isolated fixture");
    }
    ~Environment() { SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT", present ? prior.c_str() : nullptr); }
};
static std::wstring Id(unsigned value) {
    wchar_t name[48]{}; swprintf_s(name, L"{00000000-0000-0000-0000-%012u}", value); return name;
}
static void Write(const fs::path& path, const std::string& bytes, bool old = false) {
    std::ofstream file(path, std::ios::binary); file << bytes; file.close();
    Setup(bool(file), "write fixture file");
    if (old) fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(24 * 40));
}
static std::string Read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    Setup(bool(file), "read fixture file");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void Group(const fs::path& dir, unsigned id, const char* kind, const char* status,
                  const fs::path& target = {}, bool old = false) {
    const std::string planId(32, char('a' + id % 6));
    Json request{{"protocolVersion", 1}, {"kind", kind}};
    if (std::string(kind) == "Inspect" || std::string(kind) == "LaunchPreflight")
        request["gameExecutable"] = nh::Utf8((dir.parent_path()/L"Game.exe").wstring());
    if (std::string(kind) == "Execute") {
        request["planId"] = planId; request["planFingerprint"] = std::string(64, 'a');
    } else if (std::string(kind) == "Plan") {
        request["operation"] = "Install"; request["gameExecutable"] = nh::Utf8((dir.parent_path()/L"Game.exe").wstring());
        request["packageId"] = "flagship-approved"; request["proxyName"] = "dxgi.dll";
    }
    Json result{{"protocolVersion", 1}, {"status", status}};
    if (std::string(kind) == "Inspect")
        result["inspection"] = {{"target", {{"executable", request["gameExecutable"]},
            {"directory", nh::Utf8(dir.parent_path().wstring())}, {"bitness", 64}}},
            {"state", nullptr}, {"settings", Json::array()}};
    if (std::string(kind) == "Plan" || std::string(kind) == "Execute") result["planId"] = planId;
    if (std::string(kind) == "Plan") {
        result["request"] = request; result["planFingerprint"] = std::string(64, 'a');
        result["target"] = {{"executable", request["gameExecutable"]}};
    }
    if (!target.empty()) {
        result["inspection"] = {{"target", {{"executable", nh::Utf8(target.wstring())}, {"title", "Retained game"}}},
            {"state", {{"kind", "neurotic-game-install"}, {"schemaVersion", 3}, {"status", "Installed"}}}};
    }
    Write(dir/(Id(id)+L".request.json"), request.dump(), old);
    Write(dir/(Id(id)+L".result.json"), result.dump(), old);
    Write(dir/(Id(id)+L".log"), "fixture completed helper log\n", old);
}
static bool Present(const fs::path& dir, unsigned id) {
    return fs::exists(dir/(Id(id)+L".request.json")) && fs::exists(dir/(Id(id)+L".result.json")) &&
           fs::exists(dir/(Id(id)+L".log"));
}
static bool Gone(const fs::path& dir, unsigned id) {
    return !fs::exists(dir/(Id(id)+L".request.json")) && !fs::exists(dir/(Id(id)+L".result.json")) &&
           !fs::exists(dir/(Id(id)+L".log"));
}
static bool Known(const fs::path& root, const fs::path& target) {
    const auto path = root/L"known-installations.json";
    if (!fs::exists(path)) return false;
    const auto document = Json::parse(Read(path));
    for (const auto& row : document.at("targets"))
        if (row.at("executable") == nh::Utf8(target.wstring())) return true;
    return false; // Never use EnumerateKnownInstallations: it can hide failure via receipt fallback.
}
template<class Body> static void Case(const fs::path& base, const wchar_t* name, Body body) {
    const auto root = base/name;
    Setup(!fs::exists(root), "case root must be fresh");
    fs::create_directories(root/L"requests");
    Environment environment(root);
    nh::UserDataSession owner(root);
    Setup(owner.Owns(), "actual same-root session owner");
    body(root, root/L"requests");
}
// This child only holds an inherited transport log handle. It never loads a
// provider, runs a game, or invokes a product worker. Its lifetime is bounded.
struct Child {
    Handle process, release;
    Child(const fs::path& log) {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        Handle writer(CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, OPEN_EXISTING, 0, nullptr));
        Setup(writer.value != INVALID_HANDLE_VALUE, "open inherited active log");
        release.value = CreateEventW(&security, TRUE, FALSE, nullptr);
        Setup(release.value != nullptr, "create child release event");
        wchar_t self[32768]{}; const auto n = GetModuleFileNameW(nullptr, self, 32768);
        Setup(n && n < 32768, "fixture executable path");
        auto command = nh::Quote(self) + L" --hold-log " + std::to_wstring(reinterpret_cast<uintptr_t>(writer.value)) +
            L" " + std::to_wstring(reinterpret_cast<uintptr_t>(release.value));
        STARTUPINFOW start{sizeof(start)}; PROCESS_INFORMATION info{};
        Setup(CreateProcessW(self, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, log.parent_path().c_str(), &start, &info), "start bounded fixture log holder");
        process.value = info.hProcess; CloseHandle(info.hThread);
        writer.Reset(); // Only the child now holds this log open.
    }
    void Drain() {
        if (!process.value) return;
        Setup(SetEvent(release.value), "release fixture child");
        Setup(WaitForSingleObject(process.value, 30000) == WAIT_OBJECT_0, "fixture child actual exit");
        DWORD code = 1; Setup(GetExitCodeProcess(process.value, &code) && code == 0, "fixture child clean exit");
        process.Reset();
    }
    ~Child() {
        if (process.value) {
            SetEvent(release.value);
            if (WaitForSingleObject(process.value, 30000) != WAIT_OBJECT_0) {
                TerminateProcess(process.value, 3); WaitForSingleObject(process.value, 5000);
            }
        }
    }
};
static fs::path Stored(const fs::path& root,char ch){return root/(std::wstring(32,wchar_t(ch))+L".plan.json");}
static void StoredPlan(const fs::path& root,char ch){const auto exe=nh::Utf8((root/L"Game.exe").wstring());Json request{{"protocolVersion",1},{"kind","Plan"},{"operation","Install"},{"gameExecutable",exe},{"packageId","flagship-approved"},{"proxyName","dxgi.dll"}};Write(Stored(root,ch),Json{{"status","Planned"},{"planId",std::string(32,ch)},{"planFingerprint",std::string(64,'a')},{"request",request},{"target",{{"executable",exe}}},{"changes",Json::array()}}.dump());}
static void RunMalformedNeighborCases(const fs::path& base) {
 Case(base,L"bad-protocol",[&](auto root,auto dir){Group(dir,1,"Inspect","Inspected",{},true);Write(dir/(Id(1)+L".request.json"),Json{{"protocolVersion","foreign-string"},{"kind","Inspect"},{"gameExecutable",nh::Utf8((root/L"Game.exe").wstring())}}.dump(),true);Group(dir,2,"Inspect","Inspected",{},true);nh::DiagnosticRetention::Prune(dir,{0,0});Check(Present(dir,1),"wrong-typed request protocol stays preserved");Check(Gone(dir,2),"wrong-typed neighbor cannot starve unrelated eligible transport");});
 Case(base,L"bad-discovery",[&](auto root,auto dir){Write(dir/(Id(1)+L".discovery.result.json"),Json{{"protocolVersion","foreign-string"},{"kind","DiscoveryResult"},{"games",Json::array()},{"errors",Json::array()}}.dump(),true);Write(dir/(Id(1)+L".discovery.log"),"foreign shape",true);Group(dir,2,"Inspect","Inspected",{},true);nh::DiagnosticRetention::Prune(dir,{0,0});Check(fs::exists(dir/(Id(1)+L".discovery.result.json"))&&fs::exists(dir/(Id(1)+L".discovery.log")),"wrong-typed discovery metadata stays preserved");Check(Gone(dir,2),"malformed discovery cannot abort unrelated group cleanup");});
 Case(base,L"bad-plan-status",[&](auto root,auto dir){StoredPlan(root,'a');auto bad=Json::parse(Read(Stored(root,'a')));bad["status"]=7;Write(Stored(root,'a'),bad.dump());StoredPlan(root,'b');nh::DiagnosticRetention::Prune(dir,{0,0});Check(fs::exists(Stored(root,'a')),"foreign wrong-typed root plan status stays preserved");Check(!fs::exists(Stored(root,'b')),"foreign root-plan metadata does not starve independent validated unused plan");});
 Case(base,L"mixed-family",[&](auto root,auto dir){Group(dir,1,"Inspect","Inspected",{},true);Write(dir/(Id(1)+L".discovery.log"),"foreign extra family",true);Group(dir,2,"Inspect","Inspected",{},true);nh::DiagnosticRetention::Prune(dir,{0,0});Check(Present(dir,1)&&fs::exists(dir/(Id(1)+L".discovery.log")),"mixed helper families preserve whole group");Check(Gone(dir,2),"mixed-family ambiguity does not block unrelated completed group");});
 for(int suspend=0;suspend<2;++suspend){
  const auto root=base/(suspend?L"suspend-namespace":L"revoke-namespace"),moved=base/(suspend?L"suspend-moved":L"revoke-moved");
  Setup(root.parent_path()==base&&moved.parent_path()==base&&!fs::exists(root)&&!fs::exists(moved),"both directory rename targets are fresh children of owned fixture base");fs::create_directories(root/L"requests");Environment env(root);nh::UserDataSession owner(root);Setup(owner.Owns(),"actual namespace owner");
  for(unsigned i=0;i<30;++i)Group(root/L"requests",i,"Inspect","Inspected",{},true);
  nh::DiagnosticRetention::Maintain(root);nh::DiagnosticRetention::Maintain(root); // journal, then incomplete64-entry pass
  const bool renamedWhileOwned=MoveFileExW(root.c_str(),moved.c_str(),0)!=FALSE;
  Check(!renamedWhileOwned,"bounded active reference pass pins its actual namespace across polls");
  if(renamedWhileOwned)Setup(MoveFileExW(moved.c_str(),root.c_str(),0)!=FALSE,"restore unexpected early fixture rename");
  if(suspend)nh::DiagnosticRetention::Suspend(root);else owner.Release();
  const bool renamedAfterRelease=MoveFileExW(root.c_str(),moved.c_str(),0)!=FALSE;
  Check(renamedAfterRelease,suspend?"maintenance Suspend immediately releases namespace and enumeration handles":"actual root authority loss releases all namespace handles before returning");
  owner.Release();nh::DiagnosticRetention::Suspend(root);
 }
}
int wmain(int argc, wchar_t** argv) try {
    if (argc == 4 && std::wstring(argv[1]) == L"--hold-log") {
        Handle log(reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10)));
        Handle release(reinterpret_cast<HANDLE>(_wcstoui64(argv[3], nullptr, 10)));
        return WaitForSingleObject(release.value, 30000) == WAIT_OBJECT_0 ? 0 : 2;
    }
    Setup(argc == 2, "usage: fixture.exe <owned fixture root with prepared junction>");
    const fs::path base = argv[1];
    Setup(base.is_absolute() && base.lexically_normal() == base && !base.wstring().starts_with(L"\\\\"), "ordinary absolute fixture root");
    fs::create_directories(base);
    Environment outerEnvironment(base); // Even setup/reparse checks stay out of real AppData.
    Case(base, L"recent-terminal", [&](auto root, auto dir) {
        Group(dir, 1, "Inspect", "Inspected"); Group(dir, 2, "Plan", "Planned");
        Group(dir, 3, "Execute", "Succeeded"); Group(dir, 4, "Execute", "FailedWithoutChanges");
        Group(dir, 5, "Execute", "Cancelled"); Group(dir, 6, "Preflight", "PreflightChecked");
        nh::DiagnosticRetention::Prune(dir, {0, 0});
        for (unsigned id=1; id<=6; ++id) Check(Gone(dir,id), "fresh consumed recognized protocol1 group retired without day/size backlog");
        nh::DiagnosticRetention::Prune(dir, {0, 0});
        Check(Gone(dir,1), "repeated quiescent pruning is idempotent");
    });
    Case(base, L"old-terminal", [&](auto root, auto dir) {
        Group(dir, 1, "Plan", "Planned", {}, true); Group(dir, 2, "Execute", "Succeeded", {}, true);
        Group(dir, 3, "Execute", "FailedWithoutChanges", {}, true); Group(dir, 4, "Execute", "Cancelled", {}, true);
        nh::DiagnosticRetention::Prune(dir, {0, 0});
        for(unsigned id=1; id<=4; ++id) Check(Gone(dir,id), "recognized consumed legacy mutation/failed/cancelled transport is not permanent history");
    });
    Case(base, L"discovery", [&](auto root, auto dir) {
        const auto receipt=dir/(Id(1)+L".discovery.result.json"), log=dir/(Id(1)+L".discovery.log");
        Write(receipt, Json{{"protocolVersion",2},{"kind","DiscoveryResult"},{"games",Json::array()},{"errors",Json::array()}}.dump());
        Write(log,"completed discovery"); nh::DiagnosticRetention::Prune(dir,{0,0});
        Check(!fs::exists(receipt)&&!fs::exists(log), "fresh complete protocol2 discovery pair retired");
    });
    Case(base, L"incomplete-quiescent", [&](auto root, auto dir) {
        Group(dir,1,"Inspect","Inspected",{},true);
        Write(dir/(Id(1)+L".result.json"),"not valid json",true);
        Group(dir,2,"Inspect","Inspected",{},true);
        Setup(DeleteFileW((dir/(Id(2)+L".result.json")).c_str()),"remove fixture incomplete result");
        Group(dir,3,"Inspect","Inspected",{},true);
        Setup(DeleteFileW((dir/(Id(3)+L".log")).c_str()),"remove fixture ambiguous log");
        nh::DiagnosticRetention::Prune(dir,{0,0});
        Check(Gone(dir,1),"quiescent known request plus log retires malformed response transport");
        Check(Gone(dir,2),"quiescent known request plus log retires missing response transport");
        Check(fs::exists(dir/(Id(3)+L".request.json"))&&fs::exists(dir/(Id(3)+L".result.json")),
            "legacy group lacking log proof stays narrowly protected");
    });
    Case(base, L"active-child", [&](auto root, auto dir) {
        Group(dir,1,"Inspect","Inspected",{},true);
        Child child(dir/(Id(1)+L".log"));
        Check(WaitForSingleObject(child.process.value,0)==WAIT_TIMEOUT,"fixture child active with parent log handle closed");
        nh::DiagnosticRetention::Prune(dir,{0,0});
        Check(Present(dir,1),"child-only inherited active log protects all group members");
        child.Drain(); nh::DiagnosticRetention::Prune(dir,{0,0});
        Check(Gone(dir,1),"eligible group retires after actual child exit");
    });
    Case(base, L"unknown-and-hardlink", [&](auto root, auto dir) {
        Group(dir,1,"Inspect","Inspected",{},true); Write(dir/(Id(1)+L".extra"),"foreign group member");
        Write(dir/L"notes.txt","user document"); Write(dir/L"export.zip","user export");
        Group(dir,2,"Inspect","Inspected",{},true); const auto outside=root/L"outside.txt"; Write(outside,"outside evidence",true);
        Setup(DeleteFileW((dir/(Id(2)+L".log")).c_str()),"replace fixture log with hardlink");
        Setup(CreateHardLinkW((dir/(Id(2)+L".log")).c_str(),outside.c_str(),nullptr),"create hardlink fixture");
        nh::DiagnosticRetention::Prune(dir,{0,0});
        Check(Present(dir,1)&&fs::exists(dir/(Id(1)+L".extra")),"unknown group member prevents partial group deletion");
        Check(Read(dir/L"notes.txt")=="user document"&&Read(dir/L"export.zip")=="user export","foreign documents and export retained");
        Check(Present(dir,2)&&Read(outside)=="outside evidence","hardlinked group and outside identity retained");
    });
    Case(base, L"known-migration", [&](auto root, auto dir) {
        const auto target=root/L"Game A"/L"Game.exe";
        Group(dir,1,"Execute","Succeeded",target,true);
        nh::DiagnosticRetention::Prune(dir,{0,0});
        Check(Gone(dir,1),"terminal receipt retired after durable known-install migration");
        Check(Known(root,target),"known install survives in actual registry without request fallback");
    });
    Case(base, L"known-save-failure", [&](auto root, auto dir) {
        const auto seed=root/L"Seed"/L"Game.exe", target=root/L"Game A"/L"Game.exe";
        Setup(nh::RememberKnownInstallation(nh::Utf8(seed.wstring()),"Seed"),"actual known-install registry seed");
        const auto known=root/L"known-installations.json"; const auto before=Read(known);
        Group(dir,1,"Execute","Succeeded",target,true); Group(dir,2,"Execute","FailedWithoutChanges",{},true);
        Handle blocked(CreateFileW(known.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
        Setup(blocked.value!=INVALID_HANDLE_VALUE,"pin registry against atomic replacement");
        Setup(!nh::RememberKnownInstallation(nh::Utf8(target.wstring()),"A"),"actual known-install save failure injected");
        nh::DiagnosticRetention::Prune(dir,{0,0});
        Check(Present(dir,1),"failed knowledge save preserves the specific only-source receipt group");
        Check(Gone(dir,2),"one migration failure does not retain unrelated completed transport");
        Check(Read(known)==before,"failed migration leaves existing registry bytes unchanged");
        blocked.Reset(); nh::DiagnosticRetention::Prune(dir,{0,0});
        Check(Gone(dir,1)&&Known(root,target)&&Known(root,seed),"retry commits knowledge and retires protected source without losing seed");
    });
    Case(base, L"session-owner", [&](auto root, auto dir) {
        bool secondaryOwned=true;
        // A different thread matters: Windows mutex ownership is recursive on
        // the owning thread, so same-thread construction is a false test.
        std::exception_ptr secondaryError;
        std::thread secondary([&]{try {nh::UserDataSession second(root);secondaryOwned=second.Owns();}
            catch(...) {secondaryError=std::current_exception();}}); secondary.join();
        if(secondaryError)std::rethrow_exception(secondaryError);
        Check(!secondaryOwned,"actual cooperative same-root secondary cannot own user data");
        // Prune has no owner argument: do not invent an API or claim this proves
        // its caller's owner gate. Only the real owner invokes Prune above.
    });
    RunMalformedNeighborCases(base);
    const auto target=base/L"junction-target", junction=base/L"junction";
    nh::UserDataSession outerOwner(base);Setup(outerOwner.Owns(),"actual owner for reparse boundary checks");
    fs::create_directories(target);
    const auto attributes=GetFileAttributesW(junction.c_str());
    Setup(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"executor must prepare junction fixture");
    Group(target,1,"Inspect","Inspected",{},true); nh::DiagnosticRetention::Prune(junction,{0,0});
    Check(Present(target,1),"reparse cleanup root refused");
    fs::create_directories(target/L"nested");Group(target/L"nested",1,"Inspect","Inspected",{},true);
    outerOwner.Release();
    Environment linkedEnvironment(junction);
    nh::UserDataSession linkedOwner(junction);Setup(linkedOwner.Owns(),"actual owner for linked parent spelling");
    nh::DiagnosticRetention::Prune(junction/L"nested",{0,0});
    Check(Present(target/L"nested",1),"reparse ancestor refused");
    std::cout<<"RESULT checks="<<checks<<" failed="<<failed<<" REAL_PRUNE=true REAL_SESSION=true REAL_KNOWN_INSTALL=true NO_PRODUCT_WORKER=true\n";
    return failed?1:0;
} catch(const std::exception& e) { std::cerr<<e.what()<<std::endl; return 2; }
