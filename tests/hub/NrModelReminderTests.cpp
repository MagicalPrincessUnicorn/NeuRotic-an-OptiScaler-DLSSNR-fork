#include "ui/NrModelReminder.h"
#include <fstream>
#include <iostream>
int main(){
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failed+=!ok;};
 using Json=nlohmann::json;using Presence=nh::NrModelPresence;int probes=0;Presence presence=Presence::Missing;
 nh::NrModelReminderCache cache([&](const auto& path){++probes;check(path.filename()==L"nvngx_dlssnr.dll","Probe uses exact private model filename");return presence;});
 Json preflight={{"components",{{"neuralModel",{{"status","Missing"}}}}}};
 const std::filesystem::path exe=L"C:/ReminderFixture/Game.exe";
 auto result=cache.Evaluate(preflight,false,exe,false,0);
 check(result.missingApp&&!result.missingGame&&result.Visible()&&probes==0,"App missing reminder does not probe an uninstalled game");
 preflight["components"]["neuralModel"]["status"]="Present";
 result=cache.Evaluate(preflight,false,exe,true,1);check(!result.missingApp&&result.missingGame&&probes==1,"Installed game missing its model has separate reminder");
 presence=Presence::Present;result=cache.Evaluate(preflight,false,exe,true,2000);check(result.missingGame&&probes==1,"Repeated frames reuse presence for two seconds");
 result=cache.Evaluate(preflight,false,exe,true,2001);check(!result.Visible()&&probes==2,"Adding model clears reminder on bounded refresh");
 presence=Presence::Unknown;result=cache.Evaluate(preflight,false,exe,true,4001);check(!result.Visible(),"Access error or unknown presence never invents missing model");
 presence=Presence::Missing;result=cache.Evaluate(preflight,false,L"C:/ReminderFixture/Other.exe",true,4002);check(result.missingGame&&probes==4,"Selected executable identity change resets cache immediately");
 preflight["components"]["neuralModel"]["status"]="Unavailable";result=cache.Evaluate(preflight,false,exe,false,5000);check(!result.Visible(),"Unavailable App model is unknown, not fabricated absence");
 preflight["components"]["neuralModel"]["status"]="Missing";result=cache.Evaluate(preflight,true,exe,false,5001);check(!result.Visible(),"App preflight in progress suppresses stale missing state");
 preflight=Json();check(!cache.Evaluate(preflight,false,exe,false,5002).Visible(),"Missing preflight evidence stays unknown");
 Json inspection={{"target",{{"executable","C:/ReminderFixture/Game.exe"},{"sha256","a"}}},{"revision","r1"},{"missing",Json::array({"nvngx_dlssnr.dll"})}};
 auto before=probes;result=cache.Evaluate(preflight,false,exe,true,6000,inspection);check(result.missingGame&&probes==before,"Exact selected-target inspection supplies known missing evidence without probe");
 inspection["target"]["executable"]="C:/Other/Game.exe";presence=Presence::Present;result=cache.Evaluate(preflight,false,exe,true,8001,inspection);check(!result.Visible()&&probes==before+1,"Another game's receipt cannot create selected-game warning");
 inspection["target"]["executable"]="C:/ReminderFixture/Game.exe";inspection["target"]["sha256"]="b";inspection["missing"]=Json::array();inspection["inventory"]=Json::array({{{"path","nvngx_dlssnr.dll"},{"sha256","model"}}});
 result=cache.Evaluate(preflight,false,exe,true,8002,inspection);check(!result.Visible()&&probes==before+1,"Matching inventory supplies model presence on identity change");
 presence=Presence::Missing;result=cache.Evaluate(preflight,false,exe,true,10002,inspection);check(result.missingGame&&probes==before+2,"Old inspection does not mask later removal forever");
 const auto root=std::filesystem::current_path()/L"builds/hub/nr-model-reminder-fixture";if(std::filesystem::exists(root)){std::cerr<<"Fixture already exists\n";return 2;}std::filesystem::create_directories(root);const auto model=root/L"nvngx_dlssnr.dll";
 check(nh::ProbeNrModelPresence(model)==Presence::Missing,"Exact model absent in existing owned directory is missing");std::ofstream(model)<<"inert fixture";check(nh::ProbeNrModelPresence(model)==Presence::Present,"Presence probe does not hash, load or execute model bytes");std::filesystem::remove(model);std::filesystem::remove(root);
 check(nh::ProbeNrModelPresence(root/L"nvngx_dlssnr.dll")==Presence::Unknown,"Missing game directory is unknown instead of missing-model claim");
 return failed?1:0;
}
