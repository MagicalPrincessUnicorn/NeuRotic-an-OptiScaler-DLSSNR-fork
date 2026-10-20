#include "../OptiScaler/dlssnr/VulkanNrProviderCaps.h"
#include "../OptiScaler/mfg/MfgControl.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool v,const char* s){if(!v){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(){
 VkNrProviderObservation o;o.kind=VkNrProviderKind::Streamline;o.providerIdentity="mapped module/export receipt";o.apiVersion=0x020e01;o.maxGenerated=3;
 o.vulkanApi=o.exportsQualified=o.stateSucceeded=o.consumerContractObserved=true;o.dynamicReported=true;
 auto q=QualifyVkNrProvider(o);Check(q.vulkanFg&&q.fixedMfg&&!q.dynamicMfg,"dynamic MFG unavailable on Vulkan despite reported flag");
 Check(!VkNrProviderRatio(q,4)&&VkNrProviderRatio(q,3),"ratio above actual ceiling refused");
 o.adaProviderQualified=true;Check(!QualifyVkNrProvider(o).adaQualified,"D3D12 Ada guard not blindly removed");
 o.kind=VkNrProviderKind::Ffx;o.apiVersion=0x020300;Check(!QualifyVkNrProvider(o).vulkanFg,"FFX 2.3 Vulkan rejected");
 o.apiVersion=0x010104;o.consumerContractObserved=false;Check(!QualifyVkNrProvider(o).vulkanFg,"older FFX requires actual consumer contract");
 o.consumerContractObserved=true;Check(QualifyVkNrProvider(o).vulkanFg,"qualified older FFX independent capability");
 using namespace Neurotic::Mfg;
 Check(ResolveMfgSelection(MfgSelection::X4,true,1,false,7,7).status==MfgDecisionStatus::Unavailable,"donor Ada exact publication required");
 Check(AdvertisedMfgGeneratedMax(1,true,false)==1,"no capability mutation without publication");
 std::puts("versioned Vulkan fixed ratios, dynamic refusal and exact Ada publication policy verified");
}
