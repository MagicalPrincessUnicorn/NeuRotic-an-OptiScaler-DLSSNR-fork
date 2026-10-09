#include <menu/Localization.h>
#include "UpdateStatus.h"
#include "ManualLibrary.h"
#include <windows.h>
#include <winhttp.h>
#include <thread>
#include <mutex>
#include <array>
#include "json.hpp"
namespace nh {
namespace {
std::mutex gate;std::thread worker;UpdateStatus status;
bool Version(const std::string& raw,std::array<int,3>& parts){
 auto text=raw;if(text.starts_with("alpha-"))text.erase(0,6);else if(text.starts_with("v"))text.erase(0,1);
 size_t pos=0;for(int i=0;i<3;i++){size_t start=pos;int value=0;while(pos<text.size()&&text[pos]>='0'&&text[pos]<='9'){value=value*10+text[pos++]-'0';if(value>9999)return false;}if(pos==start)return false;parts[i]=value;if(i<2){if(pos==text.size()||text[pos++]!='.')return false;}}
 return pos==text.size();
}
struct Internet {HINTERNET h=nullptr;~Internet(){if(h)WinHttpCloseHandle(h);}};
UpdateStatus Fetch(){
 Internet session{WinHttpOpen(L"NeuRotic-Hub/0.9.7",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0)};if(!session.h)return {};
 WinHttpSetTimeouts(session.h,2000,2000,3000,3000);
 Internet connection{WinHttpConnect(session.h,L"api.github.com",443,0)};if(!connection.h)return {};
 Internet request{WinHttpOpenRequest(connection.h,L"GET",L"/repos/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases/latest",nullptr,nullptr,nullptr,WINHTTP_FLAG_SECURE)};if(!request.h)return {};
 DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES;WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled));
 if(!WinHttpSendRequest(request.h,L"Accept: application/vnd.github+json\r\n",(DWORD)-1,nullptr,0,0,0)||!WinHttpReceiveResponse(request.h,nullptr))return {};
 DWORD code=0,size=sizeof(code);if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&code,&size,nullptr)||code!=200)return {};
 std::string body;auto start=GetTickCount64();for(;;){DWORD available=0;if(GetTickCount64()-start>6000||!WinHttpQueryDataAvailable(request.h,&available))return {};if(!available)break;if(available>262144-body.size())return {};auto at=body.size();body.resize(at+available);DWORD got=0;if(!WinHttpReadData(request.h,body.data()+at,available,&got))return {};body.resize(at+got);if(!got)break;}
 auto result=nlohmann::json::parse(body,nullptr,false);if(!result.is_object()||!result.contains("tag_name")||!result["tag_name"].is_string()||result["tag_name"].get_ref<const std::string&>().size()>128||!result.contains("draft")||!result["draft"].is_boolean()||result["draft"].get<bool>())return {};
 return EvaluateRelease(result["tag_name"].get<std::string>());
}
}
UpdateStatus EvaluateRelease(const std::string& tag){std::array<int,3> latest,current{0,9,7};if(!Version(tag,latest))return {};return {latest>current?UpdateState::Available:UpdateState::Current,tag,latest>current?Neurotic::UiMessage("desktop.updatestatus.update_available_fcd24c16", "Update available"):Neurotic::UiMessage("desktop.updatestatus.up_to_date_77a5b882", "Up to date")};}
void StartUpdateCheck(){if(worker.joinable())return;{std::lock_guard lock(gate);status={UpdateState::Checking,"",Neurotic::UiMessage("desktop.updatestatus.checking_for_updates_fc997e19", "Checking for updates")};}worker=std::thread([]{UpdateStatus result;try{result=Fetch();}catch(...){}std::lock_guard lock(gate);status=std::move(result);});}
UpdateStatus GetUpdateStatus(){
 UpdateStatus result;{std::lock_guard lock(gate);result=status;}
 switch(result.state){
  case UpdateState::Checking:result.detail=Neurotic::UiMessage("desktop.updatestatus.checking_for_updates_fc997e19","Checking for updates");break;
  case UpdateState::Current:result.detail=Neurotic::UiMessage("desktop.updatestatus.up_to_date_77a5b882","Up to date");break;
  case UpdateState::Available:result.detail=Neurotic::UiMessage("desktop.updatestatus.update_available_fcd24c16","Update available");break;
  case UpdateState::Unavailable:result.detail=Neurotic::UiMessage("desktop.updatestatus.update_status_unavailable_b7f21ad3","Update status unavailable");break;
 }
 return result;
}
void StopUpdateCheck(){if(worker.joinable())worker.join();}
}
