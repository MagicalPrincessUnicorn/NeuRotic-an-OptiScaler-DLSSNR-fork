// Pipe/control fixture only. It performs no capture, GPU work or model inference.
#include <windows.h>
#include <json.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>
using Json=nlohmann::json;
static void Emit(const Json& j){std::cout<<j.dump()<<std::endl;}
int main(){
 Json status={{"apiVersion",1},{"event","status"},{"phase","Locked"},{"model",{{"ready",false},{"path",""}}},{"requestedRevision",0},{"appliedRevision",0},{"restartRequired",false}};
#ifndef NH_LEGACY_PROTOCOL
 status["capabilities"]={{"fsrFrameGeneration2x",true},{"hostProcessExclusion",true},{"requestRevisionErrors",true},{"nrResolutionScale",true},{"comparisonStripes",true},{"outputSnapshots",true},{"modelStyles",true},{"liveProcessing",true},{"comparisonDirections",true}};
#endif
 status["capabilities"]["spatialSuperResolution"]=true;status["capabilities"]["optionalNeuralRendering"]=true;status["capabilities"]["fullscreenSuperResolution"]=true;
 status["capabilities"]["oneFrameMeasurement"]=true;
 status["measurement"]={{"state","disabled"},{"requestId",0ull},{"ownershipRetired",true},{"summaries",nullptr}};
#ifdef NH_DEPTH_PROTOCOL
 status["capabilities"]["depthAnythingV2"]=true;status["capabilities"]["depthPreviewWithoutNr"]=true;
#endif
 Emit(status);std::string line;uint64_t revision=0,measurementId=0;
 while(std::getline(std::cin,line)){
  auto j=Json::parse(line);auto command=j.value("command","");
  if(command=="quit")return 0;
  if(command=="status"){Emit(status);continue;}
  if(command=="measure-frame"){
   const uint64_t id=j.value("requestId",0ull);
   status["measurementRequestId"]=id;
   if(j.size()!=2||!id||id<=measurementId||!status["capabilities"].value("oneFrameMeasurement",false)){
    status["event"]="measurement-rejected";status["measurementError"]="Fixture refused stale, malformed or unsupported measurement";Emit(status);continue;
   }
   const bool protocolComplete=status["model"].value("path",std::string{}).find("probe-complete")!=std::string::npos;
   if(protocolComplete&&measurementId){status["event"]="status";status["unrelatedCompleteForTest"]=true;Emit(status);Sleep(200);}
   measurementId=id;status["receivedMeasurement"]=j;
   const bool keepPending=status["model"].value("path",std::string{}).find("probe-pending")!=std::string::npos;
   status["measurement"]={{"state","pending"},{"requestId",id},{"ownershipRetired",false},{"summaries",nullptr}};
   // This unrelated status must not acknowledge the pending request.
   status["event"]="status";Emit(status);Sleep(200);
   if(!keepPending)status["measurement"]={{"state","unsupported"},{"requestId",id},{"ownershipRetired",true},{"summaries",nullptr},{"reason","Inert fixture has no rendering backend"}};
   if(protocolComplete)status["measurement"]={{"state","complete"},{"requestId",id},{"ownershipRetired",true},{"summaries",Json::array({Json{{"protocolFixtureOnly",true}},Json{{"protocolFixtureOnly",true}},Json{{"protocolFixtureOnly",true}},Json{{"protocolFixtureOnly",true}}})}};
   status["event"]="measurement";Emit(status);continue;
  }
  if(command=="catalog"){
   Json window={{"hwnd",101ull},{"pid",999999u},{"processCreation",123456ull},{"title","Media Ω"},{"executable","C:/Media/player.exe"},{"windowClass","Fixture"},{"x",0},{"y",0},{"width",480},{"height",270}};
   Json host=window;host["pid"]=j.value("hostPid",0u);Emit({{"event","catalog"},{"windows",Json::array({window,host})}});
   if(status["phase"]=="Running"&&status["model"]["path"].get<std::string>().find("source-closed")!=std::string::npos){status["phase"]="Stopped";status["event"]="stopped";Emit(status);}continue;
  }
  if(command=="start"&&status["model"]["path"].get<std::string>().find("start-reject")!=std::string::npos){Emit({{"event","error"},{"reason","Source identity changed"},{"requestRevision",j["revision"]},{"requestedRevision",revision}});continue;}
  if(j.contains("revision")){auto next=j["revision"].get<uint64_t>();if(next<=revision){Emit({{"event","error"},{"reason","Stale revision"}});continue;}revision=next;status["requestedRevision"]=revision;}
  if(command=="set-model"||command=="import-model"){
   auto path=j.value("path","");std::ifstream bytes(std::filesystem::u8path(path));std::string first;bytes>>first;
   status["modelChecks"]=status.value("modelChecks",0)+1;
   if(first=="delayed"){status["event"]="status";status["reason"]="Fixture verification in flight";Emit(status);Sleep(600);status.erase("reason");}
   if(path.ends_with("reject.dll")||first=="reject"){auto error=status;error["event"]="error";error["reason"]="Unsupported model";Emit(error);continue;}
   if(command=="import-model"&&std::filesystem::is_regular_file(std::filesystem::u8path(path))){
    auto folder=std::filesystem::u8path(j.at("modelsRoot").get<std::string>());std::filesystem::create_directories(folder);auto target=folder/L"nvngx_dlssnr.dll";
    if(!std::filesystem::exists(target))std::filesystem::copy_file(std::filesystem::u8path(path),target);
    auto encoded=target.u8string();path.assign(encoded.begin(),encoded.end());
   }
   if(path.find("oversize")!=std::string::npos){std::cout<<std::string(2*1024*1024,'x')<<std::endl;continue;}
   if(path.find("quiet-model")!=std::string::npos)Sleep(16000);
   status["model"]={{"ready",true},{"path",path},{"sha256","fixture"}};status["phase"]="Ready";status["event"]="model";
   status["receivedModel"]=j;status["capabilities"]["liveProcessing"]=path.find("no-live")==std::string::npos;status["capabilities"]["comparisonDirections"]=path.find("no-live")==std::string::npos;
   for(const char* capability:{"spatialSuperResolution","optionalNeuralRendering","fullscreenSuperResolution"})status["capabilities"][capability]=path.find("no-sr")==std::string::npos;
   status["capabilities"]["oneFrameMeasurement"]=path.find("no-sr")==std::string::npos&&path.find("no-measure")==std::string::npos;
   status["capabilities"]["fsrFrameGeneration2x"]=path.find("no-fg")==std::string::npos;
  }
  if(command=="start"){
   status["superResolution"]={{"requested",j.value("superResolution",std::string("off"))},{"effective","off"},{"state",j.value("superResolution",std::string("off"))=="fsr1"?"source-size":"off"},{"reason","Source-size fixture"}};
   status["neuralRendering"]={{"requested",j.value("neuralRendering",true)},{"effective",false}};
   status.erase("window");if(j.value("mode","")=="selected")status["window"]=j["window"];
   status["phase"]="Starting";status["event"]="status";Emit(status);
   if(status["model"]["path"].get<std::string>().find("stall")!=std::string::npos){Sleep(INFINITE);}
   status["phase"]=j.value("mode","")=="countdown"?"Countdown":"Running";status["event"]="started";status["receivedStart"]=j;status.erase("receivedProcessing");status.erase("snapshot");status["countdownRemainingMs"]=uint64_t(j.value("seconds",5u))*1000;status["presentAccepted"]=0;status["nrCompleted"]=0;status["sourcePaused"]=false;
   status["measurement"]={{"state","disabled"},{"requestId",measurementId},{"ownershipRetired",true},{"summaries",nullptr}};status.erase("receivedMeasurement");
   status["processing"]={{"state","applied"},{"requestedRevision",revision},{"appliedRevision",revision}};
  }
  if(command=="start"&&j.value("mode","")=="countdown"&&j.value("seconds",0u)==1){status["phase"]="Running";status["window"]={{"hwnd",101ull},{"pid",999999u},{"processCreation",123456ull},{"title","Media Ω"},{"executable","C:/Media/player.exe"},{"windowClass","Fixture"},{"x",0},{"y",0},{"width",480},{"height",270}};}
  if(command=="set-processing"){
   status["processing"]={{"state","pending"},{"requestedRevision",revision},{"appliedRevision",0},{"requested",j}};status["event"]="processing-requested";status["receivedProcessing"]=j;Emit(status);continue;
  }
  if(command=="snapshot"){status["snapshot"]={{"state","pending"}};status["event"]="snapshot-requested";}
  if(command=="set-frame-generation"){status["receivedFrameGeneration"]=j;status["frameGeneration"]={{"requested",j["frameGeneration"]},{"available",true},{"effective","off"},{"state","warming-up"}};status["event"]="frame-generation";}
  if(command=="set-comparison"){status["comparisonStripes"]=j.value("stripes",0);status["comparisonSplit"]=j["split"];status["event"]="comparison";}
  if(command=="stop"||command=="cancel"){status["phase"]=status["model"].value("ready",false)?"Stopped":"Locked";if(status["model"].value("path",std::string{}).find("restart-required")!=std::string::npos){status["phase"]="RestartRequired";status["restartRequired"]=true;}status["event"]="stopped";}
  if(command=="stop"||command=="cancel")status["measurement"]={{"state","invalidated"},{"requestId",measurementId},{"ownershipRetired",true},{"summaries",nullptr}};
  status["appliedRevision"]=revision;Emit(status);
 }
 return 0;
}
