#pragma once
#include "VulkanNrSession.h"
#include <shaders/dlssnr/DlssNr_Common.h>
#include <shaders/dlssnr/DlssNr_ExposureGuard.h>
#include <span>
#include <array>
namespace DlssNr {
struct VkNrAnchor { float scan=0,white=0; };
std::vector<VkNrAnchor> ParseVkNrAnchors(const std::string&);
float AnchoredVkNrWhitePoint(std::span<const VkNrAnchor>,float,bool,float);
float VkNrCalibrationPercentile(std::span<const float>);
bool SameVkNrDiagnosticContext(const VkNrFrameContract&,const VkNrFrameContract&);
float ResolveVkNrWhitePoint(uint32_t source,float manual,float exposure,float preExposure,float trim,
                           std::optional<float> scan,ExposureGuard::WhitePointHold&,
                           std::optional<float> automatic = {});
// Derived normalization, never game exposure. The caller admits only GPU-complete,
// host-visible readbacks of the untouched scene in this exact diagnostic context.
class VkNrAutomaticWhitePoint {
 public:
  struct Observation {
   const char* outcome="not-observed";
   size_t tiles=0,positive=0,lit=0,stableSamples=0;
   float peak=0,percentile=0,windowLow=0,windowHigh=0;
  };
  void Configure(const VkNrFrameContract&);
  void Reset();
  bool ObserveCompleted(const VkNrFrameContract&,uint64_t sequence,std::span<const float> tilePeaks);
  std::optional<float> Value() const { return value_; }
  const Observation& LastObservation() const { return observation_; }
 private:
  VkNrFrameContract frame_{};bool configured_=false;uint64_t sequence_=0;
  std::array<float,8> history_{};size_t count_=0,next_=0;
  std::optional<float> value_;
  Observation observation_;
};
class VulkanNrExposureScan {
 public:
  explicit VulkanNrExposureScan(VkNrRecordingOwner& owner=VulkanNrRecordings()):owner_(owner){}
  ~VulkanNrExposureScan();
  void Configure(const VkNrFrameContract&,const std::string& anchors,bool inverted,float trim);
  bool Record(const VkFrameRequest&,VkNrUseId);
  bool Readback(VkNrUseId,float,bool hostVisible);
  std::optional<float> CompletedWhitePoint(const VkNrRecordingOwner&);
  std::optional<float> Value() const { return value_; }
  float Low() const{return low_;} float High() const{return high_;}
  void Reset();
 private:
  VkNrRecordingOwner& owner_;VkNrFrameContract frame_{};std::string serialized_;
  std::vector<VkNrAnchor> anchors_;bool inverted_=false;float trim_=1;
  struct Pending { VkNrUseId use;uint64_t sequence;std::optional<float> value; };
  std::vector<Pending> pending_;uint64_t sequence_=0,published_=0;
  std::optional<float> value_,white_;float low_=0,high_=0;
};
// Resources are owned by the model generation. This stores their immutable matching frame facts.
class VkNrHeldFrame {
 public:
  bool Capture(const VkFrameRequest& request,float white){if(active_||!request.color)return false;frame_=request;white_=white;active_=true;return true;}
  std::optional<VkFrameRequest> Select(const VkNrFrameContract& current) const {
   return active_&&SameVkNrDiagnosticContext(frame_.contract,current)?std::optional(frame_):std::nullopt;
  }
  void LockWhitePoint(float white){if(active_&&white_==0)white_=white;}
  bool Active() const{return active_;} float WhitePoint() const{return white_;}
 private: VkFrameRequest frame_{};float white_=1;bool active_=false;
};
template<class Settings> void PopulateVkNrDiagnosticConstants(const Settings& cfg,const VkNrFrameContract& frame,DlssNrConstants& c) {
 c.DebugView=frame.placement==VkNrPlacement::BeforeSR?0u:std::min(cfg.DlssNrDebugView.value_or_default(),3u);
 c.DebugScale=cfg.DlssNrWhitePointScale.value_or_default();
}
}
