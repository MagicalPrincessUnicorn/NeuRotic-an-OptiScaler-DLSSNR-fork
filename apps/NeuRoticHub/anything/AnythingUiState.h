#include <menu/Localization.h>
#pragma once
#include <json.hpp>
#include <array>
#include <string>
#include <cstdint>
namespace nh {
struct AnythingUiState {
 bool cursor=true,wasReady=false,overlay=true,awaitingCountdown=false;
 bool frameGeneration=false;
 bool superResolution=false,neuralRendering=true,fullscreen=false;
 int resolution=0,manualPercent=100,countdownSeconds=3,activeCountdownSeconds=3;
 float split=.5f,transfer=1,colour=1;
 nlohmann::json selected,activeTarget,pendingStart;
 std::string notice;
 int comparison=0,stripes=6,look=0,comparisonDirection=0;
 bool preferencesLoaded=false,selectRequested=false;
 bool windowListPending=false;
 bool clearTargetPending=false;
 uint64_t targetCheckAfter=0;
 nlohmann::json targetCheckIdentity;
 uint64_t requestedCatalogRevision=0;
 std::string selectedUserProfile;
 std::string profile="Default";
 nlohmann::json profiles=nlohmann::json::object();
 std::array<char,97> profileName{};
 std::array<char,257> windowSearch{};
 double rateTime=0,deficitSeconds=0,incomingFps=0,processedFps=0;
 double outputFps=0;uint64_t ratePresented=0;
 uint64_t rateSession=0,rateArrivals=0,rateCompleted=0;
 bool ratesKnown=false,deficit=false;
 bool hotkeyEnabled=true,hotkeyAvailable=false;
 unsigned hotkeyModifiers=3,hotkeyKey='N',hotkeyRevision=0;
 std::string hotkeyError;
 bool screenshotHotkeyEnabled=true,screenshotHotkeyAvailable=false;
 unsigned screenshotHotkeyModifiers=3,screenshotHotkeyKey='S',screenshotHotkeyRevision=0;
 std::string screenshotHotkeyError;
};
// Idle selection changes must also invalidate a deferred start, without
// altering an active session or its target.
inline bool ClearIdleAnythingTarget(AnythingUiState& page,bool active,bool busy,bool stopping){
 if(active||busy||stopping)return false;
 page.selected=nlohmann::json();page.activeTarget=nlohmann::json();page.pendingStart=nlohmann::json();
 page.awaitingCountdown=page.selectRequested=page.windowListPending=false;
 return true;
}
}
