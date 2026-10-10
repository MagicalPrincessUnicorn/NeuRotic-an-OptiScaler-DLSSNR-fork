#include <menu/Localization.h>
#pragma once
#include "library/ManualLibrary.h"
#include <json.hpp>
#include <memory>
#include <vector>
#define NH_ANYTHING_SESSION_POLICY 1
#define NH_ANYTHING_SHUTDOWN_PROOF 1
namespace nh {
struct AnythingSnapshot {
 bool connected=false,busy=false,ready=false,active=false,stopping=false,forcedTermination=false,modelVerified=false;
 uint64_t modelRevision=0;
 uint64_t catalogRevision=0;
 std::string phase="Offline",message=Neurotic::UiMessage("desktop.anythingcontroller.provide_your_dlss_nr_file_to_enable_window_selec_41b3811a", "Provide your DLSS NR file to enable window selection."),modelPath,lastError;
 nlohmann::json status=nlohmann::json::object();
 std::vector<nlohmann::json> windows;
};
inline bool AnythingReadyForStages(const AnythingSnapshot& state,bool neuralRendering){
 return state.connected&&!state.status.value("restartRequired",false)&&(neuralRendering?state.ready:
  state.status.value("capabilities",nlohmann::json::object()).value("optionalNeuralRendering",false));
}
inline bool AnythingCanMeasureFrame(const AnythingSnapshot& state){
 if(!state.connected||!state.active||state.busy||state.stopping||state.phase!="Running"||state.status.value("sourcePaused",true)||state.status.value("restartRequired",false))return false;
 const auto caps=state.status.value("capabilities",nlohmann::json::object());
 const auto result=state.status.value("measurement",nlohmann::json::object());
 if(!caps.is_object()||!caps.value("oneFrameMeasurement",false)||!result.is_object()||!result.value("ownershipRetired",false))return false;
 const auto phase=result.value("state",std::string{});
 if(phase!="disabled"&&phase!="complete"&&phase!="unsupported"&&phase!="invalidated"&&phase!="failed")return false;
 const auto processing=state.status.value("processing",nlohmann::json::object());
 return processing.is_object()&&processing.value("state",std::string{})=="applied";
}
// The child owns every capture/provider resource. This owner supervises its process,
// drains bounded protocol output and serializes requests independently of the UI.
class AnythingController {
public:
 // Runtime callers may target their own host without consuming, importing, or
 // saving desktop preferences. Verified selections remain in memory for restart.
 struct SessionPolicy {bool excludeHostWindow=true;bool persistPreferences=true;};
 AnythingController(const std::filesystem::path& worker,const std::filesystem::path& userData);
 AnythingController(const std::filesystem::path& worker,const std::filesystem::path& userData,SessionPolicy policy);
 ~AnythingController();
 AnythingController(const AnythingController&)=delete;
 AnythingController& operator=(const AnythingController&)=delete;
 void Connect();void Restart();void Stop();
 // Off render/UI threads: request Stop, then end supervision and reap the child.
 // Returns true only after the child process exit was observed. Permanent shutdown.
 bool ShutdownAndWait(unsigned stopTimeoutMs=2000);
 // Desktop Close requests the existing cooperative quit protocol. No join here.
 void RequestShutdown();bool ShutdownReady() const;
 bool SelectModel(const std::filesystem::path& file,bool import);
 std::filesystem::path ModelFolder() const;
 bool EnsureModelFolder();
 bool RefreshWindows(const std::string& search={});
 bool Start(nlohmann::json options);
 void SetComparison(float split);
 bool SetComparison(float split,int stripes,int direction=0);
 bool SetProcessing(nlohmann::json settings);
 bool SetFrameGeneration(bool enabled);
 bool TakeSnapshot();
 bool MeasureOneFrame();
 AnythingSnapshot Snapshot() const;
 static std::filesystem::path FindWorker(const std::filesystem::path& appRoot);
private:
 struct Impl;std::unique_ptr<Impl> impl;
};
}
