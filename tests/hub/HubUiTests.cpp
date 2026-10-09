#include "ManualLibrary.h"
#include "imgui.h"
#include "ui/HubViewModel.h"
#include "library/GameCatalog.h"
#include <windows.h>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include "../../OptiScaler/menu/font/Hack_Compressed.h"
namespace nh { void ApplySharedTheme(bool light); }
int RunArtworkTests();
int RunOperationFlowTests();
int RunLibraryRootTests();int RunKeybindCaptureTests();int RunNumericSettingsTests();int RunNumericUiTests();
int RunLoadingUiTests();
int RunScrollLayoutTests();
int RunFocusedUxTests();
int RunGameStatusTests();
int RunFourthPassTests();
int RunFifthPassTests();int RunFrontDoorTests();int RunProfileCacheTests();int RunDiagnosticBundleTests();
int RunHubSelfTests() {
 std::cout<<std::unitbuf;
 // Keep installer backup paths below the existing PowerShell path-length guard,
 // including when verifying an extracted update inside this checkout.
 auto fixture=nh::AppRoot().parent_path()/L"nt";
 SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture.c_str());
 int failed=0;
 failed+=RunOperationFlowTests();
 failed+=RunLoadingUiTests();
 failed+=RunScrollLayoutTests();
 failed+=RunFocusedUxTests();
 failed+=RunLibraryRootTests();failed+=RunKeybindCaptureTests();failed+=RunNumericSettingsTests();failed+=RunNumericUiTests();
 failed+=RunFrontDoorTests();failed+=RunDiagnosticBundleTests();failed+=RunProfileCacheTests();failed+=RunGameStatusTests();failed+=RunFourthPassTests();failed+=RunFifthPassTests();
 auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n'; if(!ok)++failed;};
 {nh::Game game{"fixture","Renamed game","Steam","C:/fixture",{}};game.storeId="730";auto hint=nh::GameRecommendation(game);check(!hint.is_null()&&hint["name"]=="Counter-Strike 2"&&hint["profiles"][0]["proxy"]=="dxgi.dll","NH-CATALOG: exact store ID overrides display title");game.storeId="";game.title="Counter-Strike 2";check(!nh::GameRecommendation(game).is_null(),"NH-CATALOG: exact title matched");game.title="Counter-Strike 2 unofficial mod";check(nh::GameRecommendation(game).is_null(),"NH-CATALOG: fuzzy title does not establish identity");nh::HubModel defaults;check(defaults.proxy==0,"NH-CATALOG: an unknown game retains the default proxy");}
 wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);
 auto feedScan=[&](nh::HubModel& model,const nh::Json& result){
  HANDLE read=nullptr,write=nullptr;CreatePipe(&read,&write,nullptr,1048576);auto data=result.dump();DWORD written=0;WriteFile(write,data.data(),(DWORD)data.size(),&written,nullptr);CloseHandle(write);
  STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};auto exe=std::filesystem::path(system)/L"cmd.exe";auto command=nh::Quote(exe.wstring())+L" /d /c exit 0";
  if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))throw std::runtime_error("Catalog fixture failed");
  CloseHandle(child.hThread);WaitForSingleObject(child.hProcess,10000);model.discovery.text.clear();model.discovery.error.clear();model.discovery.process=child.hProcess;model.discovery.output=read;model.discovery.busy=model.discovery.discovery=true;model.discovery.started=GetTickCount64();
  while(model.discovery.busy)model.Poll();
 };
 {
  nh::HubModel catalog;auto exe=nh::Utf8((std::filesystem::path(system)/L"cmd.exe").wstring());auto root=nh::Utf8(system);
  auto result=nh::Json{{"protocolVersion",2},{"kind","DiscoveryResult"},{"errors",nh::Json::array()},{"games",nh::Json::array({{{"id","auto-game"},{"store","Epic"},{"storeId","epic-fixture"},{"title","Automatic game"},{"installRoot",root},{"executable",exe},{"executableCandidates",nh::Json::array({exe})},{"steamRoot",""},{"iconPath",""},{"reason","Identified"},{"availability","Available"}}})}};
  feedScan(catalog,result);check(catalog.games.size()==1&&catalog.games[0].store=="Epic"&&catalog.games[0].target.suitable,"NH-07-CATALOG: launcher identity and automatic executable accepted");
  catalog.games[0].manualTarget=true;auto manual=catalog.games[0].target.path;auto alternate=nh::Utf8((std::filesystem::path(system)/L"where.exe").wstring());result["games"][0]["executable"]=alternate;result["games"][0]["executableCandidates"]=nh::Json::array({alternate});feedScan(catalog,result);check(catalog.games.size()==1&&catalog.games[0].manualTarget&&catalog.games[0].target.path==manual,"NH-07-CHOICE: scan preserves the user's selected executable");
  auto saved=catalog.games.size();result["games"]=nh::Json::array();feedScan(catalog,result);check(catalog.games.size()==saved,"NH-07-CATALOG: empty scan preserves existing games");
  result["games"]=nh::Json::array({{{"id","bad-game"},{"title",5},{"store","Epic"},{"installRoot",root}}});feedScan(catalog,result);check(catalog.games.size()==saved&&catalog.message.find("could not")!=std::string::npos,"NH-07-CATALOG: malformed DTO rejected before rendering");
 }
 check(nh::InspectExecutable(std::filesystem::path(system)/L"cmd.exe").suitable,"NH-01-MANUAL: x64 PE accepted without execution");
 check(!nh::InspectExecutable(std::filesystem::path(system)/L"kernel32.dll").suitable,"NH-01-MANUAL: non-EXE refused");
 check(!nh::InspectExecutable(L"C:/does-not-exist/other.exe").suitable,"NH-01-MANUAL: missing target refused");
 check(nh::Wide(nh::Utf8(L"Game Ω & $ ; ' .exe"))==L"Game Ω & $ ; ' .exe","NH-01-MANUAL: Unicode and metacharacters are data");
 ImGui::CreateContext(); nh::ApplySharedTheme(false);
 check(ImGui::GetStyle().WindowRounding==16.f && ImGui::GetStyle().FrameRounding==8.f,"NH-01-UI-IDENTITY: accepted sleek dark theme spacing");
 nh::ApplySharedTheme(true);check(ImGui::GetStyle().WindowRounding==16.f,"NH-01-UI-IDENTITY: accepted sleek light theme");ImGui::DestroyContext();
 check(!GetModuleHandleW(L"nvngx_dlssnr.dll") && !GetModuleHandleW(L"OptiScaler.dll"),"NH-01-NO-MODEL: no injected runtime loaded");
 // Exercise actual page rendering with a scan-sized receipt, rather than only theme values.
 // Send assertions to the test log instead of an interactive runtime dialog.
 _set_error_mode(_OUT_TO_STDERR);
 _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;
 io.DisplaySize=ImVec2(1240,820);io.DeltaTime=1.f/60.f;
 io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
 io.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,16.f);nh::ApplySharedTheme(false);nh::HubModel model;model.page=5;
 std::cout<<"RUN NH-UI-DIAGNOSTICS: first open and large Steam receipt"<<std::endl;
 ImGui::NewFrame();nh::RenderHub(model,nullptr,1);ImGui::Render();
 std::cout<<"PASS NH-UI-DIAGNOSTICS: first open"<<std::endl;
 model.lastResult={{"games",nh::Json::array()},{"errors",nh::Json::array()}};
 for(int i=0;i<800;i++)model.lastResult["games"].push_back({{"id",std::to_string(i)},{"title","Steam fixture game"},{"installRoot","C:/fixture/SteamLibrary/steamapps/common/Example game"}});
 std::cout<<"RUN NH-UI-DIAGNOSTICS: large receipt"<<std::endl;
 for(int frame=0;frame<3;frame++){ImGui::NewFrame();nh::RenderHub(model,nullptr,1);ImGui::Render();}
 check(ImGui::GetDrawData()->TotalIdxCount>0,"NH-UI-DIAGNOSTICS: large Steam receipt renders without an assertion");
 model.page=1;for(int i=0;i<800;i++)model.games.push_back({std::to_string(i),"Steam game Ω ","Steam","C:/fixture",{}});
 for(int frame=0;frame<3;frame++){ImGui::NewFrame();nh::RenderHub(model,nullptr,1);ImGui::Render();}
 check(ImGui::GetDrawData()->TotalIdxCount>0,"NH-UI-LIBRARY: populated Steam library renders");
 model.page=3;model.preflight={{"runtime",{{"ready",false},{"minimumVersion","14.44.35207.0"},{"problems",nh::Json::array({"Runtime missing"})}}},{"components",{{"streamline",{{"status","Partial"},{"missing",nh::Json::array({"sl.common.dll"})}}},{"neuralModel",{{"status","Missing"},{"missing",nh::Json::array({"nvngx_dlssnr.dll"})}}}}}};
 for(int frame=0;frame<3;frame++){ImGui::NewFrame();nh::RenderHub(model,nullptr,1);ImGui::Render();}
 check(model.page==3&&ImGui::GetDrawData()->TotalIdxCount>0,"NH-UI-PREFLIGHT: missing runtime and optional files render");
 for(int page=0;page<6;page++){model.page=page;for(bool light:{false,true}){nh::ApplySharedTheme(light);model.light=light;io.DisplaySize={880,600};for(int frame=0;frame<3;frame++){ImGui::NewFrame();nh::RenderHub(model,nullptr,1);ImGui::Render();}check(ImGui::GetDrawData()->TotalIdxCount>0,"NH-UI-RESPONSIVE: compact page in both themes");}}
 ImGui::DestroyContext();
 ImGui::CreateContext();auto& boundaryIo=ImGui::GetIO();boundaryIo.IniFilename=boundaryIo.LogFilename=nullptr;boundaryIo.DisplaySize=ImVec2(1240,820);boundaryIo.DeltaTime=1.f/60.f;
 boundaryIo.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;boundaryIo.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,16.f);
 ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(1200,800));ImGui::Begin("Index boundary fixture");auto draw=ImGui::GetWindowDrawList();draw->PushClipRect(ImVec2(0,0),ImVec2(1240,820),false);auto text=std::string(1024,'R');ImGui::CalcTextSize(text.c_str());for(int i=0;i<16370;i++)draw->AddRectFilled(ImVec2(10,10),ImVec2(11,11),IM_COL32_WHITE);draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),ImVec2(70,70),IM_COL32_WHITE,text.c_str(),nullptr,300.f);bool crossed=draw->VtxBuffer.Size>65536;draw->PopClipRect();ImGui::End();ImGui::Render();
 check(crossed,"NH-UI-INDEX: wrapped text crosses the old 16-bit boundary safely");ImGui::DestroyContext();
 // A child may have exited while more than one per-frame budget remains in its pipe.
 // Closing the pipe at that point used to truncate a perfectly valid receipt.
 {
  HANDLE read=nullptr,write=nullptr;CreatePipe(&read,&write,nullptr,1048576);
  const auto data=nh::Json({{"games",nh::Json::array()},{"errors",nh::Json::array()},{"padding",std::string(100000,'Q')}}).dump();DWORD written=0;
  bool pipeReady=read&&write&&WriteFile(write,data.data(),(DWORD)data.size(),&written,nullptr)&&written==data.size();if(write)CloseHandle(write);
  STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};auto cmd=std::filesystem::path(system)/L"cmd.exe";auto arguments=nh::Quote(cmd.wstring())+L" /d /c exit 0";
  bool child=CreateProcessW(cmd.c_str(),arguments.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE;
  if(child){CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,10000);nh::ProcessJob job;job.process=process.hProcess;job.output=read;job.busy=job.discovery=true;job.started=GetTickCount64();nh::Json result;
   for(int i=0;i<8&&job.busy;i++)job.Poll(result);
   check(pipeReady&&!job.busy&&result.value("padding",std::string())==std::string(100000,'Q'),"NH-IPC-DRAIN: exited discovery child drains every bounded output byte");
  }else{if(read)CloseHandle(read);check(false,"NH-IPC-DRAIN: fixture process could not start");}
 }
 failed+=RunArtworkTests();return failed?1:0;
}
int RunHubScanTest(){
 wchar_t fixture[32768];if(!GetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture,32768)){std::cerr<<"Scan test requires an explicit fixture user-data root\n";return 1;}
 nh::HubModel model;model.Load();model.Scan();auto start=GetTickCount64();
 while((model.discovery.busy||model.readiness.busy)&&GetTickCount64()-start<55000){model.Poll();Sleep(10);}
 bool scan=model.lastResult.contains("games")&&!model.discovery.busy;
 bool preflight=model.preflight.is_object()&&model.preflight.value("status","")=="PreflightChecked"&&!model.readiness.busy;
 std::cout<<(scan?"PASS ":"FAIL ")<<"NH-LIVE-SCAN: real read-only unified helper returned "<<(scan?model.lastResult["games"].size():0)<<" entries\n";
 std::cout<<(preflight?"PASS ":"FAIL ")<<"NH-LIVE-PREFLIGHT: startup owner check completed\n";
 bool saved=false;if(scan){std::ifstream receipt(model.discovery.result);if(receipt){try{saved=nh::Json::parse(receipt)==model.lastResult;}catch(...){}}}
 std::cout<<(saved?"PASS ":"FAIL ")<<"NH-LIVE-RECEIPT: full library result saved to local diagnostics\n";
 if(!scan||!preflight||!saved)std::cout<<model.message<<"\n"<<model.preflightError<<"\n";
 return scan&&preflight&&saved?0:1;
}
