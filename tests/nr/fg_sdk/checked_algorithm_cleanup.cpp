#include <FidelityFX/host/ffx_interface.h>
#include "external/FidelityFX-SDK/sdk/src/shared/ffx_object_management.h"
#include "external/FidelityFX-CheckedClosure/CheckedAlgorithmCleanup.h"
#include <cstdio>

static unsigned checks=0,failures=0,pipelineCalls=0,resourceCalls=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(false)
static FfxErrorCode nextResult=FFX_OK;
static bool enforceContextRange=false;
static int32_t lastResource=0;
static FfxErrorCode destroyPipeline(FfxInterface*,FfxPipelineState*,FfxUInt32){++pipelineCalls;return nextResult;}
static FfxErrorCode destroyResource(FfxInterface*,FfxResourceInternal resource,FfxUInt32 context){
    ++resourceCalls;lastResource=resource.internalIndex;
    // The DX12 backend allocates context N from N*FFX_MAX_RESOURCE_COUNT+1.
    // Zero is the global null sentinel, outside every nonzero context range.
    if(enforceContextRange && resource.internalIndex<int32_t(context*FFX_MAX_RESOURCE_COUNT))return FFX_ERROR_OUT_OF_RANGE;
    return nextResult;
}
int main(){
    FfxInterface backend{};backend.fpDestroyPipeline=destroyPipeline;backend.fpDestroyResource=destroyResource;
    FfxPipelineState pipeline{};FfxResourceInternal resource{3};
    {
        ffx::nr::AlgorithmCleanup scope;
        CHECK(scope.Result()==FFX_OK);CHECK(scope.Operations()==0);
        ffxSafeReleasePipeline(&backend,&pipeline,1);
        ffxSafeReleaseResource(&backend,resource,1);
        ffxSafeReleaseCopyResource(&backend,resource,1);
        CHECK(scope.Operations()==3);CHECK(scope.Result()==FFX_OK);
        nextResult=FFX_ERROR_BACKEND_API_ERROR;
        ffxSafeReleaseResource(&backend,resource,1);
        CHECK(scope.Result()==FFX_ERROR_BACKEND_API_ERROR);
        nextResult=FFX_OK;ffxSafeReleasePipeline(&backend,&pipeline,1);
        CHECK(scope.Result()==FFX_ERROR_BACKEND_API_ERROR);CHECK(scope.Operations()==5);
    }
    {
        ffx::nr::AlgorithmCleanup outer;
        {
            ffx::nr::AlgorithmCleanup inner;
            nextResult=FFX_ERROR_INVALID_ARGUMENT;
            ffxSafeReleasePipeline(&backend,&pipeline,1);
            CHECK(inner.Result()==FFX_ERROR_INVALID_ARGUMENT);
            CHECK(outer.Result()==FFX_ERROR_INVALID_ARGUMENT);
        }
        nextResult=FFX_OK;ffxSafeReleaseResource(&backend,resource,1);
        CHECK(outer.Result()==FFX_ERROR_INVALID_ARGUMENT);CHECK(outer.Operations()==2);
    }
    {ffx::nr::AlgorithmCleanup fresh;CHECK(fresh.Result()==FFX_OK);CHECK(fresh.Operations()==0);}
    CHECK(pipelineCalls==3);CHECK(resourceCalls==4);
    enforceContextRange=true;nextResult=FFX_OK;
    {
        ffx::nr::AlgorithmCleanup cleanup;
        const auto calls=resourceCalls;
        // FI clears borrowed/uncreated entries to zero before walking its table.
        ffxSafeReleaseResource(&backend,{0},1);
        ffxSafeReleaseCopyResource(&backend,{0},1);
        CHECK(resourceCalls==calls);CHECK(cleanup.Operations()==0);CHECK(cleanup.Result()==FFX_OK);
        FfxResourceInternal owned{FFX_MAX_RESOURCE_COUNT+1};
        ffxSafeReleaseResource(&backend,owned,1);
        CHECK(resourceCalls==calls+1);CHECK(lastResource==owned.internalIndex);CHECK(cleanup.Result()==FFX_OK);
        ffxSafeReleaseCopyResource(&backend,owned,1);
        CHECK(resourceCalls==calls+2);CHECK(lastResource==owned.internalIndex+1);CHECK(cleanup.Operations()==2);
        // A non-null foreign index is still an error, never normalized to success.
        ffxSafeReleaseResource(&backend,{1},1);
        CHECK(cleanup.Result()==FFX_ERROR_OUT_OF_RANGE);CHECK(cleanup.Operations()==3);
        ffxSafeReleaseResource(&backend,{0},1);
        CHECK(cleanup.Result()==FFX_ERROR_OUT_OF_RANGE);CHECK(cleanup.Operations()==3);
    }
    std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
