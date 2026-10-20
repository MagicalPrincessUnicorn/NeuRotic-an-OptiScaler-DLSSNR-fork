#include "InstallQueueAliasBindingTests.cpp"

int RunInstallQueueConflictAliasTests(){
 IdentityContract::mode=IdentityContract::Mode::Native;const auto root=nh::UserRoot()/L"queue-conflict-alias";auto game=Game(root,"physical installation with a long ordinary directory name");
 std::vector<wchar_t> buffer(32768);const auto used=GetShortPathNameW(nh::Wide(game.target.path).c_str(),buffer.data(),static_cast<DWORD>(buffer.size()));const auto alias=used&&used<buffer.size()?nh::Utf8(std::wstring(buffer.data(),used)):std::string{};
 const bool available=!alias.empty()&&_stricmp(alias.c_str(),game.target.path.c_str())!=0;std::cout<<"PHYSICAL_ALIAS_CAPABILITY_AVAILABLE="<<available<<" NO_GLOBAL_8DOT3_CHANGES=true\n";
 if(!available){std::cout<<"UNAVAILABLE ordinary short alias; zero conflict controls executed\n";return 0;}
 nh::HubModel model;model.games={game};model.selected=0;model.RefreshSelected();
 const std::string originalRevision(64,'a');
 nh::Json baseline={{"target",{{"executable",game.target.path},{"bitness",64}}},{"state",{{"status","Installed"},{"proxy","dxgi.dll"}}},{"configRevision",originalRevision},{"settings",nh::Json::array({{{"section","DlssNr"},{"key","Enabled"},{"type","boolean"},{"available",true},{"value","false"},{"values",nh::Json::array({"auto","true","false"})}}})}};
 Feed(model,{{"status","Inspected"},{"inspection",baseline}});if(model.settings.size()!=1)throw std::runtime_error("Owned initial inspection fixture failed");strcpy_s(model.settings[0].draft.data(),model.settings[0].draft.size(),"true");
 model.PlanSettings();const auto plan=Planned(Read(model.installer.request));Feed(model,plan);
 if(!model.installer.busy||model.installer.readOnlyInstaller||Read(model.installer.request).value("kind",std::string{})!="Execute")throw std::runtime_error("Owned SaveSettings fixture did not reach bound Execute");
 auto outside=baseline;outside["target"]["executable"]=alias;outside["configRevision"]=std::string(64,'b');outside["settings"][0]["value"]="auto";outside["state"]["note"]="fresh external metadata";
 Feed(model,{{"status","PreconditionChanged"},{"planId",plan.at("planId")},{"target",alias},{"reason","Owned settings changed externally"},{"inspection",outside}});
 const bool pass=model.showIssue&&!model.installer.busy&&model.lastResult.value("status",std::string{})=="PreconditionChanged"&&model.settings.size()==1&&std::string(model.settings[0].draft.data())=="true"&&model.settings[0].original=="false"&&model.inspection.value("configRevision",std::string{})==originalRevision&&model.inspection.at("state").value("note",std::string{})=="fresh external metadata";
 std::cout<<(pass?"PASS ":"FAIL ")<<"bound same-physical short-alias SaveSettings conflict preserves draft and original write revision\n";ResetQueueForNextCase(model);
 std::cout<<"RESULT checks=1 failed="<<!pass<<" ACTUAL_HUBMODEL=true ACTUAL_NATIVE_SHORT_ALIAS=true NO_GAME_LAUNCH=true\n";return !pass;
}
#ifdef NEUROTIC_INSTALL_QUEUE_CONFLICT_ALIAS_STANDALONE
int main()try{return RunInstallQueueConflictAliasTests()?1:0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
#endif
