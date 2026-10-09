#pragma once
#include <ffx_api.h>
#include <cstdint>
#include <string>

namespace Neurotic::Lifecycle
{
// Observation of the provider selected by an existing context. This is not a
// capability, module authentication, GPU completion or provider-release receipt.
enum class Fsr3DrainCapability { UnknownProvider,UncheckedWaitResults };
struct Fsr3LoadedContextObservation
{
    ffxReturnCode_t result=FFX_API_RETURN_NO_PROVIDER;
    std::uint64_t provider=0;
    std::string version;
    bool Valid()const noexcept{return result==FFX_API_RETURN_OK&&provider&&!version.empty();}
    Fsr3DrainCapability DrainStatus()const noexcept
    {
        // This exact provider is present in the pinned SDK source. Its dispatch
        // wrapper discards ffxWaitForPresents' result; the underlying three
        // waits also discard their results. Even a loaded match is insufficient
        // to establish healthy game/interpolation/presentation queue completion.
        if(Valid()&&provider==0xF65CDD1201001003ull&&version=="1.1.3")
            return Fsr3DrainCapability::UncheckedWaitResults;
        return Fsr3DrainCapability::UnknownProvider;
    }
};
inline Fsr3LoadedContextObservation ObserveFsr3LoadedContext(PfnFfxQuery query,ffxContext context)
{
    Fsr3LoadedContextObservation result;
    if(!query||!context)return result;
    ffxQueryGetProviderVersion descriptor{};
    descriptor.header.type=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    result.result=query(&context,&descriptor.header);
    if(result.result==FFX_API_RETURN_OK&&descriptor.versionId&&descriptor.versionName)
    {
        // Copy while the exact module/context is still retained by its owner.
        result.provider=descriptor.versionId;result.version=descriptor.versionName;
    }
    return result;
}
}
