#pragma once
#include "../WorkerContracts.h"
#include <d3d11_4.h>
#include <d3d12.h>
#include <json.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <optional>
namespace nrw {
using ProbeJson=nlohmann::json;
inline uint32_t ProbeGridAxis(uint32_t extent,uint32_t i){return std::min(extent-1,((2*i+1)*extent)/32);}
inline uint32_t ProbeGridCount(uint32_t width,uint32_t height){
 if(!width||!height||width>16384||height>16384)return 0;
 uint32_t xs=0,ys=0;for(uint32_t i=0;i<16;++i){xs+=i==0||ProbeGridAxis(width,i)!=ProbeGridAxis(width,i-1);ys+=i==0||ProbeGridAxis(height,i)!=ProbeGridAxis(height,i-1);}return xs*ys;
}
struct ProbeIdentity {
 uint64_t requestId=0;FrameStamp frame;
 uint64_t targetGeneration=0,outputEpoch=0,requestedRevision=0,appliedRevision=0,processingRevision=0,comparisonRevision=0;
 uint32_t workWidth=0,workHeight=0,outputWidth=0,outputHeight=0;
 uint32_t scalePercent=100;float split=0;int stripes=0;
 float transferStrength=1,colourStrength=1;int modelStyle=0,comparisonDirection=0;
 bool nr=true,sr=false,fg=false,sdr=true;
 bool operator==(const ProbeIdentity& b)const{
  if(transferStrength!=b.transferStrength||colourStrength!=b.colourStrength||modelStyle!=b.modelStyle||comparisonDirection!=b.comparisonDirection)return false;
  return requestId==b.requestId&&frame.session==b.frame.session&&frame.sequence==b.frame.sequence&&frame.timestampQpc==b.frame.timestampQpc&&frame.width==b.frame.width&&frame.height==b.frame.height&&frame.reset==b.frame.reset&&frame.streamEpoch==b.frame.streamEpoch&&frame.geometryEpoch==b.frame.geometryEpoch&&frame.configEpoch==b.frame.configEpoch&&targetGeneration==b.targetGeneration&&outputEpoch==b.outputEpoch&&requestedRevision==b.requestedRevision&&appliedRevision==b.appliedRevision&&processingRevision==b.processingRevision&&comparisonRevision==b.comparisonRevision&&workWidth==b.workWidth&&workHeight==b.workHeight&&outputWidth==b.outputWidth&&outputHeight==b.outputHeight&&scalePercent==b.scalePercent&&split==b.split&&stripes==b.stripes&&nr==b.nr&&sr==b.sr&&fg==b.fg&&sdr==b.sdr;
 }
 bool Eligible()const{return requestId&&frame.session&&frame.sequence&&targetGeneration&&outputEpoch&&ProbeGridCount(frame.width,frame.height)&&workWidth==frame.width&&workHeight==frame.height&&outputWidth==frame.width&&outputHeight==frame.height&&scalePercent==100&&split==0&&stripes==0&&nr&&!sr&&!fg&&sdr&&requestedRevision==appliedRevision&&std::isfinite(transferStrength)&&std::isfinite(colourStrength);}
};
enum class ProbeStage:unsigned {Nr,Publication,Comparison};
class ProbeAdmission {
 enum class State {Disabled,Pending,Complete,Unsupported,Invalidated,Failed,Unknown};
 State state=State::Disabled;uint64_t id=0;bool bound=false;ProbeIdentity token;
 unsigned recorded=0,retired=0;std::array<uint64_t,3> required{};std::string reason;
public:
 uint64_t RequestId()const{return id;}
 bool Retired()const{return state!=State::Unknown&&(recorded&~retired)==0;}
 bool Pending()const{return state==State::Pending;}
 bool Quarantined()const{return state==State::Unknown;}
 const ProbeIdentity& Identity()const{return token;}
 bool Request(uint64_t request){if(!request||request<=id||Pending()||!Retired())return false;id=request;state=State::Pending;bound=false;recorded=retired=0;required={};reason.clear();return true;}
 bool Bind(const ProbeIdentity& input){if(!Pending()||bound||input.requestId!=id)return false;if(!input.Eligible()){state=State::Unsupported;reason="Only stable NR-only scale100 SDR equal-extent enhanced-only frames are supported";return false;}token=input;bound=true;return true;}
 bool Matches(const ProbeIdentity& input)const{return Pending()&&bound&&token==input;}
 void Record(ProbeStage stage){if(Pending()&&bound)recorded|=1u<<unsigned(stage);}
 void Require(ProbeStage stage,uint64_t value){auto index=unsigned(stage);if(!(recorded&(1u<<index))||!value||(required[index]&&required[index]!=value)){Unknown("Diagnostic last-use identity is unproved");return;}required[index]=value;}
 bool Observe(ProbeStage stage,uint64_t completed){if(Quarantined())return false;const auto index=unsigned(stage);if(completed==UINT64_MAX){Unknown("Device loss leaves diagnostic last use unproved");return false;}if(!required[index]||completed<required[index])return false;retired|=1u<<index;return true;}
 void Invalidate(const char* why){if(state!=State::Unknown){state=State::Invalidated;reason=why;}}
 void Fail(const char* why){if(state!=State::Unknown){state=State::Failed;reason=why;}}
 void Unsupported(const char* why){if(state!=State::Unknown){state=State::Unsupported;reason=why;}}
 void Unknown(const char* why){state=State::Unknown;reason=why;}
 bool Complete(){if(!Pending()||!bound||recorded!=7||retired!=7)return false;state=State::Complete;return true;}
 ProbeJson Status()const{
  static constexpr const char* names[]={"disabled","pending","complete","unsupported","invalidated","failed","failed/unknown"};
  return {{"state",names[unsigned(state)]},{"requestId",id},{"reason",reason},{"ownershipRetired",Retired()},{"summaries",nullptr}};
 }
};
struct ProbeRecord {std::array<uint32_t,32> words{};};
static_assert(sizeof(ProbeRecord)==128);
inline std::optional<ProbeJson> DecodeProbeRecord(const ProbeRecord& record,const ProbeIdentity& id,unsigned boundary){
 const auto& w=record.words;const uint32_t samples=ProbeGridCount(id.frame.width,id.frame.height);
 if(boundary>3||!samples||w[0]!=uint32_t(id.requestId)||w[1]!=uint32_t(id.requestId>>32)||w[2]!=uint32_t(id.frame.session)||w[3]!=uint32_t(id.frame.session>>32)||w[4]!=uint32_t(id.frame.sequence)||w[5]!=uint32_t(id.frame.sequence>>32)||w[6]!=id.frame.width||w[7]!=id.frame.height||w[24]!=boundary||w[25]!=samples)return {};
 if(w[8]>3*samples||w[9]>3*samples||w[8]+w[9]!=3*samples||w[10]>samples||w[10]>w[8]/3||w[11]>w[10]||w[12]>w[10]||w[11]+w[12]!=w[10]||w[13]>w[12]||w[16]>samples||w[17]>samples||w[16]+w[17]!=samples||w[18]>w[16]||w[19]>w[16]||w[18]+w[19]!=w[16]||w[22]>w[23]||w[23]>samples||w[26]>samples||w[29]>samples||w[27]>w[28]||w[28]>w[26]||w[30]>w[31]||w[31]>w[29]||w[16]>std::min(w[26],w[29]))return {};
 const auto value=[&](unsigned i){return std::bit_cast<float>(w[i]);};
 for(unsigned i:{14u,15u,20u,21u})if(!std::isfinite(value(i))||value(i)<0)return {};
 if(w[10]<samples-std::min(samples,w[9])||w[16]<w[26]+w[29]-std::min(samples,w[26]+w[29])||(boundary==0?w[23]<w[10]:(w[22]!=0||w[23]!=0)))return {};
 // Up to 768 rounded float32 additions. This conservative relative tolerance
 // applies to sums only; identity/zero denominators must contain exact zero.
 const double domain=boundary==0?131008.:1.;
 const auto consistent=[&](unsigned maximum,unsigned sum,uint32_t finite,uint32_t unequal,bool noPartial){
  const double m=value(maximum),s=value(sum),tolerance=std::max(1e-6,std::max(s,m*finite)*.0005);
  if(m>domain||s>domain*finite+tolerance||s+tolerance<m||s>m*finite+tolerance)return false;
  if(!finite&&(m!=0||s!=0))return false;
  if((m==0)!=(s==0)||((m==0||s==0)&&unequal>0))return false;
  if(noPartial&&unequal==0&&(m!=0||s!=0))return false;
  return true;
 };
 if(!consistent(14,15,w[8],w[12],w[8]==3*w[10])||!consistent(20,21,w[16],w[19],true))return {};
 if(w[13]&&(boundary==0?value(14)<=1e-5f:value(14)+1e-6f<2.f/255.f))return {};
 const auto metric=[&](unsigned index,uint32_t denominator,bool mean){return denominator?ProbeJson(mean?double(value(index))/denominator:double(value(index))):ProbeJson(nullptr);};
 const auto count=[&](uint32_t value,uint32_t denominator){return denominator?ProbeJson(value):ProbeJson(nullptr);};
 return ProbeJson{{"uniqueSamples",samples},{"units",boundary==0?"raw FP16 numeric values":"normalized UNORM values"},{"threshold",boundary==0?1e-5:1./255.},
  {"rgb",{{"finiteChannels",w[8]},{"nonfiniteChannels",w[9]},{"finitePixels",w[10]},{"equalPixels",count(w[11],w[10])},{"unequalPixels",count(w[12],w[10])},{"aboveThresholdPixels",count(w[13],w[10])},{"maximumAbsoluteDifference",metric(14,w[8],false)},{"meanAbsoluteDifference",metric(15,w[8],true)}}},
  {"alpha",{{"finitePairedSamples",w[16]},{"nonfinitePairedComparisons",w[17]},{"equalSamples",count(w[18],w[16])},{"unequalSamples",count(w[19],w[16])},{"maximumAbsoluteDifference",metric(20,w[16],false)},{"meanAbsoluteDifference",metric(21,w[16],true)},
   {"lhs",{{"finite",w[26]},{"zero",count(w[27],w[26])},{"notOne",count(w[28],w[26])}}},{"rhs",{{"finite",w[29]},{"zero",count(w[30],w[29])},{"notOne",count(w[31],w[29])}}}}},
  {"fallback",boundary==0?ProbeJson{{"finiteModelRgbSamples",w[23]},{"lowModelLuminanceSamples",count(w[22],w[23])},{"predicate","dot(model.rgb,[0.2126,0.7152,0.0722]) <= 1e-5; passthrough=1"}}:ProbeJson(nullptr)}};
}
// One reusable fixed-budget GPU owner. The implementation retains exact fences
// and immutable references; failed/unknown GPU ownership is never recycled.
class FrameProbe {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 FrameProbe();~FrameProbe();FrameProbe(const FrameProbe&)=delete;
 bool Request(uint64_t);bool Bind(const ProbeIdentity&);bool Pending()const;bool Busy()const;bool Quarantined()const;
 uint64_t RequestId()const;const ProbeIdentity& Identity()const;
 void Invalidate(const char*);void Refuse(const char*);void Unknown(const char*);
 bool CaptureConstants(const std::array<uint32_t,24>&);
 bool RecordNr(ID3D12Device*,ID3D11Device*,ID3D11DeviceContext*,ID3D12GraphicsCommandList*,ID3D12Resource* model,ID3D12Resource* proxy,ID3D12Resource* resolved,ID3D12Resource* original);
 void CompleteNr(ID3D12Fence*,uint64_t);
 bool RecordPublication(ID3D11DeviceContext*,ID3D11Texture2D*,ID3D11ShaderResourceView* resolved);
 void CompletePublication(ID3D11Fence*,uint64_t);
 bool RecordComparison(ID3D11DeviceContext*,ID3D11Texture2D* backbuffer,ID3D11Texture2D* published,uint64_t outputEpoch,int alphaMode,bool overlay);
 void RequireComparison(ID3D11Fence*,uint64_t);void ObserveComparison(uint64_t);
 void Poll();ProbeJson Status()const;
};
}
