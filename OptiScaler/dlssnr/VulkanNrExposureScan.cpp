#include "VulkanNrExposureScan.h"
#include <algorithm>
namespace DlssNr {
std::vector<VkNrAnchor> ParseVkNrAnchors(const std::string& text){
 std::vector<VkNrAnchor> result;size_t start=0;
 while(start<text.size()&&result.size()<8){auto end=text.find(';',start);auto token=text.substr(start,end-start);
  start=end==std::string::npos?text.size():end+1;auto colon=token.find(':');if(colon==std::string::npos)continue;
  try{float scan=std::stof(token.substr(0,colon)),white=std::stof(token.substr(colon+1));
   if(std::isfinite(scan)&&std::isfinite(white)&&scan>1e-6f&&scan<1e4f&&white>1e-6f)result.push_back({scan,white});}catch(...){}
 }
 std::sort(result.begin(),result.end(),[](auto a,auto b){return a.scan<b.scan;});
 result.erase(std::unique(result.begin(),result.end(),[](auto a,auto b){return a.scan==b.scan;}),result.end());return result;
}
float AnchoredVkNrWhitePoint(std::span<const VkNrAnchor> anchors,float now,bool inverted,float trim){
 if(anchors.empty()||!std::isfinite(now)||now<=1e-6f||!std::isfinite(trim))return 0;
 trim=std::clamp(trim,.25f,4.f);auto clamp=[&](float w){return std::clamp(w*trim,.01f,4096.f);};
 if(anchors.size()==1)return clamp(anchors[0].white*(inverted?now/anchors[0].scan:anchors[0].scan/now));
 if(now<=anchors.front().scan)return clamp(anchors.front().white);if(now>=anchors.back().scan)return clamp(anchors.back().white);
 for(size_t i=0;i+1<anchors.size();++i){auto a=anchors[i],b=anchors[i+1];if(now>=a.scan&&now<=b.scan&&b.scan>a.scan*1.0001f){
  const float t=(std::log(now)-std::log(a.scan))/(std::log(b.scan)-std::log(a.scan));return clamp(std::exp(std::log(a.white)+t*(std::log(b.white)-std::log(a.white))));}}
 return clamp(anchors.back().white);
}
float VkNrCalibrationPercentile(std::span<const float> values){std::vector<float> good;for(float v:values)if(std::isfinite(v)&&v>1e-6f)good.push_back(v);
 if(good.empty())return 0;size_t nth=static_cast<size_t>(float(good.size()-1)*.90f);std::nth_element(good.begin(),good.begin()+nth,good.end());return good[nth];}
bool SameVkNrDiagnosticContext(const VkNrFrameContract& a,const VkNrFrameContract& b){
 return a.route==b.route&&a.placement==b.placement&&a.deviceGeneration==b.deviceGeneration&&a.routeEpoch==b.routeEpoch&&
 a.resumeGeneration==b.resumeGeneration&&a.inputInterruptionEpoch==b.inputInterruptionEpoch&&a.swapchainGeneration==b.swapchainGeneration&&a.queue==b.queue&&a.queueFamily==b.queueFamily&&
 a.output.width==b.output.width&&a.output.height==b.output.height&&a.work.width==b.work.width&&a.work.height==b.work.height&&
 a.representation==b.representation&&a.guideGeneration==b.guideGeneration&&a.settingsRevision==b.settingsRevision;
}
float ResolveVkNrWhitePoint(uint32_t source,float manual,float exposure,float pre,float trim,std::optional<float> scan,ExposureGuard::WhitePointHold& held,std::optional<float> automatic){
 if(source==2&&scan&&std::isfinite(*scan)&&*scan>=.01f&&*scan<=4096.f)return *scan;
 if(source==1){float candidate=0;bool supported=std::isfinite(exposure)&&exposure>1e-6f&&std::isfinite(pre)&&pre>0&&
  std::isfinite(trim)&&ExposureGuard::IsSupportedWhitePoint(exposure,pre,std::clamp(trim,.25f,4.f),candidate);
  if(supported||held.HasValue())return held.Resolve(candidate,supported,manual);
  if(automatic&&std::isfinite(*automatic)&&*automatic>=.01f&&*automatic<=4096.f&&std::isfinite(trim)){
   const float derived=*automatic*std::clamp(trim,.25f,4.f);
   if(derived>=.01f&&derived<=4096.f)return std::max(manual,derived);
  }
  return manual;}
 return manual;
}
void VkNrAutomaticWhitePoint::Reset(){configured_=false;sequence_=0;count_=next_=0;value_.reset();observation_={};}
void VkNrAutomaticWhitePoint::Configure(const VkNrFrameContract& frame){
 if(!configured_||!SameVkNrDiagnosticContext(frame_,frame)){Reset();frame_=frame;configured_=true;}
}
bool VkNrAutomaticWhitePoint::ObserveCompleted(const VkNrFrameContract& frame,uint64_t sequence,std::span<const float> tiles){
 observation_={};observation_.tiles=tiles.size();
 if(!configured_||!SameVkNrDiagnosticContext(frame_,frame)){observation_.outcome="context-mismatch";return false;}
 if(sequence<=sequence_){observation_.outcome="old-sequence";return false;}
 sequence_=sequence;
 const auto reject=[&](const char* reason){observation_.outcome=reason;count_=next_=0;return false;};
 if(tiles.size()<16)return reject("too-few-tiles");
 float brightest=0;size_t positive=0;
 for(float v:tiles){
  if(!std::isfinite(v)||v<0||v>65504.f)return reject("invalid-tile");
  brightest=std::max(brightest,v);if(v>1e-6f)++positive;
 }
 observation_.peak=brightest;observation_.positive=positive;
 observation_.percentile=VkNrCalibrationPercentile(tiles);
 if(positive<16)return reject("too-few-positive-tiles");
 // Measure occupancy against the robust scene statistic. A small set of emissive
 // peaks must not veto a well-populated scene whose supported P90 is stable.
 // Sparse lights still fail the same >20% occupancy requirement.
 size_t lit=0;for(float v:tiles)if(v>observation_.percentile*.10f)++lit;
 observation_.lit=lit;
 if(static_cast<float>(lit)/static_cast<float>(tiles.size())<=.20f)return reject("highlight-dominated");
 // Reuse the calibration statistic, not the brightest pixel or a scene-specific divisor.
 const float candidate=observation_.percentile;
 if(candidate<.01f||candidate>4096.f)return reject("percentile-out-of-range");
 history_[next_]=candidate;next_=(next_+1)%history_.size();count_=std::min(count_+1,history_.size());
 observation_.stableSamples=count_;
 if(count_<history_.size()){observation_.outcome="warming";return false;}
 auto stable=history_;std::sort(stable.begin(),stable.end());
 observation_.windowLow=stable.front();observation_.windowHigh=stable.back();
 if(stable.back()/stable.front()>1.25f){observation_.outcome="unstable-window";return false;}
 const float target=(stable[3]+stable[4])*.5f;
 if(!value_)value_=target;
 else {
  // Limit temporal changes in log exposure space; a dark or uncertain frame holds.
  constexpr float maxStops=1.f/60.f;
  const float delta=std::clamp(std::log2(target / *value_),-maxStops,maxStops);
  value_=*value_*std::exp2(delta);
 }
 observation_.outcome="accepted";return true;
}
VulkanNrExposureScan::~VulkanNrExposureScan(){Reset();}
void VulkanNrExposureScan::Reset(){for(const auto& p:pending_)owner_.ReleaseUse(p.use);pending_.clear();value_.reset();white_.reset();published_=0;low_=high_=0;}
void VulkanNrExposureScan::Configure(const VkNrFrameContract& frame,const std::string& anchors,bool inverted,float trim){
 if(!SameVkNrDiagnosticContext(frame_,frame)||serialized_!=anchors||inverted_!=inverted||trim_!=trim){Reset();frame_=frame;serialized_=anchors;anchors_=ParseVkNrAnchors(anchors);inverted_=inverted;trim_=trim;}
}
bool VulkanNrExposureScan::Record(const VkFrameRequest& request,VkNrUseId use){
 if(!use||!SameVkNrDiagnosticContext(frame_,request.contract)||pending_.size()>=256||!owner_.RetainUse(use))return false;
 pending_.push_back({use,++sequence_,{}});return true;
}
bool VulkanNrExposureScan::Readback(VkNrUseId use,float value,bool visible){
 if(!visible)return false;
 if(!std::isfinite(value)||value<=1e-6f||value>=1e4f)value=0;
 for(auto& p:pending_)if(p.use==use){p.value=value;return true;}return false;
}
std::optional<float> VulkanNrExposureScan::CompletedWhitePoint(const VkNrRecordingOwner& owner){
 for(auto it=pending_.begin();it!=pending_.end();){if(owner.GpuComplete(it->use)&&it->value){
   if(it->sequence>published_&&*it->value>0){published_=it->sequence;value_=it->value;low_=low_>0?std::min(low_,*value_):*value_;high_=std::max(high_,*value_);float white=AnchoredVkNrWhitePoint(anchors_,*value_,inverted_,trim_);white_=white>0?std::optional(white):std::nullopt;}
   owner_.ReleaseUse(it->use);it=pending_.erase(it);
  }else ++it;}return white_;
}
}
