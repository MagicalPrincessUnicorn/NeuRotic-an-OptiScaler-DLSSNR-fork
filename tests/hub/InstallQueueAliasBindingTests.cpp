#include "InstallQueueSessionIdentityTests.cpp"

int RunInstallQueueAliasBindingTests(){
 int checks=0,failed=0;auto check=[&](bool pass,const char* label){++checks;failed+=!pass;std::cout<<(pass?"PASS ":"FAIL ")<<label<<std::endl;};IdentityContract::mode=IdentityContract::Mode::Native;
 const auto root=nh::UserRoot()/L"queue-alias-binding";auto game=Game(root,"physical installation with a long ordinary directory name");
 std::vector<wchar_t> buffer(32768);const auto used=GetShortPathNameW(nh::Wide(game.target.path).c_str(),buffer.data(),static_cast<DWORD>(buffer.size()));
 const auto alias=used&&used<buffer.size()?nh::Utf8(std::wstring(buffer.data(),used)):std::string{};
 const bool available=!alias.empty()&&_stricmp(alias.c_str(),game.target.path.c_str())!=0;
 std::cout<<"PHYSICAL_ALIAS_CAPABILITY_AVAILABLE="<<available<<" NO_GLOBAL_8DOT3_CHANGES=true\n";
 if(!available){std::cout<<"UNAVAILABLE ordinary short-path alias; zero alias controls executed\n";return 0;}
 {
  nh::HubModel model;model.games={game};model.selected=0;model.ChooseProxy(0);model.Plan("Install");const auto accepted=Read(model.installer.request),intent=model.InstallQueueItems()[0].request;
  auto planned=QueuePlan(accepted,"alias-plan-binding",QueueDefaults());planned["target"]["executable"]=alias;planned["target"]["directory"]=nh::Utf8(std::filesystem::path(nh::Wide(alias)).parent_path().wstring());Feed(model,planned);
  check(model.installer.busy&&!model.installer.readOnlyInstaller&&model.CurrentQueuedInstall()&&model.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::Installing&&Read(model.installer.request).value("planId",std::string{})=="alias-plan-binding"&&model.InstallQueueItems()[0].request==intent,"same physical short-path Planned target starts the exact immutable accepted writer");ResetQueueForNextCase(model);
 }
 {
  nh::HubModel model;model.games={game};model.selected=0;model.ChooseProxy(0);model.Plan("Install");Feed(model,QueuePlan(Read(model.installer.request),"alias-completion-binding",QueueDefaults()));
  auto done=QueueDone(game,"alias-completion-binding");done["inspection"]["target"]["executable"]=alias;Feed(model,done);
  const auto notice=model.installCompletion.Pending();
  check(model.InstallQueueItems()[0].phase==nh::QueuedInstall::Phase::Succeeded&&notice&&notice->planId=="alias-completion-binding"&&notice->target==game.target.path&&notice->gameId==game.id,"same physical short-path success retains the original completion identity and notice");ResetQueueForNextCase(model);
 }
 std::cout<<"RESULT checks="<<checks<<" failed="<<failed<<" ACTUAL_HUBMODEL=true ACTUAL_NATIVE_SHORT_ALIAS=true NO_GAME_LAUNCH=true\n";return failed;
}
#ifdef NEUROTIC_INSTALL_QUEUE_ALIAS_BINDING_STANDALONE
int main()try{return RunInstallQueueAliasBindingTests()?1:0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
#endif
