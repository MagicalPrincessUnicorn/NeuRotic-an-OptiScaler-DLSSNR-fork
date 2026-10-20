#include "../OptiScaler/nr/diagnostics/capability/CapabilityNgxObservation.h"
#include "../OptiScaler/dlssnr/NativeParameterOverride.h"
#include "../OptiScaler/nr/diagnostics/capability/CapabilityQuery.h"
#include <cassert>
#include <iostream>
using namespace DlssNr::Capability;
static RequirementsEnvelope observed;
static void RequirementSpy(const RequirementsEnvelope& value) noexcept { observed=value; }
static unsigned readbacks=0;
static ParameterEnvelope lastParameter;
static void ReadbackSpy(const ParameterEnvelope& value) noexcept { assert(value.synthetic||value.phase.View()=="evaluation"); lastParameter=value; ++readbacks; }
struct Requirement { unsigned FeatureSupported=3,MinHWArchitecture=4; };
struct Parameters {
    void* binding=reinterpret_cast<void*>(1); unsigned reads=0,writes=0; bool fail=false,failAfterWrite=false;
    int Get(const char*,void** out) { ++reads; if(fail) return 1; *out=binding; return 0; }
    void Set(const char*,void* value) { ++writes; binding=value; if(failAfterWrite) fail=true; }
};
int main()
{
    SetRequirementsObserver(RequirementSpy); SetParameterObserver(ReadbackSpy);
    Requirement output; unsigned calls=0; auto vendor=[&] { ++calls; return 0u; };
    const auto result=vendor(); CaptureRequirementsResult(GraphicsPath::D3D12,18,result,0u,&output,RequirementsOrigin::Raw);
    assert(calls==1&&result==0&&observed.outputDefined&&observed.supported==3&&observed.minimumArchitecture==4);
    CaptureRequirementsResult(GraphicsPath::Vulkan,18,1u,0u,reinterpret_cast<Requirement*>(1),RequirementsOrigin::Raw);
    assert(!observed.outputDefined&&observed.supported==0); // failure never dereferences undefined output
    CaptureRequirementsResult(GraphicsPath::D3D11,1,0u,0u,&output,RequirementsOrigin::Effective);
    assert(observed.origin==RequirementsOrigin::Effective);
    Parameters parameters;
    { DlssNr::NativeParameterOverride<Parameters,int> override(&parameters,"DLSS.Input.Color",parameters.binding,0);
      assert(override.Ready()); assert(override.Bind(reinterpret_cast<void*>(2))); assert(override.Restore()); }
    assert(parameters.reads==4&&parameters.writes==2&&readbacks==4);
    Parameters failing; failing.failAfterWrite=true;
    { DlssNr::NativeParameterOverride<Parameters,int> override(&failing,"DLSS.Input.Color",failing.binding,0);
      assert(!override.Bind(reinterpret_cast<void*>(2))); assert(!override.Restore()); }
    assert(failing.reads==4&&failing.writes==2&&lastParameter.possibleWrite&&!lastParameter.readbackSucceeded);
    CaptureSyntheticDefaults(); assert(lastParameter.synthetic);
    Store store("session"); AdapterFactory factory(store); auto port=factory.RegisterWriter(ProducerId::Ngx,"ngx").port;
    auto sequence=ReserveSample(*port); observed.sequence=sequence.sequence;
    auto batch=AdaptRequirements(*port,observed); assert(TryPublish(*port,sequence.sequence,batch)==PublishResult::Published);
    auto snapshot=store.Capture({}); QueryRequest q; q.session="session"; q.subject="ngx-D3D11";
    q.capability=CapabilityId::VendorFeatureSupport; assert(Query(*snapshot.snapshot,q).resolution==Resolution::Unknown);
    std::cout << "capability NGX: PASS\n";
}
