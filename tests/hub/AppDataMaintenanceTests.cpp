#include "storage/AppDataMaintenance.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <winioctl.h>
namespace fs=std::filesystem;
int checks=0;void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);++checks;}
void Put(const fs::path& p,const char* data="data"){fs::create_directories(p.parent_path());std::ofstream(p)<<data;}
void Junction(const fs::path& link,const fs::path& target){
 fs::create_directory(link);HANDLE h=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);Check(h!=INVALID_HANDLE_VALUE,"junction handle");
 struct Reparse{DWORD tag;WORD size,reserved,subOffset,subLength,printOffset,printLength;wchar_t text[2048];} r{};
 auto sub=L"\\??\\"+target.wstring(),print=target.wstring();r.tag=IO_REPARSE_TAG_MOUNT_POINT;r.subLength=WORD(sub.size()*2);r.printOffset=WORD((sub.size()+1)*2);r.printLength=WORD(print.size()*2);wcscpy_s(r.text,sub.c_str());wcscpy_s(r.text+sub.size()+1,2048-sub.size()-1,print.c_str());r.size=WORD(8+(sub.size()+print.size()+2)*2);DWORD n=0;bool ok=DeviceIoControl(h,FSCTL_SET_REPARSE_POINT,&r,r.size+8,nullptr,0,&n,nullptr)!=FALSE;CloseHandle(h);Check(ok,"junction setup");
}
int main(){try{
 const auto work=fs::current_path()/L"builds/hub/maintenance-tests/fixture";
 if(fs::exists(work))throw std::runtime_error("Inspect previous maintenance fixture before reuse");
 const auto root=work/L"Hub";Put(root/L"profiles/a.json");Put(root/L"library.json");Put(root/L"Runtime/model.dll");Put(root/L"Dumbfire/broken/Pending.json");Put(work/L"outside/keep.txt");
 auto profiles=nh::PlanAppDataClear(root,nh::AppDataClearScope::GameProfiles);
 Check(profiles.Ready()&&profiles.fileCount==1,"profile plan scope");Check(fs::exists(root/L"profiles/a.json"),"plan is read only");
 auto cleared=nh::ExecuteAppDataClear(profiles);Check(cleared.complete&&cleared.deletedFiles==1,"clear profiles");Check(fs::exists(root/L"library.json")&&fs::exists(root/L"Runtime/model.dll"),"profiles preserve other state");
 Put(root/L"profiles/b.json");auto stale=nh::PlanAppDataClear(root,nh::AppDataClearScope::GameProfiles);Put(root/L"profiles/new.json");
 cleared=nh::ExecuteAppDataClear(stale);Check(!cleared.complete&&cleared.deletedFiles==0&&fs::exists(root/L"profiles/b.json"),"new entry invalidates before deletion");
 auto all=nh::PlanAppDataClear(root,nh::AppDataClearScope::AppData);Check(all.Ready()&&all.preserved.size()==1,"recovery subtree retained without parsing");
 cleared=nh::ExecuteAppDataClear(all);Check(cleared.complete,"clear app data");Check(fs::exists(root/L"Dumbfire/broken/Pending.json")&&fs::exists(work/L"outside/keep.txt"),"recovery and outside preserved");Check(!fs::exists(root/L"Runtime")&&!fs::exists(root/L"library.json"),"runtime and preferences clear");
 auto art=nh::EnsureArtworkCacheFolder(root);Check(art==root/L"artwork"&&fs::is_directory(art),"artwork folder resolved");
 Put(root/L"profiles/hard.json");Check(CreateHardLinkW((work/L"outside/linked.json").c_str(),(root/L"profiles/hard.json").c_str(),nullptr),"fixture hardlink");
 Check(!nh::PlanAppDataClear(root,nh::AppDataClearScope::GameProfiles).Ready(),"hardlink refused");fs::remove(work/L"outside/linked.json");
 auto changed=nh::PlanAppDataClear(root,nh::AppDataClearScope::GameProfiles);Put(root/L"profiles/hard.json","longer changed bytes");cleared=nh::ExecuteAppDataClear(changed);Check(!cleared.complete&&cleared.deletedFiles==0,"changed file refused");
 auto lockedPlan=nh::PlanAppDataClear(root,nh::AppDataClearScope::GameProfiles);HANDLE locked=CreateFileW((root/L"profiles/hard.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Check(locked!=INVALID_HANDLE_VALUE,"fixture lock");cleared=nh::ExecuteAppDataClear(lockedPlan);CloseHandle(locked);Check(!cleared.complete&&cleared.deletedFiles==0,"live owner blocks deletion");
 Check(!nh::PlanAppDataClear(L"C:\\",nh::AppDataClearScope::AppData).Ready(),"volume root refused");
 auto absent=nh::PlanAppDataClear(work/L"absent/Hub",nh::AppDataClearScope::AppData);Check(absent.Ready()&&absent.missing&&nh::ExecuteAppDataClear(absent).complete,"missing root no-op");bool complete=true;auto pins=nh::maintenance_detail::Parents(work/L"absent/Hub",false,&complete);Check(!complete,"missing ancestor never authorizes descendant walk");pins.clear();
 Junction(root/L"profiles/escape",work/L"outside");Check(!nh::PlanAppDataClear(root,nh::AppDataClearScope::GameProfiles).Ready(),"junction traversal refused");Check(RemoveDirectoryW((root/L"profiles/escape").c_str())!=FALSE,"fixture junction removed");Check(fs::exists(work/L"outside/keep.txt"),"junction referent untouched");
 auto oldRoot=nh::PlanAppDataClear(root,nh::AppDataClearScope::GameProfiles);fs::rename(root/L"profiles",root/L"old-profiles");fs::create_directory(root/L"profiles");fs::rename(root/L"old-profiles/hard.json",root/L"profiles/hard.json");Check(!nh::ExecuteAppDataClear(oldRoot).complete,"replacement root with same child identity refused");
 Put(root/L"profiles/a.json");Put(root/L"profiles/z.json");SetFileAttributesW((root/L"profiles/z.json").c_str(),FILE_ATTRIBUTE_READONLY);auto partial=nh::ExecuteAppDataClear(nh::PlanAppDataClear(root,nh::AppDataClearScope::GameProfiles));Check(!partial.complete&&partial.deletedFiles>0&&fs::exists(root/L"profiles/z.json"),"partial deletion reported and refusing file retained");SetFileAttributesW((root/L"profiles/z.json").c_str(),FILE_ATTRIBUTE_NORMAL);
 // Fixture cleanup is bounded to the just-created private tree, after no links remain.
 fs::remove_all(work);std::cout<<"PASS "<<checks<<" App data maintenance checks\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
