#include "../OptiScaler/mfg/MfgResolutionDiagnostics.h"
#include <cassert>
#include <cstdio>
using namespace Neurotic::Mfg;
int main()
{
    assert(MfgResolutionExport(nullptr) == 0);
    for (uintptr_t ordinal : {uintptr_t(1), uintptr_t(0xefff), uintptr_t(0xf000), uintptr_t(0xffff)})
        assert(MfgResolutionExport(reinterpret_cast<const char*>(ordinal)) == 0);
    assert(MfgResolutionExport("unrelated") == 0);
    assert(MfgResolutionExport("NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl") == 1);
    assert(MfgResolutionExport("NVSDK_NGX_D3D12_CreateFeature") == 3);
    assert(MfgResolutionExport("NVSDK_NGX_D3D12_EvaluateFeature") == 4);
    assert(MfgResolutionExport("NVSDK_NGX_D3D12_GetFeatureRequirements") == 5);
    assert(MfgResolutionExport("NVSDK_NGX_D3D12_CreateFeature_Impl") == 0);
    assert(MfgResolutionExport("NVSDK_NGX_D3D12_EvaluateFeature_Impl") == 0);
    MfgResolutionDiagnostics diagnostics;
    MfgResolutionObservation observation {10, 20, 30, 1};
    assert(diagnostics.Record(observation));
    assert(!diagnostics.Record(observation));
    observation.queried = 11;
    assert(diagnostics.Record(observation));
    observation.caller = 31;
    assert(diagnostics.Record(observation));
    observation.resolved = 0;
    assert(!diagnostics.Record(observation));
    for (unsigned i = 0; i < 70; ++i)
        diagnostics.Record({100+i, 200+i, 300+i, 1});
    assert(diagnostics.Count() == 64);
    assert(diagnostics.Saturated());
    std::puts("PASS: resolution diagnostics preserve distinct owners, deduplicate repeats and bound recording; no publication authority");
}
