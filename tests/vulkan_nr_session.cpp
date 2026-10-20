#include "../OptiScaler/dlssnr/VulkanNrSession.h"
#include "../OptiScaler/dlssnr/VulkanNrRuntime.h"

#include <cstdio>
#include <cstdlib>

using namespace DlssNr;

static void Check(bool condition, const char* name)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}

int main()
{
    VulkanNrRuntime runtime;
    int inits = 0;
    const auto device = reinterpret_cast<VkDevice>(0x101);
    Check(runtime.EnsureDevice(device, [&] { ++inits; return true; }), "first device init");
    Check(runtime.EnsureDevice(device, [&] { ++inits; return true; }), "same device reuse");
    Check(inits == 1, "one shared device init");
    Check(ClassifyVkRouteDevice(VK_NULL_HANDLE, runtime.Device(), device) == VkRouteDeviceUse::FirstUse,
          "Present-first runtime lets Native attach to the same live device");
    Check(ClassifyVkRouteDevice(device, runtime.Device(), device) == VkRouteDeviceUse::Reuse,
          "same route device is reused");
    Check(ClassifyVkRouteDevice(VK_NULL_HANDLE, runtime.Device(), reinterpret_cast<VkDevice>(0x102)) ==
              VkRouteDeviceUse::Conflict,
          "uninitialized route does not attach to another live device");

    VulkanNrSession native(VkNrRoute::Native, runtime);
    VulkanNrSession present(VkNrRoute::Present, runtime);
    const VkFrameRequest request {};

    runtime.Activate(VkNrRoute::Native);
    auto n = native.Record(request, [](const VkFrameRequest&, bool reset) {
        Check(reset, "first native record requests reset");
        return VkRecordResult { VkRecordStatus::Complete, "", true, true };
    });
    Check(n.status == VkRecordStatus::Complete && native.RecordedCount() == 1 &&
              native.CompletedCount() == 0 && present.RecordedCount() == 0,
          "native recording is not GPU completion and does not touch Present");

    Check(VkNrNativeDeliveryAccepted(n,false,false,true),"current complete AfterSR recording accepted");
    Check(!VkNrNativeDeliveryAccepted(n,false,false,false),"later SR failure revokes old successful delivery");
    Check(!VkNrNativeDeliveryAccepted(n,true,false,true),"unbound Performance carrier cannot claim delivery");
    Check(VkNrNativeDeliveryAccepted(n,true,true,true),"bound successful Performance carrier accepted");
    Check(!VkNrNativeDeliveryAccepted({VkRecordStatus::NoWork,"refused"},false,false,true),"later refusal revokes old successful delivery");
    Check(!VkNrNativeDeliveryAccepted({VkRecordStatus::Partial,"output refused",true,false},false,false,true),"model recording alone is not delivery");
    runtime.Activate(VkNrRoute::Present);
    VkFrameRequest guided;
    unsigned bindCalls=0,modelCalls=0;
    guided.bindGuides=[&](VkNrUseId){++bindCalls;return false;};
    auto refusedGuides=present.Record(guided,[&](const VkFrameRequest&,bool){
        ++modelCalls;return VkRecordResult{VkRecordStatus::Complete,"",true,true};
    });
    Check(refusedGuides.status==VkRecordStatus::NoWork&&bindCalls==1&&modelCalls==0,
        "failed Enhanced binding cannot run model or retry image-only");
    auto p = present.Record(request, [](const VkFrameRequest&, bool reset) {
        Check(reset, "first Present record requests reset");
        return VkRecordResult { VkRecordStatus::Partial, "model recorded but output not sealed", true, false };
    });
    Check(p.status == VkRecordStatus::Partial && present.RecordedCount() == 0 &&
              present.CompletedCount() == 0 && native.RecordedCount() == 1,
          "partial Present does not become complete or overwrite Native history");

    runtime.Activate(VkNrRoute::Native);
    n = native.Record(request, [](const VkFrameRequest&, bool reset) {
        Check(reset, "returning to Native requests reset");
        return VkRecordResult { VkRecordStatus::Complete, "", true, true };
    });
    Check(n.status == VkRecordStatus::Complete && native.RecordedCount() == 2,
          "Native history records independently after route switch");
    native.ObserveCompleted();
    Check(native.CompletedCount() == 1 && present.CompletedCount() == 0,
          "Native GPU completion proof does not complete Present work");

    runtime.Activate(VkNrRoute::Present);
    p = present.Record(request, [](const VkFrameRequest&, bool reset) {
        Check(reset, "returning to Present requests reset");
        return VkRecordResult { VkRecordStatus::NoWork, "refused", false, false };
    });
    Check(p.status == VkRecordStatus::NoWork && present.RecordedCount() == 0,
          "failed Present record cannot be reported complete");

    p = present.Record(request, [](const VkFrameRequest&, bool reset) {
        Check(reset, "refused record keeps Present reset pending");
        return VkRecordResult { VkRecordStatus::Complete, "", true, true };
    });
    Check(p.status == VkRecordStatus::Complete && present.RecordedCount() == 1,
          "sealed Present work counted as recorded");
    auto pending = present.Record(request, [](const VkFrameRequest&,bool) {
        return VkRecordResult { VkRecordStatus::NoWork,"generation pending",false,false };
    });
    present.AbandonLatestRecording(pending.status == VkRecordStatus::Complete);
    Check(present.RecordedCount() == 1,"discarded pending replacement cannot revoke an earlier accepted frame");
    present.AbandonLatestRecording();
    Check(present.RecordedCount() == 0 && present.ResetPending(),
          "discarded Present work revokes its count and history");

    present.Record(request, [](const VkFrameRequest&, bool) {
        return VkRecordResult { VkRecordStatus::Complete, "", true, true };
    });
    const auto presentEpoch = runtime.Epoch();
    native.Record(request, [](const VkFrameRequest&, bool) {
        return VkRecordResult { VkRecordStatus::NoWork, "Native route not selected", false, false };
    });
    Check(runtime.Epoch() == presentEpoch, "inactive callback preserves active Present epoch");
    present.Record(request, [](const VkFrameRequest&, bool reset) {
        Check(!reset, "inactive Native callback does not reset Present history");
        return VkRecordResult { VkRecordStatus::Complete, "", true, true };
    });
    present.Record(request, [](const VkFrameRequest&, bool) {
        return VkRecordResult{VkRecordStatus::NoWork,"selected frame resource pressure"};
    });
    present.ObserveCompleted(); // Older completion must not acknowledge this gap.
    Check(present.ResetPending(),"selected skipped frame leaves reset pending");
    present.Record(request, [](const VkFrameRequest&, bool reset) {
        Check(reset,"first successful recording after selected gap resets history");
        return VkRecordResult{VkRecordStatus::Complete,"",true,true};
    });
    try { present.Record(request, [](const VkFrameRequest&,bool)->VkRecordResult { throw 1; }); }
    catch(int) {}
    Check(present.ResetPending(),"throwing recorder cannot lose temporal discontinuity");
    present.Record(request, [](const VkFrameRequest& frame,bool) {
        Check(bool(frame.waitBudget),"one evaluation owns a shared wait budget");
        const auto start=VkNrWaitBudget::Clock::now();
        VkNrWaitBudget budget(start);
        Check(budget.RemainingNs(start)==100000000ull,"evaluation wait allowance is 100ms total");
        Check(budget.RemainingNs(start+std::chrono::milliseconds(90))==10000000ull,"later pass gets only unspent allowance");
        Check(budget.RemainingNs(start+std::chrono::milliseconds(101))==0,"ten passes cannot multiply wait allowance");
        return VkRecordResult{VkRecordStatus::Complete,"",true,true};
    });
    present.Record(request,[&](const VkFrameRequest&,bool) {
        present.RequestReset();
        return VkRecordResult{VkRecordStatus::Complete,"",true,true};
    });
    Check(present.ResetPending(),"successful older record cannot acknowledge a newer reset request");
    present.Record(request,[](const VkFrameRequest&,bool reset) {
        Check(reset,"next live session recording receives the newer pending reset");
        return VkRecordResult{VkRecordStatus::Complete,"",true,true};
    });
    Check(!present.ResetPending(),"current successful recording acknowledges its own reset");
    present.Record(request,[](const VkFrameRequest&,bool reset) {
        Check(!reset,"subsequent live session recording does not repeat an acknowledged reset");
        return VkRecordResult{VkRecordStatus::Complete,"",true,true};
    });
    std::puts("PASS: shared device init, independent route history, reset and recording truth");
}
