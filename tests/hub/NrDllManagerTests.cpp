#include <ui/NrDllManager.h>
#include <windows.h>
#include <fstream>
#include <iostream>
#include <thread>
using namespace nh;
static unsigned checks;
static void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
static void Drain(NrDllManager& m){
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(m.Busy()&&std::chrono::steady_clock::now()<deadline){m.Poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 Check(!m.Busy(),"manager jobs drain within fixture deadline");
}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)throw std::runtime_error("owned new fixture root required");
 const std::filesystem::path root=argv[1];if(std::filesystem::exists(root))throw std::runtime_error("fixture already exists");
 std::filesystem::create_directories(root);const auto source=root/L"nvngx_dlssnr.dll";
 {std::ofstream file(source,std::ios::binary);file<<"inert user bytes, never loaded";}
 NrDllManager m(root/L"vault",true);m.Refresh();Drain(m);
 Check(!m.Error()&&m.Catalog().profiles.empty(),"empty catalog refresh has no default or import side effect");
 m.Import(source,"Original user name");Check(m.Busy(),"import is asynchronous");Drain(m);
 Check(!m.Error()&&m.Catalog().profiles.size()==1&&m.Catalog().defaultId.empty(),"import does not silently select a default");
 const auto id=m.Catalog().profiles.at(0).id;m.SetDefault(id);Drain(m);
 Check(m.Catalog().defaultId==id,"default changes only through explicit action");
 m.Rename(id,"Renamed user profile");Drain(m);Check(m.Catalog().profiles.at(0).name=="Renamed user profile","rename is catalog data");
 {const auto vault=root/L"vault",backup=root/L"vault-read-failure";std::filesystem::rename(vault,backup);{std::ofstream f(vault);f<<"owned non-directory witness";}
  m.Refresh();Drain(m);Check(m.Ready()&&!m.CatalogCurrent()&&m.Error().has_value(),"failed real catalog read keeps old display data but invalidates current default selection");
  std::filesystem::remove(vault);std::filesystem::rename(backup,vault);m.Refresh();Drain(m);Check(m.CatalogCurrent()&&!m.Error(),"successful actual refresh restores current catalog state");}
 m.SetDefault(id);Check(!m.CatalogCurrent(),"pending default change is not exposed as a confirmed snapshot");Drain(m);Check(m.CatalogCurrent()&&m.Catalog().defaultId==id,"default mutation publishes a complete refreshed snapshot");
 {auto held=NrDllProfiles(root/L"vault",true).Acquire(id);m.Remove(id);Drain(m);
  Check(m.Error()==NrDllErrorCode::InUse&&m.Catalog().profiles.size()==1,"queued source lease reports busy and preserves profile");}
 m.Remove(id);Drain(m);Check(!m.Error()&&m.Catalog().profiles.empty(),"removal succeeds after real source lease release");
 Check(std::filesystem::exists(source),"removal never deletes original download");
 NrDllManager readOnly(root/L"vault",false);readOnly.Import(source,"No write");Drain(readOnly);
 Check(readOnly.Error()==NrDllErrorCode::ReadOnly,"writable ownership remains enforced in background job");
 m.Import(source,"Second");m.RequestStop();Drain(m);
 Check(!m.Busy(),"closing cancellation drains before controller destruction");
 std::cout<<checks<<" NR DLL manager lifecycle checks PASS; inert bytes, no provider load\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
