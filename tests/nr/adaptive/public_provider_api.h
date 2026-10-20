#pragma once
#include "public_fixture_lifetime.h"
#if !defined(NR_ADAPTIVE_PUBLIC_PROVIDER_PORT_AVAILABLE)
#error Public provider API belongs only to the explicit fixture build
#endif
#include <d3d12.h>
#include <nvsdk_ngx_params.h>
#include <dlssnr/NativeProviderEntry.h>
#include <dlssnr/Feature18HostContract.h>

namespace DlssNr::AdaptivePublicFixture
{
enum class ProviderFault:unsigned { None,SkipWrite,SkipReturn };
struct ProviderStats
{
    unsigned size=sizeof(ProviderStats);
    unsigned initializations=0,modelCreates=0,modelEvaluations=0,modelReleases=0;
    unsigned srCreates=0,srEvaluations=0,srReleases=0,liveModels=0,liveSr=0;
};
}

#if defined(NR_ADAPTIVE_PUBLIC_PROVIDER_EXPORTS)
#define NR_PUBLIC_API __declspec(dllexport)
#else
#define NR_PUBLIC_API
#endif

extern "C" {
NR_PUBLIC_API const char* nr_adaptive_public_fixture_identity_v1();
NR_PUBLIC_API const char* dlssnr_call_host_recorded_contract_v1();
NR_PUBLIC_API int nr_adaptive_public_fixture_stats_v1(DlssNr::AdaptivePublicFixture::ProviderStats*);
// Fault selection is allowed only before model creation. Identity mutation is
// a deliberate negative-test action, never a shipping admission mechanism.
NR_PUBLIC_API int nr_adaptive_public_fixture_fault_v1(unsigned);
NR_PUBLIC_API int nr_adaptive_public_fixture_identity_fault_v1(unsigned);
NR_PUBLIC_API void* dlssnr_call_create(const wchar_t*,const wchar_t*,ID3D12Device*,
    ID3D12GraphicsCommandList*,void*,unsigned,unsigned,int,float,int,float,float,float,int,int);
NR_PUBLIC_API int dlssnr_call_evaluate(ID3D12GraphicsCommandList*,void*,void*,ID3D12Resource*,
    ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,unsigned,unsigned,unsigned,unsigned,int,int,float,int,
    float,float,float,int,float,float,float,float);
NR_PUBLIC_API int dlssnr_call_evaluate_guides_v4(ID3D12GraphicsCommandList*,void*,void*,ID3D12Resource*,
    ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,
    int,int,float,int,float,float,float,int,float,float,float,float,const unsigned*,const DlssNr::ProviderEntryCallbacks*);
NR_PUBLIC_API void dlssnr_call_release(void*);
NR_PUBLIC_API int dlssnr_call_release_checked_v1(void*,DlssNr::FeatureReleaseResult*);
NR_PUBLIC_API int dlssnr_call_shutdown();
NR_PUBLIC_API int dlssnr_call_probe_d3d12(const wchar_t*);
NR_PUBLIC_API void dlssnr_call_probe_float(void*,const char*,float,int);
NR_PUBLIC_API void dlssnr_call_set_float_slot(int);
NR_PUBLIC_API extern int dlssnr_call_last_create;
NR_PUBLIC_API extern int dlssnr_call_last_init;
}
