#include "ui/HubViewModel.h"
#include <fstream>
#include <iostream>
int RunLibraryRootTests(){
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failed;};
 auto user=nh::UserRoot();std::filesystem::create_directories(user);auto cache=user/L"library.json";std::string prior;bool had=std::filesystem::exists(cache);if(had){std::ifstream in(cache);prior.assign(std::istreambuf_iterator<char>(in),{});}
 auto fixture=user/L"library-root-tests";if(std::filesystem::exists(fixture))throw std::runtime_error("Previous library-root fixture remains");
 std::filesystem::create_directories(fixture/L"Library"/L"Game");std::filesystem::create_directories(fixture/L"Custom");std::filesystem::create_directories(fixture/L"OldCustom");std::filesystem::create_directories(fixture/L"Metadata");
 auto root=nh::Utf8((fixture/L"Library").wstring()),game=nh::Utf8((fixture/L"Library"/L"Game").wstring()),custom=nh::Utf8((fixture/L"Custom").wstring()),oldCustom=nh::Utf8((fixture/L"OldCustom").wstring());
 auto write=[&](const nh::Json& value){std::ofstream(cache)<<value.dump();};
 auto wait=[&](nh::HubModel& model){auto deadline=GetTickCount64()+15000;while(model.readiness.busy&&GetTickCount64()<deadline){model.Poll();Sleep(1);}if(model.readiness.busy)throw std::runtime_error("Root migration preflight did not finish");};
 try{
  nh::Json legacy={{"schemaVersion",2},{"searchRoots",nh::Json::array({custom,custom+"\\"})},{"excludedRoots",nh::Json::array({root+"\\"})},{"games",nh::Json::array({{{"id","keep-id"},{"title","Keep favorite"},{"store","GOG"},{"root",game},{"favorite",true},{"storeId","keep-store-id"},{"iconPath",game+"/icon.png"}}})},{"knownDirectories",nh::Json::array({{{"path",game},{"source","GOG"},{"automatic",true}},{{"path",nh::Utf8((fixture/L"Metadata").wstring())},{"source","Epic"},{"automatic",true}},{{"path",oldCustom},{"source","Custom"},{"automatic",false}}})}};
  write(legacy);nh::HubModel loaded;loaded.Load();wait(loaded);
  check(loaded.games.size()==1&&loaded.games[0].id=="keep-id"&&loaded.games[0].favorite&&loaded.games[0].storeId=="keep-store-id"&&loaded.games[0].iconHint==game+"/icon.png","ROOT-CACHE migration preserves games, favorites, launcher identity and artwork");
  check(loaded.SearchDirectories().size()==2&&loaded.knownDirectories.empty()&&loaded.searchRoots.size()==2,"ROOT-CACHE schema2 discards untrusted automatic metadata and migrates/deduplicates custom configuration");
  loaded.Save();nh::Json saved;{std::ifstream in(cache);saved=nh::Json::parse(in);}check(saved.value("schemaVersion",0)==3,"ROOT-CACHE library-root schema is versioned");
  check(loaded.anythingUi.countdownSeconds==3&&saved.value("anythingCountdownSeconds",0)==3,"NH-NR: older preferences acquire the three-second countdown default");
  for(int seconds:{1,9,30}){loaded.anythingUi.countdownSeconds=seconds;loaded.Save();nh::HubModel restored;restored.Load();wait(restored);check(restored.anythingUi.countdownSeconds==seconds&&restored.games.size()==1,"NH-NR: countdown preference round trips with the existing library owner");}
  for(const auto& invalid:nh::Json::array({0,-1,31,"9",true,nullptr,UINT64_MAX})){auto malformed=saved;malformed["anythingCountdownSeconds"]=invalid;write(malformed);nh::HubModel restored;restored.Load();wait(restored);check(restored.anythingUi.countdownSeconds==3&&restored.games.size()==1,"NH-NR: invalid countdown preference falls back without discarding the library");}
  legacy.erase("knownDirectories");legacy["schemaVersion"]=1;legacy.erase("searchRoots");write(legacy);nh::HubModel oldest;oldest.Load();wait(oldest);check(oldest.games.size()==1&&oldest.SearchDirectories().empty(),"ROOT-CACHE old games never synthesize search directories");
  nh::HubModel remove;remove.games=loaded.games;remove.searchRoots={custom};remove.RemoveDirectory(0);check(remove.games.size()==1&&remove.excludedRoots.size()==1&&remove.SearchDirectories().empty()&&std::filesystem::is_directory(fixture/L"Custom"),"ROOT-REMOVE custom roots exclude future provider scans without removing games/files");
  remove.knownDirectories={{custom,"Epic",true},{custom+"Sibling","GOG",true}};check(remove.SearchDirectories().empty(),"ROOT-MISSING cached automatic libraries require an existing directory");
  remove.excludedRoots.clear();remove.knownDirectories={{root,"Steam",true}};remove.Save();nh::HubModel current;current.Load();wait(current);check(current.SearchDirectories().size()==1&&current.SearchDirectories()[0].path==root&&current.games.size()==1,"ROOT-CACHE schema3 retains real automatic roots independently of games");
  nh::HubModel identity;identity.games=loaded.games;identity.games[0].store="Steam";identity.games[0].steamRoot="C:/OriginalSteam";identity.scanFolder=root;
  identity.MergeDiscovery({{"protocolVersion",2},{"kind","DiscoveryResult"},{"errors",nh::Json::array()},{"games",nh::Json::array({{{"id","rescan-id"},{"title","Local Steam metadata"},{"store","Steam"},{"storeId","keep-store-id"},{"installRoot",game},{"steamRoot",root}}})}});
  check(identity.games.size()==1&&identity.games[0].id=="keep-id"&&identity.games[0].favorite&&identity.games[0].steamRoot=="C:/OriginalSteam"&&identity.games[0].iconHint==game+"/icon.png","ROOT-IDENTITY local provider rescan preserves game identity and launcher artwork location");
  auto guard=fixture/L"unrelated.txt",legacyTemporary=user/L"library.json.tmp";
  const std::string sentinel="Unrelated user bytes must survive library saves";
  std::ofstream(guard,std::ios::binary)<<sentinel;
  if(std::filesystem::exists(legacyTemporary))throw std::runtime_error("Unexpected legacy library temporary file");
  if(!CreateHardLinkW(legacyTemporary.c_str(),guard.c_str(),nullptr))throw std::runtime_error("Cannot construct linked cache regression");
  identity.Save();std::string observed;{std::ifstream in(guard,std::ios::binary);observed.assign(std::istreambuf_iterator<char>(in),{});}
  check(observed==sentinel,"ROOT-SAVE legacy temporary hardlink cannot overwrite unrelated user bytes");
  std::filesystem::remove(legacyTemporary);
  {std::ifstream in(cache);saved=nh::Json::parse(in);}check(saved["games"][0]["id"]=="keep-id","ROOT-SAVE linked legacy temporary does not prevent a normal library save");
  auto original=saved.dump();HANDLE locked=CreateFileW(cache.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  if(locked==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock library fixture");
  identity.games[0].title="Save must fail while locked";identity.Save();CloseHandle(locked);
  {std::ifstream in(cache);saved=nh::Json::parse(in);}check(saved.dump()==original&&identity.message.find("save")!=std::string::npos,"ROOT-SAVE failed replacement keeps the previous complete library and reports failure");
 }catch(...){if(had)std::ofstream(cache)<<prior;else std::filesystem::remove(cache);throw;}
 if(had)std::ofstream(cache)<<prior;else std::filesystem::remove(cache);
 for(auto& item:std::filesystem::recursive_directory_iterator(fixture))if(GetFileAttributesW(item.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("Linked library-root fixture refused");std::filesystem::remove_all(fixture);
 return failed;
}
