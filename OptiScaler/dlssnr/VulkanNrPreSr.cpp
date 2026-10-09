#include "VulkanNrPreSr.h"
namespace DlssNr
{
bool ShouldRecordVkNrPreSr(const VkNrPreSrAdmission& a)
{ return a.enabled&&a.nativeSelected&&a.beforeRequested&&a.selectedSr&&!a.rayReconstruction&&!a.bridged; }
bool ShouldRecordVkNrAfterSr(const VkNrPreSrAdmission& a)
{ return a.enabled && a.nativeSelected && !a.bridged && (a.rayReconstruction || !a.beforeRequested); }
void VkNrPreSrSeed::Record(VkNrUseId use,bool succeeded)
{
    if(ready_||use_||!succeeded||!owner_.RetainUse(use))return;
    use_=use;
}
bool VkNrPreSrSeed::Ready()
{
    if(use_&&owner_.GpuComplete(use_)) { ready_=true;owner_.ReleaseUse(use_);use_={}; }
    else if(use_&&owner_.Reusable(use_))Reset();
    return ready_;
}
void VkNrPreSrSeed::Reset() { if(use_)owner_.ReleaseUse(use_);use_={};ready_=false; }
}
