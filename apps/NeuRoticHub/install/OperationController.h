#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include "json.hpp"
namespace nh {
using Json=nlohmann::json;
std::filesystem::path UserRoot();
std::filesystem::path LegacyUserRoot();
std::filesystem::path SharedRuntimeRoot();
std::filesystem::path AppRoot();
std::wstring Quote(const std::wstring& value);
struct ProcessJob {
 private:
 std::string antiCheatApproval;
 uint64_t progressOffset=0;
 std::string progressPartial;
 int64_t progressDone=-1,progressTotal=-1;
 public:
 HANDLE process=nullptr,output=nullptr;
 std::filesystem::path result,request,logPath;
 std::string text,error;
 bool discovery=false,busy=false,preflight=false,readOnlyInstaller=false;
 ULONGLONG started=0;
 ULONGLONG progressReadAt=0,lastProgressAt=0;
 float progress=-1;
 std::string progressStage,action;
 bool ReadOnlyTimedOut(ULONGLONG now) const;
 void ReadProgress(const std::string& lines);
 ProcessJob()=default;
 ProcessJob(const ProcessJob&)=delete;
 ~ProcessJob();
 void StartInstaller(const Json& request);
 void AcknowledgeAntiCheat(const Json& reviewedPlan);
 void ClearAntiCheatApproval(){antiCheatApproval.clear();}
 void StartDiscovery(const Json& payload=Json{{"protocolVersion",2},{"kind","DiscoverGames"}});
 void Cancel();
 bool Poll(Json& value);
};
}
