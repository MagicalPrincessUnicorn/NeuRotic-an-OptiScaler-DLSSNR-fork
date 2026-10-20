#include <windows.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#if __has_include("anything/AnythingController.h")
#include "anything/AnythingController.h"
#include "anything/AnythingPreferences.h"
#include "../../apps/NeuRoticWindowWorker/control/SnapshotLocation.h"
using namespace nh;
static int failed=0;
static void Check(bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;failed+=!ok;}
template<class Predicate> static bool Wait(Predicate predicate,int milliseconds=6000){auto end=GetTickCount64()+milliseconds;while(GetTickCount64()<end){if(predicate())return true;Sleep(10);}return predicate();}
template<class Snapshot> static bool KeptReason(const Snapshot& snapshot){if constexpr(requires { snapshot.lastError; })return snapshot.lastError=="Source identity changed";else return false;}
template<class Controller> static bool PreparedFolder(Controller& c,const std::filesystem::path& expected){if constexpr(requires { c.ModelFolder();c.EnsureModelFolder(); })return c.ModelFolder()==expected&&c.EnsureModelFolder()&&std::filesystem::is_regular_file(expected/L"README.txt");else return false;}
int wmain(int argc,wchar_t** argv){
 if(argc<3)return 2;std::filesystem::path fixture=argv[1],root=argv[2];std::filesystem::create_directories(root);
 // Reuse one fixture root. Remove only this test's known preference leaves.
 if(root.filename()!=L"fixtures"||std::filesystem::weakly_canonical(root.parent_path())!=std::filesystem::weakly_canonical(fixture.parent_path()))return 2;
 {
  auto data=root/L"deposit",folder=data/L"Place NVNGX DLSS NR File Here",file=folder/L"nvngx_dlssnr.dll";
  std::filesystem::remove(file);std::filesystem::remove(data/L"anything.json");
  AnythingController c(fixture,data);Check(PreparedFolder(c,folder),"Model folder preparation creates the named deposit with instructions");
  c.Connect();Wait([&]{return c.Snapshot().connected&&!c.Snapshot().busy;});
  std::filesystem::create_directories(folder);std::ofstream(file)<<"fixture model";
  Check(Wait([&]{return c.Snapshot().ready&&!c.Snapshot().busy;}),"Direct deposit automatically verifies and enables Start without another chooser");
  const auto verified=c.Snapshot().modelRevision;
  std::filesystem::remove(file);
  Check(Wait([&]{return !c.Snapshot().ready;}),"Removed deposit revokes verified readiness");
  Check(!c.Start({{"mode","countdown"}}),"Removed deposit cannot start the stale worker model");
  std::ofstream(file)<<"reject";
  Check(Wait([&]{return !c.Snapshot().busy&&c.Snapshot().lastError=="Unsupported model";}),"Replacement is sent through worker verification and rejected");
  Check(!c.Snapshot().ready&&!c.Start({{"mode","countdown"}}),"Rejected replacement cannot retain previous readiness");
  const auto attempts=c.Snapshot().status.value("modelChecks",0);Sleep(1200);Check(c.Snapshot().modelRevision==verified&&c.Snapshot().status.value("modelChecks",0)==attempts,"Unchanged rejected deposit is not repeatedly registered");
  std::ofstream(file)<<"delayed model";
  Check(Wait([&]{return c.Snapshot().busy&&c.Snapshot().message=="Fixture verification in flight";}),"Changed deposit begins background verification");
  std::ofstream(file)<<"reject";
  Check(Wait([&]{return !c.Snapshot().busy&&c.Snapshot().lastError=="Unsupported model";}),"Deposit replaced during acknowledgment is verified again");
  Check(!c.Snapshot().ready&&!c.Start({{"mode","countdown"}}),"In-flight replacement cannot inherit the earlier model acknowledgment");
  std::ofstream(file)<<"accepted replacement";
  Check(Wait([&]{return c.Snapshot().ready&&!c.Snapshot().busy&&c.Snapshot().modelRevision>verified;}),"New replacement is reverified after a rejection");
  std::filesystem::remove(file);
 }
 {
  auto data=root/L"migration",folder=data/L"Place NVNGX DLSS NR File Here",source=data/L"Models/old-hash/nvngx_dlssnr.dll";
  std::filesystem::create_directories(source.parent_path());std::ofstream(source)<<"legacy bytes";
  std::filesystem::remove(folder/L"nvngx_dlssnr.dll");std::ofstream(data/L"anything.json")<<nlohmann::json{{"modelPath",Utf8(source.wstring())}};
  AnythingController c(fixture,data);c.Connect();
  Check(Wait([&]{return c.Snapshot().ready&&!c.Snapshot().busy;}),"Saved legacy model is automatically migrated and reverified");
  Check(std::filesystem::exists(folder/L"nvngx_dlssnr.dll")&&std::filesystem::file_size(source)==12,"Legacy migration preserves source bytes and creates one flat deposit");
  Check(c.Snapshot().modelPath==Utf8((folder/L"nvngx_dlssnr.dll").wstring()),"Migrated preference uses the same flat folder as onboarding");
 }
 {
  auto data=root/L"migration",file=data/L"Place NVNGX DLSS NR File Here/nvngx_dlssnr.dll";std::filesystem::remove(file);
  AnythingController c(fixture,data);c.Connect();Wait([&]{return c.Snapshot().connected&&!c.Snapshot().busy;});Sleep(500);
  Check(!c.Snapshot().ready&&!std::filesystem::exists(file),"Removed migrated model does not trigger another deposit from retained legacy bytes");
 }
 {
  auto data=root/L"deposit-conflict",folder=data/L"Place NVNGX DLSS NR File Here",source=data/L"Models/nvngx_dlssnr.dll",file=folder/L"nvngx_dlssnr.dll";
  std::filesystem::create_directories(source.parent_path());std::filesystem::create_directories(folder);std::ofstream(source)<<"legacy bytes";std::ofstream(file)<<"reject";
  std::ofstream(data/L"anything.json")<<nlohmann::json{{"modelPath",Utf8(source.wstring())}};
  AnythingController c(fixture,data);c.Connect();Check(Wait([&]{return c.Snapshot().connected&&!c.Snapshot().busy&&c.Snapshot().lastError=="Unsupported model";}),"Existing deposit is verified before any legacy migration");
  std::ifstream in(file);std::string bytes;in>>bytes;in.close();Check(bytes=="reject"&&!c.Snapshot().ready&&!c.Start({{"mode","countdown"}}),"Rejected destination preserves new user bytes and blocks the old model");
  std::filesystem::remove(file);
 }
 {
  PWSTR local=nullptr;SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local);
  Check(local&&nrw::SnapshotFolderPath()==std::filesystem::path(local)/L"NeuRotic"/L"Snapshots","Shared worker/save and desktop/open snapshot folder is directly under LocalAppData NeuRotic, outside Hub preferences");CoTaskMemFree(local);
  AnythingUiState p;Check(p.resolution==0&&p.comparison==0&&p.profile=="Default"&&p.split==.5f&&p.stripes==6,"New Anything preferences use full resolution, Default, On, centered split and six stripes");
  ApplyAnythingLook(p,2);p.transfer=.42f;p.colour=.73f;p.profile="My natural";p.profiles[p.profile]=AnythingProfile(p);p.countdownSeconds=7;
  auto path=root/L"anything-profiles-test.json";SaveAnythingPreferences(p,path);AnythingUiState restored;LoadAnythingPreferences(restored,path);
  Check(restored.profile==p.profile&&restored.profiles==p.profiles&&restored.look==2&&restored.transfer==.42f&&restored.colour==.73f&&restored.countdownSeconds==7,"Named profile, current strengths and last-loaded name survive disk roundtrip");
  auto invalid=AnythingProfile(p);invalid["detail"]=-1;auto before=AnythingProfile(restored);Check(!ApplyAnythingProfile(restored,invalid)&&AnythingProfile(restored)==before,"Malformed profile cannot partially change current settings");
  invalid["detail"]=1;invalid["quality"]=5;Check(!ApplyAnythingProfile(restored,invalid),"Unsupported quality fails profile admission");
  std::filesystem::remove(path);
  AnythingUiState rates;UpdateAnythingRates(rates,0,1,0,0,true);
  for(int i=1;i<=4;++i)UpdateAnythingRates(rates,i,1,i*60,i*30,true);
  Check(rates.ratesKnown&&!rates.deficit&&rates.incomingFps==60&&rates.processedFps==30,"Transient deficit reports measured rates without warning");
  UpdateAnythingRates(rates,5,1,300,150,true);Check(rates.deficit,"Five-second sustained deficit recommends quality change");
  UpdateAnythingRates(rates,6,1,360,210,true);Check(!rates.deficit,"Recovered processing clears deficit");
  UpdateAnythingRates(rates,7,2,0,0,true);Check(!rates.ratesKnown&&!rates.deficit,"Session rollover cannot reuse old rates");
  UpdateAnythingRates(rates,8,2,60,30,false);Check(!rates.ratesKnown,"Paused source has no invented current rates");
  UpdateAnythingRates(rates,0,3,0,0,true);for(int i=1;i<=6;++i)UpdateAnythingRates(rates,i,3,i*60,i*54,true);Check(!rates.deficit,"Exactly 90 percent output never warns");
  UpdateAnythingRates(rates,0,4,0,0,true);for(int i=1;i<=6;++i)UpdateAnythingRates(rates,i,4,i*4,0,true);Check(!rates.deficit,"Sparse capture arrivals do not imply a throughput deficit");
  UpdateAnythingRates(rates,0,5,0,0,true);for(int i=1;i<=6;++i)UpdateAnythingRates(rates,i,5,i*60,0,true);Check(rates.deficit&&rates.processedFps==0,"Zero accepted output with sustained incoming capture warns");
  UpdateAnythingRates(rates,11,5,660,0,true);Check(!rates.ratesKnown&&!rates.deficit,"Long telemetry gap cannot retain stale warning");
 }
 for(auto name:{L"normal",L"locked",L"reject",L"quiet",L"start-reject",L"stall",L"oversize",L"real",L"restart"})for(auto leaf:{L"anything.json",L"anything.pending.json"})std::filesystem::remove(root/name/leaf);
 {
  AnythingController c(fixture.parent_path()/L"LegacyProtocolFixture.exe",root/L"legacy");c.Connect();
  Check(Wait([&]{return c.Snapshot().phase=="Error";}),"Older worker without required integration capabilities is refused");Check(!c.Snapshot().ready&&!c.Start({{"mode","countdown"}}),"Incompatible worker cannot enable rendering");
 }
 {
  AnythingController c(fixture,root/L"normal");Check(!c.Start({{"mode","countdown"}}),"Start remains locked before verified NR file");
  c.Connect();Check(Wait([&]{return c.Snapshot().connected;}),"Background reader receives worker greeting");
  Check(!c.Snapshot().ready,"Worker Locked state cannot unlock selection");
  auto path=root/L"Space & Ω"/L"nvngx_dlssnr.dll";
  Check(c.SelectModel(path,false),"Reference model selection accepted without blocking caller");
  Check(Wait([&]{auto s=c.Snapshot();return !s.busy&&s.ready;}),"Only model acknowledgment unlocks selection");
  Check(c.Snapshot().modelPath==Utf8(path.wstring()),"Unicode model path round trips through JSON and persistence");
  const auto catalogBefore=c.Snapshot().catalogRevision;
  c.RefreshWindows();Check(Wait([&]{return !c.Snapshot().busy&&c.Snapshot().windows.size()==1;}),"Catalog excludes the hosting Hub process");
  Check(c.Snapshot().catalogRevision==catalogBefore+1,"Completed asynchronous catalog response advances the dropdown opening revision");
  if(c.Snapshot().windows.empty())return 1;auto window=c.Snapshot().windows.at(0);
  Check(c.Start({{"mode","selected"},{"window",window},{"guides","off"},{"split",0.5f},{"nrScalePercent",67}}),"Selected-window start accepts full catalog identity");
  Check(Wait([&]{return c.Snapshot().phase=="Running";}),"Started acknowledgment exposes Running separately from completed frames");
  auto s=c.Snapshot();Check(s.status["receivedStart"]["window"]==window&&s.status["receivedStart"]["split"]==0.5f,"Actual pipe command preserves complete identity and comparison");
  Check(s.status["receivedStart"]["nrScalePercent"]==67,"Actual pipe start preserves NR resolution choice");
  Check(!c.SelectModel(path,true),"Active session prevents model replacement");
  Check(c.RefreshWindows(),"Running session allows the window dropdown to refresh for retargeting");Check(Wait([&]{auto s=c.Snapshot();return s.active&&!s.busy;}),"Catalog refresh does not stop the current rendering session");
  for(int i=0;i<500;++i)c.SetComparison(float(i%101)/100);
  Check(Wait([&]{auto v=c.Snapshot();return !v.busy&&v.phase=="Running"&&v.status.value("comparisonSplit",-1.f)==.95f;})&&c.Snapshot().active,"Comparison pressure alone keeps Running and never issues Stop");
  Check(c.SetComparison(.5f,6),"Live stripe request accepted with advertised worker capability");
  Check(Wait([&]{return c.Snapshot().status.value("comparisonStripes",0)==6;}),"Stripe count survives serialized pipe transport");
  Check(!c.SetComparison(.5f,33)&&!c.SetComparison(.5f,1),"Invalid stripe counts never queue");
  Check(c.TakeSnapshot(),"Active snapshot request reaches worker");
  Check(Wait([&]{auto v=c.Snapshot();return !v.busy&&v.status.value("snapshot",nlohmann::json::object()).value("state",std::string{})=="pending";}),"Snapshot acknowledgment releases supervisor without claiming saved output");
  Check(!c.TakeSnapshot(),"Pending snapshot cannot be queued twice");
  auto processing=nlohmann::json{{"nrScalePercent",67},{"modelStyle",2},{"transferStrength",.42f},{"colourStrength",.73f}};
  for(int i=0;i<500;++i){processing["transferStrength"]=float(i%100)/100;c.SetProcessing(processing);}
  Check(Wait([&]{auto v=c.Snapshot();return v.status.contains("receivedProcessing")&&v.status["receivedProcessing"].value("transferStrength",-1.f)==.99f;}),"Rapid live drags coalesce to final complete configuration");
  auto received=c.Snapshot().status;Check(received["processing"]["state"]=="pending"&&received["processing"]["appliedRevision"]==0,"Request acknowledgment never fabricates an applied frame");
  auto bad=processing;bad["window"]=window;Check(!c.SetProcessing(bad),"Live edit cannot smuggle retargeting");
  for(int i=0;i<500;++i)c.SetProcessing(processing);
  auto stopAt=GetTickCount64();c.Stop();Check(Wait([&]{return c.Snapshot().phase=="Stopped";},3500),"Stop supersedes comparison pressure");
  Check(GetTickCount64()-stopAt<3000,"Stop reaches terminal state within supervisor bound");Check(!c.SetProcessing(processing),"Stopped sessions reject queued live edits");
  auto moved=window;moved["title"]="Changed media title";moved["width"]=640;
  Check(c.Start({{"mode","selected"},{"window",moved}}),"Stable window identity survives title and size changes between selections");Check(Wait([&]{return c.Snapshot().phase=="Running"&&!c.Snapshot().busy;}),"Retargeted session starts after Stop");Check(c.Snapshot().status["receivedStart"]["window"]==window,"Retarget start uses complete current catalog metadata");c.Stop();Wait([&]{return !c.Snapshot().stopping&&!c.Snapshot().busy;});
  Check(c.SelectModel(path,true),"Managed import is available only after stopping");Check(Wait([&]{auto s=c.Snapshot();return !s.busy&&s.ready;}),"Managed import acknowledgment restores verified readiness");
  s=c.Snapshot();Check(s.status["receivedModel"]["command"]=="import-model"&&s.status["receivedModel"]["modelsRoot"]==Utf8((root/L"normal"/L"Place NVNGX DLSS NR File Here").wstring()),"Actual pipe import targets the same flat onboarding directory");
  c.Restart();Check(Wait([&]{return c.Snapshot().ready&&!c.Snapshot().busy;}),"Restart restores preference but re-verifies model before ready");
  auto saved=c.Snapshot().modelPath;Check(c.SelectModel(root/L"reject.dll",true),"Custom rejection request reaches the validator");
  Check(Wait([&]{return !c.Snapshot().busy;}),"Custom rejection completes");
  Check(c.Snapshot().ready&&c.Snapshot().modelPath==saved,"Invalid custom file preserves the previous verified selection and readiness");
 }
 {
  AnythingController c(fixture,root/L"source-closed");c.Connect();Wait([&]{return c.Snapshot().connected;});c.SelectModel(root/L"source-closed.dll",false);Wait([&]{return c.Snapshot().ready&&!c.Snapshot().busy;});
  c.RefreshWindows();Wait([&]{return !c.Snapshot().busy;});auto window=c.Snapshot().windows.at(0);c.Start({{"mode","selected"},{"window",window}});Wait([&]{return c.Snapshot().phase=="Running";});
  c.RefreshWindows();c.SetProcessing({{"nrScalePercent",50},{"modelStyle",2},{"transferStrength",.4},{"colourStrength",.7}});
  Check(Wait([&]{return c.Snapshot().phase=="Stopped"&&!c.Snapshot().busy;}),"Unsolicited source-close state cancels live edit queue");
  c.Start({{"mode","selected"},{"window",window},{"transferStrength",1}});Wait([&]{return c.Snapshot().phase=="Running";});Sleep(200);
  Check(!c.Snapshot().status.contains("receivedProcessing"),"Canceled prior-session edit cannot override a new startup");
 }
 {
  AnythingController c(fixture,root/L"no-live");c.Connect();Wait([&]{return c.Snapshot().connected;});c.SelectModel(root/L"no-live.dll",false);Wait([&]{return c.Snapshot().ready&&!c.Snapshot().busy;});
  Check(!c.Start({{"mode","countdown"},{"comparisonDirection",4}}),"Older worker refuses unsupported startup direction");
  Check(c.Start({{"mode","countdown"}}),"Older compatible worker still permits its supported session");Wait([&]{return c.Snapshot().phase=="Countdown";});
  Check(!c.SetProcessing({{"nrScalePercent",67},{"modelStyle",2},{"transferStrength",1},{"colourStrength",1}}),"Older compatible worker refuses unsupported live processing");
 }
 {
  AnythingController c(fixture,root/L"restart");c.Connect();Wait([&]{return c.Snapshot().connected;});c.SelectModel(root/L"restart-required.dll",false);Wait([&]{return c.Snapshot().ready&&!c.Snapshot().busy;});
  Check(c.Start({{"mode","countdown"},{"seconds",5}}),"None target starts foreground countdown");Wait([&]{return c.Snapshot().phase=="Countdown";});c.Stop();
  Check(Wait([&]{auto s=c.Snapshot();return s.ready&&!s.busy&&!s.active&&!s.stopping&&s.phase=="Ready";},6000),"Stop automatically reconnects a worker that requires a new process");
  Check(c.Start({{"mode","countdown"},{"seconds",1}}),"A second rendering session starts without a manual Restart Worker");
 }
 {
  AnythingController c(fixture,root/L"locked");c.Connect();Wait([&]{return c.Snapshot().connected;});c.RefreshWindows();c.Stop();
  Check(Wait([&]{auto s=c.Snapshot();return s.connected&&!s.stopping&&!s.busy&&s.phase=="Locked";},3500),"Stop acknowledgment while model locked preserves responsive worker");
 }
 {
  AnythingController c(fixture,root/L"reject");c.Connect();Wait([&]{return c.Snapshot().connected;});c.SelectModel(root/L"reject.dll",false);
  Check(Wait([&]{return !c.Snapshot().busy;}),"Model rejection clears pending operation");Check(!c.Snapshot().ready&&!c.Start({{"mode","countdown"}}),"Rejected model cannot start and is not persisted as verified");
 }
 {
  AnythingController c(fixture,root/L"start-reject");c.Connect();Wait([&]{return c.Snapshot().connected;});c.SelectModel(root/L"start-reject-model.dll",false);
  Check(Wait([&]{return c.Snapshot().ready&&!c.Snapshot().busy;}),"Rejection probe first reaches model ready");Check(c.Start({{"mode","countdown"}}),"Rejection probe actually sends the start request");
  Check(Wait([&]{return !c.Snapshot().busy;}),"Rejected start acknowledgment clears pending operation");
  Check(!c.Snapshot().active&&c.Snapshot().phase=="Error","Rejected start releases provisional active state and reports failure");
  Sleep(1500);
  Check(KeptReason(c.Snapshot()),"Rejected request reason survives later idle status for returning users");
 }
 {
  AnythingController c(fixture,root/L"stall");c.Connect();Wait([&]{return c.Snapshot().connected;});c.SelectModel(root/L"stall-nvngx_dlssnr.dll",false);Wait([&]{return c.Snapshot().ready;});c.Start({{"mode","countdown"},{"seconds",5}});Wait([&]{return c.Snapshot().phase=="Starting";});
  auto stopAt=GetTickCount64();c.Stop();Check(Wait([&]{return !c.Snapshot().connected&&!c.Snapshot().stopping;},3500),"Blocked provider cannot defeat independent Stop watchdog");
  Check(GetTickCount64()-stopAt<3000&&c.Snapshot().forcedTermination,"Unresponsive isolated worker is terminated, not falsely retired");
 }
 {
  AnythingController c(fixture,root/L"quiet");c.Connect();Wait([&]{return c.Snapshot().connected;});c.SelectModel(root/L"quiet-model.dll",false);
  Check(Wait([&]{auto s=c.Snapshot();return s.ready&&!s.busy;},20000),"Quiet verification uses its operation deadline instead of the shorter idle watchdog");
 }
 {
  AnythingController c(fixture,root/L"oversize");c.Connect();Wait([&]{return c.Snapshot().connected;});c.SelectModel(root/L"oversize.dll",false);
  Check(Wait([&]{return !c.Snapshot().connected&&!c.Snapshot().busy;}),"Oversize output is bounded and faults the connection");Check(!c.Snapshot().ready,"Protocol fault revokes readiness");
 }
 if(argc>3){
  AnythingController c(argv[3],root/L"real");c.Connect();Check(Wait([&]{return c.Snapshot().connected;}),"Actual production worker connects without provider entry");
  Check(!c.Snapshot().ready&&!c.Start({{"mode","countdown"}}),"Actual worker remains model locked");
  c.RefreshWindows();Check(Wait([&]{return !c.Snapshot().busy;}),"Actual worker catalog round trip completes");
  c.RefreshWindows();c.Stop();Check(Wait([&]{auto s=c.Snapshot();return s.connected&&!s.stopping&&!s.busy&&s.phase=="Locked";},3500),"Actual locked Stop is acknowledged without unnecessary process termination");
  std::ofstream(root/L"nvngx_dlssnr.dll")<<"not a model";c.SelectModel(root/L"nvngx_dlssnr.dll",false);
  Check(Wait([&]{return !c.Snapshot().busy;}),"Actual model rejection returns to responsive controls");Check(!c.Snapshot().ready,"Actual worker refuses wrong model bytes");
 }
 return failed?1:0;
}
#else
int main(){std::cout<<"FAIL Anything worker connection and supervision are not implemented"<<std::endl;return 1;}
#endif
