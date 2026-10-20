#include "InstallQueueIdentityContractTests.cpp"
#define CreateFileW IdentityContract::Open
#define GetFileInformationByHandle IdentityContract::Basic
#define GetFileInformationByHandleEx IdentityContract::Extended
#define GetFinalPathNameByHandleW IdentityContract::FinalName
#include "ui/ProfileCache.cpp"
#undef CreateFileW
#undef GetFileInformationByHandle
#undef GetFileInformationByHandleEx
#undef GetFinalPathNameByHandleW

namespace {
nh::Json OwnershipInspection(const nh::Game& game){return {{"status","Inspected"},{"inspection",{{"target",{{"executable",game.target.path},{"bitness",64}}},{"state",{{"status","Installed"},{"proxy","dxgi.dll"}}},{"settings",nh::Json::array()},{"configRevision",game.id}}}};}
std::pair<nh::Game,nh::Game> OwnershipPair(const std::filesystem::path& root){
 auto a=Game(root,"CaseScope");auto b=a;b.id=b.title="case-distinct-B";b.root=nh::Utf8((root/L"casescope").wstring());b.target.path=nh::Utf8((root/L"casescope"/L"Game.exe").wstring());return {a,b};
}
void OwnershipDraft(nh::HubModel& model){nh::SettingsField field;field.section="Fixture";field.key="Draft";field.type="text";field.original="saved";field.available=true;strcpy_s(field.draft.data(),field.draft.size(),"B unsaved draft");model.settings.push_back(std::move(field));}
bool OwnershipDraftIntact(const nh::HubModel& model){return model.settings.size()==1&&std::string(model.settings[0].draft.data())=="B unsaved draft";}
}
std::filesystem::path OwnershipLegacyPath(const std::string& executable){
 auto key=std::filesystem::path(nh::Wide(executable)).lexically_normal().wstring();std::replace(key.begin(),key.end(),L'/',L'\\');std::transform(key.begin(),key.end(),key.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});const auto bytes=nh::Utf8(key);
 HCRYPTPROV provider=0;HCRYPTHASH hash=0;BYTE digest[32]{};DWORD size=sizeof(digest);
 if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)||!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)||!CryptHashData(hash,reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),0)||!CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0))throw std::runtime_error("Owned legacy-key fixture failed");
 CryptDestroyHash(hash);CryptReleaseContext(provider,0);std::wstring name;constexpr wchar_t hex[]=L"0123456789abcdef";for(auto value:digest){name+=hex[value>>4];name+=hex[value&15];}return nh::UserRoot()/L"profiles"/(name+L".json");
}
void OwnershipMakeLegacy(const nh::Game& game){
 const auto legacy=OwnershipLegacyPath(game.target.path);if(!nh::SaveGameProfile(game.target.path,OwnershipInspection(game).at("inspection")))throw std::runtime_error("Owned profile fixture failed to save");
 // Find the file by its authenticated document target, not the current key algorithm.
 for(const auto& entry:std::filesystem::directory_iterator(nh::UserRoot()/L"profiles"))if(entry.path().extension()==L".json"){
  const auto record=Read(entry.path());if(record.value("executable",std::string{})==game.target.path){if(entry.path()!=legacy){std::filesystem::copy_file(entry.path(),legacy,std::filesystem::copy_options::overwrite_existing);std::filesystem::remove(entry.path());}return;}}
 throw std::runtime_error("Owned profile fixture document missing");
}
int RunInstallQueueIdentityOwnershipTests(){
 int checks=0,failed=0;auto check=[&](bool pass,const char* text){++checks;failed+=!pass;std::cout<<(pass?"PASS ":"FAIL ")<<text<<std::endl;};
 const auto root=nh::UserRoot()/L"queue-identity-ownership";std::filesystem::create_directories(root);IdentityContract::fixtureRoot=root.wstring();auto [a,b]=OwnershipPair(root);
 if(!std::filesystem::exists(nh::Wide(b.target.path))){std::cout<<"UNAVAILABLE controlled case-spelling adapter; no filesystem settings changed\n";return 0;}
 IdentityContract::mode=IdentityContract::Mode::UpperId;
 {
  nh::HubModel model;model.games={a,b};model.selected=0;model.ChooseProxy(0);model.Plan("Install");Feed(model,QueuePlan(Read(model.installer.request),"ownership-completion",QueueDefaults()));
  model.Select(1);OwnershipDraft(model);model.gameDiagnosticsText="B diagnostics";const auto before=model.inspection;
  Feed(model,QueueDone(a,"ownership-completion"));
  const bool deferredB=model.installer.busy&&model.installer.readOnlyInstaller&&model.installer.action=="Inspect"&&Read(model.installer.request).value("gameExecutable",std::string{})==b.target.path;
  check(model.selected==1&&model.inspection==before&&OwnershipDraftIntact(model)&&(deferredB?model.gameDiagnosticsText.empty():model.gameDiagnosticsText=="B diagnostics"),"A completion preserves B inspection and drafts; deferred refresh belongs to B and may clear its diagnostics");ResetQueueForNextCase(model);
 }
 {
  nh::HubModel model;model.games={a,b};model.selected=0;model.proxy=-1;model.Plan("Install");
  Feed(model,{{"status","NeedsDecision"},{"decisionKind","ProxyRequired"},{"target",a.target.path}});model.Select(1);model.ChooseProxy(2);model.ChooseQueuedProxy(0);
  check(model.proxy==2&&model.QueuedProxyName()=="dxgi.dll","queue A proxy decision preserves distinct case-spelled selected B proxy");ResetQueueForNextCase(model);
 }
 {
  nh::HubModel model;model.games={a,b};model.selected=0;model.ChooseProxy(0);model.Plan("Install");Feed(model,QueuePlan(Read(model.installer.request),"ownership-receipt",QueueDefaults()));
  auto wrong=QueueDone(b,"ownership-receipt");Feed(model,wrong);
  check(model.CurrentQueuedInstall()&&model.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::NeedsAttention&&model.InstallQueueItems()[0].phase!=nh::QueuedInstall::Phase::Succeeded,"case-fold-equal but physically distinct receipt cannot complete queue A");ResetQueueForNextCase(model);
 }
 {
  nh::HubModel model;model.games={a,b};model.selected=1;model.RefreshSelected();Feed(model,OwnershipInspection(b));OwnershipDraft(model);model.Select(0);Feed(model,OwnershipInspection(a));model.ChooseProxy(0);model.Plan("Install");model.Select(1);
  check(model.inspection.value("configRevision",std::string{})==b.id&&OwnershipDraftIntact(model),"case-distinct per-game session cache restores B data rather than A data while A is queued");ResetQueueForNextCase(model);
 }
 {
  nh::RemoveGameProfile(a.target.path);nh::RemoveGameProfile(b.target.path);
  const bool savedA=nh::SaveGameProfile(a.target.path,OwnershipInspection(a).at("inspection")),savedB=nh::SaveGameProfile(b.target.path,OwnershipInspection(b).at("inspection"));
  const auto loadedA=nh::LoadGameProfile(a.target.path),loadedB=nh::LoadGameProfile(b.target.path);
  check(savedA&&savedB&&loadedA&&loadedB&&loadedA->inspection.value("configRevision",std::string{})==a.id&&loadedB->inspection.value("configRevision",std::string{})==b.id,"case-distinct persisted profiles round-trip independently despite equal basic indexes and lexical fold");
  nh::RemoveGameProfile(a.target.path);const auto retainedB=nh::LoadGameProfile(b.target.path);
  check(retainedB&&retainedB->inspection.value("configRevision",std::string{})==b.id,"removing A advisory profile preserves B persisted profile");nh::RemoveGameProfile(b.target.path);
 }
 {
  OwnershipMakeLegacy(a);const auto legacyA=nh::LoadGameProfile(a.target.path);
  check(legacyA&&legacyA->inspection.value("configRevision",std::string{})==a.id,"same-target usable legacy folded profile remains readable");nh::RemoveGameProfile(a.target.path);
  OwnershipMakeLegacy(b);const auto path=OwnershipLegacyPath(b.target.path);const auto before=Read(path);const auto wrong=nh::LoadGameProfile(a.target.path);nh::RemoveGameProfile(a.target.path);
  check(!wrong&&std::filesystem::exists(path)&&Read(path)==before,"A cannot reuse or remove another physical target B legacy folded profile");nh::RemoveGameProfile(b.target.path);
 }
 IdentityContract::mode=IdentityContract::Mode::Native;std::cout<<"RESULT checks="<<checks<<" failed="<<failed<<" ACTUAL_HUBMODEL=true CONTROLLED_LEGAL_METADATA=true NATIVE_CASE_SENSITIVE_QUALIFIED=false NO_GAME_LAUNCH=true\n";return failed;
}
#ifdef NEUROTIC_INSTALL_QUEUE_IDENTITY_OWNERSHIP_STANDALONE
int main()try{return RunInstallQueueIdentityOwnershipTests()?1:0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
#endif
