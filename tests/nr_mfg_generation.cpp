// Actual runtime + inert image data only. The sequential call order is the
// exact happens-before case: completed invalidation, then delayed initial
// preparation taking the runtime mutex. No proprietary entry is executed.
#include <windows.h>
#include <d3d12.h>
#include "../OptiScaler/mfg/MfgAdaUnlock.h"
#include "../OptiScaler/mfg/MfgMappedPe.h"
#include "../OptiScaler/mfg/MfgProviderQualification.h"
#include "../OptiScaler/mfg/MfgTemporalDescriptor.h"
#include <array>
#include <cstdio>
#include <string>
using namespace Neurotic::Mfg;
static bool Check(bool value,const char* text){std::printf("%s: %s\n",value?"PASS":"FAIL",text);return value;}
int wmain(int argc,wchar_t** argv)
{
    if(argc!=2)return 2;
    const std::wstring folder=argv[1];
    const auto wrapper=LoadLibraryExW((folder+L"/sl.dlss_g.dll").c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    const auto provider=LoadLibraryExW((folder+L"/nvngx_dlssg.dll").c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!wrapper||!provider)return 2;
    const LUID adapter{89951,0};ObserveAdaD3D12Adapter(adapter);
    const auto p=ReadMfgMappedPe(reinterpret_cast<const uint8_t*>(provider)).ExecutableText();
    const auto w=ReadMfgMappedPe(reinterpret_cast<const uint8_t*>(wrapper)).ExecutableText();
    MfgTemporalDescriptors temporal;
    if(!p.bytes||!w.bytes||!FindTemporalDescriptors(reinterpret_cast<const uint8_t*>(provider),temporal))return 2;
    const auto providerBefore=FingerprintMfgImage(p.bytes,p.size);
    const auto wrapperBefore=FingerprintMfgImage(w.bytes,w.size);
    std::array<uint64_t,12> slotsBefore{};
    for(size_t i=0;i<temporal.count;++i)
        if(!ReadMfgMemory(reinterpret_cast<void*>(temporal.slots[i]),&slotsBefore[i],sizeof(uint64_t)))return 2;

    InvalidateAdaMfg(13); // Completes before old TryPublish is allowed to begin.
    const bool oldPublished=TryPublishAdaMfg(wrapper,12,5,adapter);
    bool unchanged=FingerprintMfgImage(p.bytes,p.size)==providerBefore&&
        FingerprintMfgImage(w.bytes,w.size)==wrapperBefore;
    for(size_t i=0;i<temporal.count;++i){
        uint64_t value=0;
        unchanged=ReadMfgMemory(reinterpret_cast<void*>(temporal.slots[i]),&value,sizeof value)&&
            value==slotsBefore[i]&&unchanged;
    }
    if(!Check(!oldPublished&&AdaMfgSnapshot().status!=MfgRuntimeStatus::Published&&unchanged,
        "completed invalidation refuses delayed initial publication before any mutation"))return 1;
    LUID other=adapter;other.LowPart^=1u;
    LUID observed{};
    if(!Check(!ObserveAdaCreateOwner(provider,&other,12)&&ObservedAdaD3D12Adapter(observed)&&
        observed.LowPart==adapter.LowPart&&observed.HighPart==adapter.HighPart,
        "old unowned observation cannot replace the current startup adapter"))return 1;
    if(!Check(TryPublishAdaMfg(wrapper,13,5,adapter)&&HasOwnedAdaMfg(wrapper,13)&&
        AdaMfgSnapshot().generation==13,
        "current-generation startup can publish after the rejected old request"))return 1;
    InvalidateAdaMfg(12);
    if(!Check(HasOwnedAdaMfg(wrapper,13)&&!TryPublishAdaMfg(wrapper,12,5,adapter)&&
        HasOwnedAdaMfg(wrapper,13)&&AdaMfgSnapshot().generation==13,
        "older invalidation and preparation leave current publication usable"))return 1;
    InvalidateAdaMfg(14);
    if(!Check(!TryPublishAdaMfg(wrapper,13,5,adapter)&&!HasOwnedAdaMfg(wrapper,13)&&
        TryPublishAdaMfg(wrapper,14,5,adapter)&&HasOwnedAdaMfg(wrapper,14),
        "owned publication renews only at the completed invalidation generation"))return 1;
    return 0;
}
