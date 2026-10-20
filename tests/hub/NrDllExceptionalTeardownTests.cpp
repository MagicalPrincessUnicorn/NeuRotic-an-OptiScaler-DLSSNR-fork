// Real HubModel queue/source lease and real controlled child; no provider/game/device.
#include "NrDllQueueTests.cpp"
#include <atomic>

namespace {
struct TeardownChild {
 HANDLE process=nullptr,release=nullptr,ready=nullptr;
 std::thread releaser;
 ~TeardownChild(){
  if(release)SetEvent(release);
  if(releaser.joinable())releaser.join();
  if(process){WaitForSingleObject(process,5000);CloseHandle(process);}
  if(release)CloseHandle(release);
  if(ready)CloseHandle(ready);
 }
};
}

int RunNrDllExceptionalTeardownTests(){
 int checks=0,failed=0;
 auto check=[&](bool ok,const char* why){++checks;failed+=!ok;std::cout<<(ok?"PASS ":"FAIL ")<<why<<'\n';};
 const auto fixture=nh::UserRoot()/L"nr-exception-teardown";
 std::filesystem::create_directories(fixture);
 const auto source=fixture/L"nvngx_dlssnr.dll";Write(source,"inert profile, never loaded");
 nh::NrDllProfiles storage(fixture/L"vault",true);
 for(bool readOnly:{false,true}){
  const auto profile=storage.Import(source,readOnly?"Read-only input":"Writer input");storage.SetDefault(profile.id);
  TeardownChild child;
  const auto eventName=L"Local\\NeuRotic-NrTeardown-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
  child.release=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());
  const auto readyName=eventName+L"-ready";child.ready=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str());
  if(!child.release||!child.ready)throw std::runtime_error("controlled teardown event unavailable");
  bool caught=false;std::atomic<int> heldDuringWait=0,liveAfterRemoval=-1;
  try{
   nh::HubModel model;model.nrDllManager=std::make_shared<nh::NrDllManager>(fixture/L"vault",true);
   model.nrDllManager->Refresh();DrainNr(model);model.games={Game(fixture,readOnly?"reader":"writer")};model.selected=0;model.ChooseProxy(0);model.Plan("Install");DrainNr(model);
   check(model.installer.busy&&model.CurrentQueuedInstall()&&model.CurrentQueuedInstall()->nrDllLease!=nullptr,"real queue acquired the selected source before child replacement");
   // Drain only the established inert read-only fixture backend, preserving the
   // model's actual queued lease. The replacement below is a controlled child,
   // not an installer or a provider. No active writer is terminated.
   if(!model.installer.readOnlyInstaller)throw std::runtime_error("fixture backend must be read-only");
   model.installer.Cancel();nh::Json ignored;
   const auto deadline=GetTickCount64()+5000;
   while(model.installer.busy&&GetTickCount64()<deadline){model.installer.Poll(ignored);Sleep(1);}
   if(model.installer.busy||model.installer.process)throw std::runtime_error("inert fixture backend did not drain");
   wchar_t executable[32768]{};if(!GetModuleFileNameW(nullptr,executable,32768))throw std::runtime_error("fixture executable unavailable");
   auto command=nh::Quote(executable)+L" --nr-teardown-child "+nh::Quote(eventName)+L" "+nh::Quote(readyName);
   STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION info{};
   if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&info))throw std::runtime_error("controlled child unavailable");
   CloseHandle(info.hThread);
   if(!DuplicateHandle(GetCurrentProcess(),info.hProcess,GetCurrentProcess(),&child.process,0,FALSE,DUPLICATE_SAME_ACCESS)){SetEvent(child.release);WaitForSingleObject(info.hProcess,5000);CloseHandle(info.hProcess);throw std::runtime_error("child observation handle unavailable");}
   model.installer.process=info.hProcess;model.installer.busy=true;model.installer.readOnlyInstaller=readOnly;
   check(WaitForSingleObject(child.ready,5000)==WAIT_OBJECT_0,"controlled child reached its blocked-reader gate before exceptional unwind");
   check(WaitForSingleObject(child.process,0)==WAIT_TIMEOUT,"controlled child is still active before exceptional unwind");
   try{storage.Remove(profile.id);check(false,"held source must resist removal before unwind");}catch(const nh::NrDllError& e){check(e.code==nh::NrDllErrorCode::InUse,"actual queued source is held before exceptional unwind");}
   child.releaser=std::thread([&]{Sleep(100);try{storage.Remove(profile.id);heldDuringWait=2;}catch(const nh::NrDllError& e){heldDuringWait=e.code==nh::NrDllErrorCode::InUse?1:3;}catch(...){heldDuringWait=4;}liveAfterRemoval=WaitForSingleObject(child.process,0)==WAIT_TIMEOUT?1:0;if(!readOnly)SetEvent(child.release);});
   throw 73;
  }catch(int value){caught=value==73;}
  check(caught,"controlled rendering-style exception reaches its outer catch");
  check(WaitForSingleObject(child.process,0)==WAIT_OBJECT_0,readOnly?"exceptional model teardown drains the real read-only reader before releasing its source":"exceptional model teardown preserves the selected source until the real writer exits naturally");
  SetEvent(child.release);if(child.releaser.joinable())child.releaser.join();WaitForSingleObject(child.process,5000);
  check(readOnly?(heldDuringWait==1||(heldDuringWait==2&&liveAfterRemoval==0)):(heldDuringWait==1&&liveAfterRemoval==1),"catalog removal never completes before a subsequently observed still-live reader; writer remains live and leased until its release gate");
  DWORD code=STILL_ACTIVE;check(GetExitCodeProcess(child.process,&code)&&code==(readOnly?3u:0u),readOnly?"stalled read-only child receives cancellation before its safety timeout":"active writer completes naturally without termination");
  if(!storage.List().profiles.empty())storage.Remove(profile.id);check(storage.List().profiles.empty(),"source becomes removable after exceptional child drain");
 }
 std::cout<<"RESULT checks="<<checks<<" failed="<<failed<<" ACTUAL_HUBMODEL=true REAL_CONTROLLED_CHILD=true NO_PROVIDER_LOAD=true\n";
 return failed?1:0;
}

#ifdef NEUROTIC_NR_DLL_TEARDOWN_STANDALONE
int main(int argc,char** argv)try{
 if(argc==4&&std::string_view(argv[1])=="--nr-teardown-child"){
  const auto name=nh::Wide(argv[2]),readyName=nh::Wide(argv[3]);HANDLE event=OpenEventW(SYNCHRONIZE,FALSE,name.c_str());if(!event)return 5;
  HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,readyName.c_str());if(!ready){CloseHandle(event);return 6;}
  if(!SetEvent(ready)){CloseHandle(ready);CloseHandle(event);return 7;}CloseHandle(ready);
  const auto result=WaitForSingleObject(event,5000);CloseHandle(event);return result==WAIT_OBJECT_0?0:4;
 }
 return RunNrDllExceptionalTeardownTests();
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
#endif
