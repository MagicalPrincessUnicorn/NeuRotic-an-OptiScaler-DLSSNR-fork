// CPU-only: actual built FI/OF destroy functions with instrumented backend callbacks.
// No context creation, provider, device, shader, or GPU dispatch is executed.
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include "external/FidelityFX-SDK/sdk/src/components/frameinterpolation/ffx_frameinterpolation_private.h"
#include "external/FidelityFX-SDK/sdk/src/components/opticalflow/ffx_opticalflow_private.h"
#include "external/FidelityFX-CheckedClosure/CheckedAlgorithmCleanup.h"
#include <memory>
#include <vector>
#include <cstdio>
static unsigned checks=0,failures=0,backends=0;
static std::vector<int> destroyed;
static FfxErrorCode resourceResult=FFX_OK,backendResult=FFX_OK,pipelineResult=FFX_OK;
static bool hudless=false,badOwned=false;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d %s\n",__LINE__,#x);}}while(0)
static FfxErrorCode pipeline(FfxInterface*,FfxPipelineState*,FfxUInt32){return pipelineResult;}
static FfxErrorCode resource(FfxInterface*,FfxResourceInternal r,FfxUInt32 id){
    destroyed.push_back(r.internalIndex);
    // Model the actual backend's static interval, including its upper bound.
    // Dynamic registrations descend from (context+1)*512-1 and are borrowed.
    if(r.internalIndex<int(id*FFX_MAX_RESOURCE_COUNT) || r.internalIndex>=int(id*FFX_MAX_RESOURCE_COUNT+16))return FFX_ERROR_OUT_OF_RANGE;
    return resourceResult;
}
static FfxErrorCode backend(FfxInterface*,FfxUInt32){++backends;return backendResult;}
static FfxInterface interface(){FfxInterface i{};i.fpDestroyResource=resource;i.fpDestroyPipeline=pipeline;i.fpDestroyBackendContext=backend;return i;}
static void reset(){destroyed.clear();backends=0;resourceResult=backendResult=pipelineResult=FFX_OK;hudless=badOwned=false;}
static FfxErrorCode fi(){
    auto c=std::make_unique<FfxFrameInterpolationContext_Private>();
    c->contextDescription.backendInterface=interface();c->effectContextId=1;
    c->srvResources[FFX_FRAMEINTERPOLATION_RESOURCE_IDENTIFIER_COUNTERS]={badOwned?1023:FFX_MAX_RESOURCE_COUNT+1};
    // Dispatch registers currentBackBuffer first at dynamic index1023. With
    // HUDless absent PRESENT_BACKBUFFER aliases CURRENT_INTERPOLATION_SOURCE;
    // with HUDless present the two borrowed registrations are distinct.
    c->srvResources[FFX_FRAMEINTERPOLATION_RESOURCE_IDENTIFIER_PRESENT_BACKBUFFER]={1023};
    c->srvResources[FFX_FRAMEINTERPOLATION_RESOURCE_IDENTIFIER_CURRENT_INTERPOLATION_SOURCE]={hudless?1022:1023};
    for(auto id:{FFX_FRAMEINTERPOLATION_RESOURCE_IDENTIFIER_OPTICAL_FLOW_VECTOR,
                 FFX_FRAMEINTERPOLATION_RESOURCE_IDENTIFIER_OPTICAL_FLOW_SCENE_CHANGE_DETECTION,
                 FFX_FRAMEINTERPOLATION_RESOURCE_IDENTIFIER_DILATED_DEPTH,
                 FFX_FRAMEINTERPOLATION_RESOURCE_IDENTIFIER_DILATED_MOTION_VECTORS,
                 FFX_FRAMEINTERPOLATION_RESOURCE_IDENTIFIER_RECONSTRUCTED_DEPTH_PREVIOUS_FRAME,
                 FFX_FRAMEINTERPOLATION_RESOURCE_IDENTIFIER_OUTPUT,
                 FFX_FRAMEINTERPOLATION_RESOURCE_IDENTIFIER_DISTORTION_FIELD})c->srvResources[id]={1021};
    return ffxFrameInterpolationContextDestroy(reinterpret_cast<FfxFrameInterpolationContext*>(c.get()));
}
static FfxErrorCode of(){
    auto c=std::make_unique<FfxOpticalflowContext_Private>();
    c->contextDescription.backendInterface=interface();c->effectContextId=1;
    c->resources[1]={FFX_MAX_RESOURCE_COUNT+1};
    return ffxOpticalflowContextDestroy(reinterpret_cast<FfxOpticalflowContext*>(c.get()));
}
int main(){
    reset();CHECK(fi()==FFX_OK);CHECK(backends==1);
    CHECK((destroyed==std::vector<int>{FFX_MAX_RESOURCE_COUNT+2,FFX_MAX_RESOURCE_COUNT+1}));
    reset();hudless=true;CHECK(fi()==FFX_OK);CHECK(backends==1);
    CHECK((destroyed==std::vector<int>{FFX_MAX_RESOURCE_COUNT+2,FFX_MAX_RESOURCE_COUNT+1}));
    reset();badOwned=true;CHECK(fi()==FFX_ERROR_OUT_OF_RANGE);CHECK(backends==0);
    reset();CHECK(of()==FFX_OK);CHECK(backends==1);CHECK((destroyed==std::vector<int>{FFX_MAX_RESOURCE_COUNT+1}));
    for(auto release:{fi,of}){
        reset();resourceResult=FFX_ERROR_BACKEND_API_ERROR;
        {ffx::nr::AlgorithmCleanup outer;CHECK(release()==resourceResult);CHECK(outer.Result()==resourceResult);CHECK(backends==0);}
        reset();pipelineResult=FFX_ERROR_INVALID_ARGUMENT;
        CHECK(release()==pipelineResult);CHECK(backends==0);
        reset();backendResult=FFX_ERROR_BACKEND_API_ERROR;
        CHECK(release()==backendResult);CHECK(backends==1);
    }
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
