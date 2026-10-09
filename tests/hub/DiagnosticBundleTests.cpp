#include "ui/DiagnosticBundle.h"
#include "ui/HubViewModel.h"
#include "ManualLibrary.h"
#include <fstream>
#include <iostream>

int RunDiagnosticBundleTests(){
 int failed=0;
 auto check=[&](bool ok,const char* message){std::cout<<(ok?"PASS ":"FAIL ")<<message<<'\n';if(!ok)++failed;};
 auto privateReport=nh::Json{{"state",{{"status","installed-verified"}}},{"antiCheat",{{"scan_status","complete"},{"game_state","possible"},{"safety_verified",false},{"findings",nh::Json::array({{{"provider","battleye"},{"state","possible"},{"evidence",nh::Json::array({{{"path","BattlEye/BEService.exe"},{"pe",true}}})}}})},{"scan",{{"root","C:/Users/private/Game"}}},{"identity",{{"manifest","C:/Users/private/manifest"}}},{"target","C:/Users/private/Game/game.exe"}}}};
 auto redacted=nh::RedactAntiCheatForExport(privateReport);check(redacted["antiCheat"]["findings"][0]["evidence"][0]["path"]=="BattlEye/BEService.exe"&&redacted.dump().find("C:/Users/private")==std::string::npos&&redacted["state"]==privateReport["state"],"NH-ZIP: anti-cheat export retains relative evidence without collector root or manifest paths");
 // UserRoot is redirected by the Hub self-test runner. No game/provider is run.
 auto fixture=nh::UserRoot()/L"diagnostic-bundle-native";
 if(std::filesystem::exists(fixture)){std::cout<<"FAIL NH-ZIP: stale fixture requires inspection\n";return 1;}
 std::filesystem::create_directories(fixture);
 auto game=fixture/L"Game & O'Brien \u03a9",output=fixture/L"Export \u03a9";
 std::filesystem::create_directory(game);std::filesystem::create_directory(output);
 auto executable=game/L"fixture game.exe";
 // Re-selecting the origin revalidates its PE architecture. Copy an actual
 // system PE64 executable as inert fixture data; this test never runs it.
 wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);
 if(!CopyFileW((std::filesystem::path(system)/L"cmd.exe").c_str(),executable.c_str(),FALSE))throw std::runtime_error("Diagnostic executable fixture copy failed");
 std::ofstream(game/L"OptiScaler.log",std::ios::binary)<<std::string(700000,'A')<<"FULL-LOG-END";
 std::ofstream(game/L"OptiScaler.ini")<<"[NR]\nEnabled=false\n";
 std::ofstream(output/L"keep.txt")<<"unrelated output";
 auto result=nh::ExportDiagnosticBundle(nh::Utf8(executable.wstring()),output,{{"state",{{"status","installed-verified"}}},{"runtime","NotAttempted"}});
 if(!result.success)std::cout<<result.status<<'\n';
 check(result.success&&!result.path.empty()&&std::filesystem::is_regular_file(std::filesystem::path(nh::Wide(result.path))),"NH-ZIP: native hidden worker exports full diagnostic ZIP with Unicode and shell metacharacter paths");
 // Exercise the actual export worker with a maximum-size encoded ObjectRules
 // value and its second config representation, not only a budget constant.
 auto large=nh::Json{{"settings",nh::Json::array({{{"type","objectrules"},{"value",std::string(4*1024*1024,'a')}}})},{"config",{{"ObjectRules",std::string(4*1024*1024,'b')}}}};
 auto largeResult=nh::ExportDiagnosticBundle(nh::Utf8(executable.wstring()),output,large);
 check(largeResult.success,"NH-ZIP: full-size ObjectRules snapshot crosses the native and PowerShell export boundary");
 bool scratch=false;for(auto& item:std::filesystem::directory_iterator(output))if(item.is_directory())scratch=true;
 check(!scratch,"NH-ZIP: successful native export removes its own snapshot/receipt working directory");
 auto refused=nh::ExportDiagnosticBundle(nh::Utf8(executable.wstring()),output/L"keep.txt",nh::Json::object());
 std::ifstream kept(output/L"keep.txt");std::string text;std::getline(kept,text);kept.close();
 check(!refused.success&&refused.path.empty()&&text=="unrelated output","NH-ZIP: invalid destination refuses and preserves unrelated output");
 auto invalid=nh::ExportDiagnosticBundle(nh::Utf8((game/L".."/L"escape.exe").wstring()),output,nh::Json::object());
 check(!invalid.success&&invalid.path.empty(),"NH-ZIP: noncanonical selected target refuses before collection");
 nh::HubModel model;nh::Game origin{"origin","Origin","Manual",nh::Utf8(game.wstring()),{nh::Utf8(executable.wstring()),"fixture",true,{}}};nh::Game other{"other","Other","Manual",nh::Utf8(output.wstring()),{}};model.games={origin,other};model.selected=0;
 model.ExportSelectedDiagnostics(output);model.Select(1);auto deadline=GetTickCount64()+30000;while(model.DiagnosticExportRunning()&&GetTickCount64()<deadline){model.Poll();Sleep(5);}
 check(!model.DiagnosticExportRunning()&&model.bundlePath.empty(),"NH-ZIP: async completion never appears under another selected game");model.Select(0);
 check(!model.bundlePath.empty()&&std::filesystem::is_regular_file(nh::Wide(model.bundlePath)),"NH-ZIP: originating game retains export result independently of inspection cache");
 while(model.installer.busy&&GetTickCount64()<deadline){model.Poll();Sleep(5);}
 // Remove only the names this successful fixture created; retain failed data
 // for inspection. No recursive traversal or link following is needed.
 if(!failed){for(const auto& path:{result.path,largeResult.path,model.bundlePath})if(!path.empty())DeleteFileW(nh::Wide(path).c_str());DeleteFileW((output/L"keep.txt").c_str());DeleteFileW(executable.c_str());DeleteFileW((game/L"OptiScaler.ini").c_str());DeleteFileW((game/L"OptiScaler.log").c_str());RemoveDirectoryW(game.c_str());RemoveDirectoryW(output.c_str());RemoveDirectoryW(fixture.c_str());}
 return failed;
}
