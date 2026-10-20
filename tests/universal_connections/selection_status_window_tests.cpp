#include "dlssnr/connections/ConnectionSelection.h"
#include "dlssnr/connections/ConnectionPolicy.h"
#include "dlssnr/PreparedGuideStatusV2.h"
#include "anything/WindowPreferences.h"
#include <cassert>
#include <array>
using namespace DlssNr::Connections;
int main(){
 SetNativeUsable(true);assert(!NeuRotic_ClaimPreparedConnectionV1(3,1,11));
 SetNativeUsable(false);assert(NeuRotic_ClaimPreparedConnectionV1(3,1,11));
 assert(!NeuRotic_ClaimPreparedConnectionV1(3,1,12));assert(!NeuRotic_RetirePreparedConnectionV1(3,12,1));
 assert(NeuRotic_RetirePreparedConnectionV1(3,11,1));assert(NeuRotic_ClaimPreparedConnectionV1(2,1,12));
 assert(!NeuRotic_RetirePreparedConnectionV1(2,12,0));assert(!NeuRotic_RetirePreparedConnectionV1(2,12,1));
 assert(!NeuRotic_ClaimPreparedConnectionV1(3,1,11));

 std::array routes{RouteCapability{Source::BuiltIn,true,true,true,true,true,true,true},RouteCapability{Source::Native,true,true,true,true,false,true,true}};
 auto s=Decide({},routes,{});assert(s.usable&&s.source==Source::Native);
 OwnerState live{Source::BuiltIn,Transport::GPUOnly,true,true,false,false};s=Decide({},routes,live);assert(!s.usable&&s.source==Source::BuiltIn&&s.reason==SelectionReason::UnsafeOwner);
 for(uint32_t source=0;source<5;++source)for(uint32_t transport=0;transport<3;++transport){
  RequestedPolicy p{Source(source),Transport(transport),true};auto first=Decide(p,routes,{});assert(first.source==Decide(p,routes,{}).source);
  if(source==3||source==4)assert(!first.usable);
 }
 routes[0].gpuQualified=false;RequestedPolicy cpu{Source::BuiltIn,Transport::Automatic,true};assert(Decide(cpu,routes,{}).transport==Transport::CPU);
 cpu.allowCpuFallback=false;assert(!Decide(cpu,routes,{}).usable);cpu.transport=Transport::CPU;assert(Decide(cpu,routes,{}).usable);
 routes[0].sameOutputSemantics=false;assert(!Decide(cpu,routes,{}).usable);
 assert(MigrateSource(1,true,true,true)==Source::Native);assert(MigrateSource(0,false,true,true)==Source::BuiltIn);
 using namespace DlssNr::PreparedGuides;
 StatusV2 v;v.updatedTickMs=10;v.session=v.capture=1;v.creationReady=v.guideReady=v.outputValid=1;v.inputFrames=8;v.depthOrigin=Origin::Observed;
 assert(ValidateStatusV2(v,3010).fresh);auto stale=ValidateStatusV2(v,3011);assert(!stale.fresh&&!stale.status.guideReady&&stale.status.inputFrames==8);
 assert(sizeof(ProjectV1(v))==240);assert(ProjectV1(v).depthReady);v.version=3;assert(!ValidateStatusV2(v,10).available);
 nh::NrPreferences p;p.nrScalePercent=25;assert(!nh::MapNrPreferences(p,{}).accepted);auto mapped=nh::MapNrPreferences(p,{true});assert(mapped.accepted&&mapped.options["guides"]=="off");p.requestsDepth=true;assert(!nh::MapNrPreferences(p,{true}).accepted);p.requestsDepth=false;p.transferStrength=5;assert(!nh::MapNrPreferences(p,{true}).accepted);
 auto projected=nh::ProjectWorkerStatus({{"outputMode","overlay"},{"presentCalls",20}});assert(projected["displayObserved"].is_null());
}
