// Dedicated SDK mechanics build: retain the original API dispatcher and DX12
// swapchain provider, with an explicit provider inventory containing only that provider.
// No algorithm provider is exposed or loaded by this module.
#include "../FidelityFX-SDK/ffx-api/src/dx12/ffx_provider_framegenerationswapchain_dx12.h"

ffxProvider_FrameGenerationSwapChain_DX12 ffxProvider_FrameGenerationSwapChain_DX12::Instance;
const ffxProvider* GetffxProvider(ffxStructType_t type,uint64_t overrideId,void*) {
    auto& provider=ffxProvider_FrameGenerationSwapChain_DX12::Instance;
    return provider.CanProvide(type) && (!overrideId || overrideId==provider.GetId()) ? &provider : nullptr;
}
const ffxProvider* GetAssociatedProvider(ffxContext* context) {
    return context && *context ? reinterpret_cast<InternalContextHeader*>(*context)->provider : nullptr;
}
uint64_t GetProviderCount(ffxStructType_t type,void*) { return GetffxProvider(type,0,nullptr)?1:0; }
uint64_t GetProviderVersions(ffxStructType_t type,void*,uint64_t capacity,uint64_t* ids,const char** names) {
    auto provider=GetffxProvider(type,0,nullptr); if(!provider || !capacity) return 0;
    if(ids) ids[0]=provider->GetId(); if(names) names[0]=provider->GetVersionName(); return 1;
}
