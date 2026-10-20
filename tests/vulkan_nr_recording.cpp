#include "../OptiScaler/dlssnr/VulkanNrRecording.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
using namespace DlssNr;
static void Check(bool result, const char* message) { if (!result) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
int main()
{
    VkNrRecordingOwner owner;
    auto device = reinterpret_cast<VkDevice>(0x1);
    auto pool = reinterpret_cast<VkCommandPool>(0x2);
    auto secondary = reinterpret_cast<VkCommandBuffer>(0x3);
    auto a = reinterpret_cast<VkCommandBuffer>(0x4), b = reinterpret_cast<VkCommandBuffer>(0x5);
    owner.OnAllocate(device, pool, secondary, 0, 0, VK_COMMAND_BUFFER_LEVEL_SECONDARY);
    owner.OnAllocate(device, pool, a, 0); owner.OnAllocate(device, pool, b, 0);
    Check(!owner.Reserve(a, 1), "allocated without begin is not recordable");
    owner.OnBegin(secondary, 0, VK_SUCCESS);
    const auto use = owner.Reserve(secondary, 1);
    Check(use.has_value(), "observed recording reserves use");
    const auto incarnation = owner.Incarnation(secondary);
    owner.OnBegin(secondary, 0, VK_ERROR_OUT_OF_HOST_MEMORY);
    owner.OnEnd(secondary, VK_ERROR_OUT_OF_HOST_MEMORY);
    owner.OnReset(secondary, VK_ERROR_OUT_OF_HOST_MEMORY);
    Check(owner.Incarnation(secondary) == incarnation && !owner.RecordingReleased(*use), "failed begin/end/reset preserve incarnation and vendor use");
    Check(!owner.SnapshotUses(secondary), "failed end does not seal recording");
    owner.OnEnd(secondary, VK_SUCCESS);
    for (auto primary : {a, b}) { owner.OnBegin(primary, 0, VK_SUCCESS); owner.OnExecute(primary, {&secondary, 1}); owner.OnEnd(primary, VK_SUCCESS); }
    Check(owner.SnapshotUses(a)->size() == 1 && owner.SnapshotUses(b)->at(0) == *use, "both primaries retain actual secondary uses");
    owner.OnReset(a, VK_SUCCESS); owner.OnResetPool(device,pool, VK_ERROR_OUT_OF_HOST_MEMORY);
    Check(!owner.RecordingReleased(*use) && owner.SnapshotUses(b).has_value(), "surviving secondary and failed pool reset retain use");
    owner.OnBegin(a, 0, VK_SUCCESS);
    owner.OnRendering(a,true);
    Check(!owner.Reserve(a,3),"active traditional or dynamic rendering forbids compute injection");
    owner.OnRendering(a,false);
    owner.OnRendering(a,true,VK_RENDERING_SUSPENDING_BIT);owner.OnRendering(a,false);
    Check(!owner.Reserve(a,3),"suspended rendering cannot admit synchronization or compute work before resume");
    owner.OnRendering(a,true,VK_RENDERING_RESUMING_BIT);owner.OnRendering(a,false);
    auto failedVendorUse = owner.Reserve(a, 3);
    Check(failedVendorUse.has_value() && !owner.RecordingReleased(*failedVendorUse), "unsealed vendor work remains held");
    owner.OnReset(a, VK_SUCCESS);
    Check(owner.RecordingReleased(*failedVendorUse), "actual successful reset releases failed vendor recording");
    owner.OnReset(secondary, VK_SUCCESS);
    Check(owner.RecordingReleased(*use) && !owner.SnapshotUses(b), "reset secondary invalidates parent incarnation");
    owner.OnBegin(secondary, 0, VK_SUCCESS); auto newUse = owner.Reserve(secondary, 1); owner.OnEnd(secondary, VK_SUCCESS);
    Check(newUse && *newUse != *use && !owner.SnapshotUses(b), "re-recorded secondary cannot become old parent's work");
    int observed = 0;
    { VkNrObservationScope exportScope(VkNrObservation::Begin, 3);
      { VkNrObservationScope procScope(VkNrObservation::Begin, 3); if (procScope.Observe()) ++observed; }
      if (exportScope.Observe()) ++observed; }
    Check(observed == 1, "export/proc alias observed once");
    owner.OnResetPool(device,pool, VK_SUCCESS); Check(owner.RecordingReleased(*newUse), "successful pool reset releases uses");
    owner.OnAllocate(device, pool, a, 0, VK_COMMAND_POOL_CREATE_PROTECTED_BIT); owner.OnBegin(a, 0, VK_SUCCESS);
    Check(!owner.Reserve(a, 1) && !owner.Reserve(reinterpret_cast<VkCommandBuffer>(0x55), 1), "unknown and protected refused");
    owner.OnDestroyPool(device,pool); Check(owner.Incarnation(a) == 0, "destroy pool releases identities");
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_PROTECTED_BIT; poolInfo.queueFamilyIndex = 2;
    owner.OnCreatePool(device, pool, poolInfo, VK_SUCCESS);
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = pool; allocation.commandBufferCount = 1;
    owner.OnAllocateBuffers(device, allocation, &a, VK_SUCCESS); owner.OnBegin(a, 0, VK_SUCCESS);
    Check(!owner.Reserve(a, 1), "actual observed protected pool flags reach reservation");
    const auto active = owner.Incarnation(a);
    owner.OnAllocateBuffers(device, allocation, &a, VK_ERROR_OUT_OF_HOST_MEMORY);
    Check(owner.Incarnation(a) == active, "failed allocation preserves existing identity");
    for (const auto* file : {"OptiScaler/hooks/Vulkan_Hooks.cpp", "OptiScaler/hooks/VulkanwDx12_Hooks.cpp"}) {
        std::ifstream input(file); const std::string source{std::istreambuf_iterator<char>(input), {}};
        Check(source.find(".OnBegin(commandBuffer, pBeginInfo->flags, result)") != std::string::npos ||
              source.find(".OnBegin(buffer, info->flags, result)") != std::string::npos, "both dispatch chains observe successful returned begin");
        Check(source.find(".OnResetPool(device, commandPool, result)") != std::string::npos ||
              source.find(".OnResetPool(device, commandPool,result)") != std::string::npos ||
              source.find(".OnResetPool(device, pool, result)") != std::string::npos, "both dispatch chains observe returned pool reset");
        Check(source.find("VkNrObservationScope") != std::string::npos, "both dispatch chains de-duplicate aliases");
    }
    owner.OnAllocate(device,pool,a,0);owner.OnBegin(a,0,VK_SUCCESS);
    owner.OnUnsupportedBindings(a);
    Check(!owner.Reserve(a,1)&&owner.ReserveCopy(a,1).has_value(),"copy preserves untracked shader bindings while compute remains refused");
    owner.OnRendering(a,true);
    Check(!owner.ReserveCopy(a,1),"copy remains forbidden inside rendering");
    owner.OnRendering(a,false);owner.OnEnd(a,VK_SUCCESS);
    Check(!owner.ReserveCopy(a,1),"copy requires active recording");
    owner.OnAllocate(device,pool,a,0,VK_COMMAND_POOL_CREATE_PROTECTED_BIT);owner.OnBegin(a,0,VK_SUCCESS);
    Check(!owner.ReserveCopy(a,1),"copy cannot bypass protected recording");
    Check(!owner.ReserveCopy(reinterpret_cast<VkCommandBuffer>(0x555),1),"copy cannot invent command ownership");
    {
        VkNrRecordingOwner partial;auto opaque=reinterpret_cast<VkCommandBuffer>(0x999);
        partial.OnAllocate(device,pool,a,0);partial.OnBegin(a,0,VK_SUCCESS);
        const auto direct=*partial.ReserveCopy(a,1);
        partial.OnAllocate(device,pool,secondary,0,0,VK_COMMAND_BUFFER_LEVEL_SECONDARY);
        partial.OnBegin(secondary,0,VK_SUCCESS);const auto childUse=*partial.ReserveCopy(secondary,1);
        partial.OnEnd(secondary,VK_SUCCESS);
        partial.OnExecute(a,{&opaque,1});partial.OnExecute(a,{&secondary,1});
        Check(!partial.ReserveCopy(a,1),"opaque child does not grant new recording admission");
        Check(!partial.SnapshotSubmissionUses(a),"unsealed primary cannot gain submission proof");
        partial.OnEnd(a,VK_SUCCESS);
        Check(!partial.SnapshotUses(a),"opaque secondary still refuses complete graph qualification");
        auto submittedUses=partial.SnapshotSubmissionUses(a);
        Check(submittedUses&&submittedUses->size()==2&&submittedUses->at(0)==direct&&submittedUses->at(1)==childUse,
            "opaque sibling cannot erase observed direct and valid secondary work");
        const auto childTrace=partial.DescribeProducer(childUse);
        Check(childTrace.find("level=secondary")!=std::string::npos&&
            childTrace.find("queries=2 graph=accepted")!=std::string::npos&&childTrace.find("observedParentCount=1 parentScanTruncated=0")!=std::string::npos,
            "secondary trace distinguishes its observed submitted parent from a missing direct submission");
        Check(!partial.ProducerProof(childUse),"snapshot tracing alone never grants producer proof");
        partial.OnReset(secondary,VK_SUCCESS);
        Check(!partial.SnapshotSubmissionUses(a),"reset known secondary invalidates whole primary submission snapshot");
        Check(partial.DescribeProducer(direct).find("directGraph=not_executable")!=std::string::npos,
            "rejected root trace identifies the child graph failure");
        Check(!partial.SnapshotSubmissionUses(opaque)&&partial.DescribeProducer(direct).find("unknownRoots=1")!=std::string::npos,
            "untracked submitted roots are counted without inventing command ownership");
        for(uintptr_t index=0;index<4200;++index)partial.OnAllocate(device,pool,reinterpret_cast<VkCommandBuffer>(0x10000+index),0);
        Check(partial.DescribeProducer(direct).find("parentScanTruncated=1")!=std::string::npos,
            "large recording registries truncate parent scans rather than stall submission under the owner lock");
        partial.OnBegin(secondary,0,VK_SUCCESS);partial.ReserveCopy(secondary,1);partial.OnEnd(secondary,VK_SUCCESS);
        Check(!partial.SnapshotSubmissionUses(a),"new child incarnation never substitutes for original link");
        partial.OnBegin(a,0,VK_SUCCESS);partial.ReserveCopy(a,1);
        partial.OnAllocate(device,pool,b,1,0,VK_COMMAND_BUFFER_LEVEL_SECONDARY);
        partial.OnBegin(b,0,VK_SUCCESS);partial.OnEnd(b,VK_SUCCESS);partial.OnExecute(a,{&b,1});partial.OnEnd(a,VK_SUCCESS);
        Check(!partial.SnapshotSubmissionUses(a),"known wrong-family child remains invalid rather than opaque");
        partial.OnBegin(a,0,VK_SUCCESS);partial.ReserveCopy(a,1);
        partial.OnBegin(secondary,0,VK_SUCCESS);partial.OnExecute(a,{&secondary,1});partial.OnEnd(a,VK_SUCCESS);
        Check(!partial.SnapshotSubmissionUses(a),"known unsealed child remains invalid rather than opaque");
        partial.OnEnd(secondary,VK_SUCCESS);partial.OnBegin(a,0,VK_SUCCESS);partial.ReserveCopy(a,1);
        partial.OnExecute(a,{&secondary,1});partial.OnEnd(a,VK_SUCCESS);partial.OnFree(secondary);
        Check(!partial.SnapshotSubmissionUses(a),"freed known child cannot become an opaque sibling");
    }
    {
        VkNrRecordingOwner traced;
        traced.OnAllocate(device,pool,a,0);traced.OnBegin(a,0,VK_SUCCESS);
        auto late=*traced.ReserveCopy(a,1);
        { VkNrObservationScope outer(VkNrObservation::Submit,1);
          VkNrObservationScope inner(VkNrObservation::Submit2,1);
          Check(traced.TraceGuideCapture(late),"first guide capture trace admitted"); }
        traced.OnEnd(a,VK_SUCCESS);traced.TraceGuidePresent(late);
        const auto beforeSubmit=traced.ProducerStates({&late,1});
        Check(!traced.ProducerProof(late),"Present trace grants no producer rights");
        auto roots=*traced.SnapshotSubmissionUses(a);
        auto submit=*traced.PrepareSubmission({reinterpret_cast<VkQueue>(6),1,roots});
        traced.SubmissionReturned(submit,VK_SUCCESS);
        traced.OnReset(a,VK_SUCCESS);
        const auto afterSubmit=traced.ProducerStates({&late,1});
        Check(!beforeSubmit[0].proof&&!beforeSubmit[0].recordingReleased&&
              afterSubmit[0].proof&&afterSubmit[0].recordingReleased,
              "atomic source snapshot cannot confuse later completion with abandonment at entry");
        auto trace=traced.GuideTraces()[0];
        Check(trace.capture< trace.end && trace.end<trace.present && trace.present<trace.root &&
              trace.root<trace.returned && trace.returned<trace.closed && trace.submitDepth==2 &&
              trace.result==VK_SUCCESS && std::string(trace.closeReason)=="reset",
              "immutable source trace distinguishes submission after Present from absent submission");
        traced.CheckpointReturned(submit,VK_SUCCESS);traced.ObserveFence(submit,VK_SUCCESS);
        Check(traced.Reusable(late),"diagnostic trace does not pin source resources");
        traced.OnBegin(a,0,VK_SUCCESS);auto never=*traced.ReserveCopy(a,1);
        traced.TraceGuideCapture(never);traced.TraceGuidePresent(never);traced.OnReset(a,VK_SUCCESS);
        trace=traced.GuideTraces()[1];
        Check(trace.closed>trace.present && !trace.root && !trace.returned,
              "reset without any observed submission is distinct from late submission");
        traced.OnBegin(a,0,VK_SUCCESS);auto failed=*traced.ReserveCopy(a,1);traced.TraceGuideCapture(failed);
        traced.OnEnd(a,VK_SUCCESS);traced.SnapshotSubmissionUses(a);
        traced.UntrackedSubmission({&failed,1},VK_ERROR_OUT_OF_HOST_MEMORY);
        trace=traced.GuideTraces()[2];
        Check(trace.returned>trace.root && trace.result==VK_ERROR_OUT_OF_HOST_MEMORY &&
              std::string(trace.returnPath)=="untracked" && !traced.ProducerProof(failed),
              "untracked failed return is recorded without granting producer proof");
        for(int n=0;n<30;++n){traced.OnBegin(a,0,VK_SUCCESS);auto u=*traced.ReserveCopy(a,1);traced.TraceGuideCapture(u);}
        const auto full=traced.GuideTraces();
        Check(full.size()==16 && full[0].use==late.value && full[1].use==never.value,
              "fixed trace survives use cleanup without recycling identities");
        traced.OnDeviceDestroyed(device);
        Check(traced.GuideTraces().back().closed!=0,"device destruction closes remaining trace");
    }
    {
        VkNrRecordingOwner profiles;
        profiles.OnAllocate(device,pool,a,2);
        profiles.OnBegin(a,VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,VK_SUCCESS);
        Check(!profiles.ReserveModel(a,1),"simultaneous replay refused before private model recording");
        profiles.OnBegin(a,0,VK_SUCCESS);
        Check(!profiles.ReserveModel(a,1),"sequential replay needs a qualified private provider profile");
        profiles.OnBegin(a,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
        Check(profiles.ReserveModel(a,1).has_value(),"single-use primary remains recordable without predicting queue");
        profiles.OnAllocate(device,pool,secondary,2,0,VK_COMMAND_BUFFER_LEVEL_SECONDARY);
        profiles.OnBegin(secondary,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
        Check(!profiles.ReserveModel(secondary,1),"secondary private model execution unqualified before insertion");
    }
    std::puts("recording incarnations, failures, secondary references, alias observations and bounded guide lifetime verified");
}
