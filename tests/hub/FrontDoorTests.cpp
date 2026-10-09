#include "ui/HubViewModel.h"
#include "storage/UserDataSession.h"
#include <iostream>
template<class Game> int LaunchChecks(Game game) {
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failed+=!ok;};
 if constexpr(requires { MakeLaunchPlan(game); }) {
  game.store="Steam";game.storeId="730";game.target.path="C:/Games/Other.exe";
  auto steam=MakeLaunchPlan(game);check(steam.allowed&&steam.platform&&steam.file==L"steam://rungameid/730"&&steam.directory.empty(),"Steam identity routes through platform, independent of exe choice");
  for(auto id:{"0","000","4294967296","730/quit","730\" & calc","-1",""}){game.storeId=id;check(!MakeLaunchPlan(game).allowed,"Malformed Steam app identity refuses launch");}
  game.store="Manual";game.target.path="C:/Games/Space & Unicode Ω/game.exe";game.target.suitable=true;
  auto manual=MakeLaunchPlan(game);check(manual.allowed&&!manual.platform&&manual.file==L"C:/Games/Space & Unicode Ω/game.exe"&&manual.directory==L"C:/Games/Space & Unicode Ω","Manual launch preserves exact exe and working directory as data");
  game.target.suitable=false;check(!MakeLaunchPlan(game).allowed,"Unresolved manual target refuses launch");
 } else check(false,"Launch planning is available without executing a game");
 return failed;
}
int RunFrontDoorTests(){
 int failed=LaunchChecks(nh::Game{});auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failed+=!ok;};
 auto fixture=nh::UserRoot()/L"state-admission";
 {nh::UserDataSession owner(fixture);check(owner.Owns(),"AUDIT-DESK04: initial state owner admitted");auto competitor=std::async(std::launch::async,[fixture]{nh::UserDataSession other(fixture);return other.Owns();});check(!competitor.get(),"AUDIT-DESK04: concurrent state owner refused before loading");}
 {nh::UserDataSession after(fixture);check(after.Owns(),"AUDIT-DESK04: state ownership released on normal exit");}
 auto legacy=fixture/L"legacy",current=fixture/L"current";
 // This fixture's one-time marker is deliberately outside its current folder.
 // Reset only our known test documents so the same suite is repeatable.
 for(const auto& path:{fixture/L"current.legacy-import.json",current/L"library.json",legacy/L"library.json"}){auto attributes=GetFileAttributesW(path.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked front-door fixture refused");std::filesystem::remove(path);}
 nh::SaveUserFile(legacy/L"library.json","{\"schemaVersion\":3,\"games\":[]}");nh::ImportLegacyUserDocuments(legacy,current);
 nh::SaveUserFile(legacy/L"library.json","{\"schemaVersion\":3,\"games\":[\"stale\"]}");nh::ImportLegacyUserDocuments(legacy,current);
 {std::ifstream file(current/L"library.json");auto data=nh::Json::parse(file);check(data["games"].empty(),"AUDIT-DESK04: stale legacy writes never reimport over new state");}
 DeleteFileW((current/L"library.json").c_str());nh::ImportLegacyUserDocuments(legacy,current);check(!std::filesystem::exists(current/L"library.json"),"AUDIT-RL05: clearing current data never resurrects legacy documents");
 auto desktop=nh::FitHubWindow(0,0,1920,1040,1);check(desktop.width==1240&&desktop.height==960,"Default window is taller and preserves width");
 for(float dpi:{1.f,1.5f,2.f}){auto fitted=nh::FitHubWindow(-1280,20,1280,700,dpi);check(fitted.x>=-1280&&fitted.y>=20&&fitted.x+fitted.width<=0&&fitted.y+fitted.height<=720,"Window fits offset monitor work area at each DPI");}
 return failed;
}
#ifdef NH_FRONTDOOR_STANDALONE
int main(){return RunFrontDoorTests();}
#endif
