#include <menu/Localization.h>
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#if defined(_WIN32) && !defined(NR_PREPARED_GUIDES_NO_WINDOWS_QUERY)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace DlssNr::PreparedGuides {
// Read-only diagnostics. These records never confer native input ownership or
// prove display. Counters describe producer-observed completions this session.
enum class Stage : uint32_t { Idle, Waiting, Captured, Processing, Processed, Delivered, Blocked };
struct alignas(8) Status {
    uint32_t size=sizeof(Status),version=1;
    uint64_t session=0,capture=0,updatedTickMs=0,inputFrames=0,modelCompletions=0,copybackCompletions=0;
    uint32_t sourceApi=0,depthReady=0,motionReady=0,active=0;
    Stage stage=Stage::Idle;
    char reason[160]{};
};
static_assert(std::is_standard_layout_v<Status> && std::is_trivially_copyable_v<Status>);
static_assert(sizeof(Status)==240 && offsetof(Status,reason)==76);
#ifdef _WIN32
using QueryFn=unsigned(__cdecl*)(Status*);
#else
using QueryFn=unsigned(*)(Status*);
#endif
inline const char* QueryExport="NeuRotic_QueryPreparedGuidesV1";
struct Snapshot {Status status;bool available=false,fresh=false;};
inline const char* StageName(Stage stage) noexcept {
    switch(stage) {
    case Stage::Idle:return Neurotic::UiLiteral("ingame.provider.ab0171ca0494", "Idle");
    case Stage::Waiting:return Neurotic::UiLiteral("ingame.preparedguidestatus.waiting_for_inputs_4cdeb121", "Waiting for inputs");
    case Stage::Captured:return Neurotic::UiLiteral("ingame.provider.8a03fa9ad749", "Captured");
    case Stage::Processing:return Neurotic::UiLiteral("ingame.provider.c8e3e92a62ec", "Processing");
    case Stage::Processed:return Neurotic::UiLiteral("ingame.preparedguidestatus.model_completed_010930a7", "Model completed");
    case Stage::Delivered:return Neurotic::UiLiteral("ingame.preparedguidestatus.copyback_completed_30487a34", "Copyback completed");
    case Stage::Blocked:return Neurotic::UiLiteral("ingame.provider.18f2a0947f9d", "Blocked");
    default:return Neurotic::UiLiteral("ingame.menu-common.unknown_d80d0833", "Unknown");
    }
}
inline const char* SourceApiName(uint32_t api) noexcept {
    switch(api) {
    case 0x9000:return "D3D9";
    case 0xa000:return "D3D10";
    case 0xb000:return "D3D11";
    case 0xc000:return "D3D12";
    case 0x10000:return "OpenGL";
    case 0x20000:return "Vulkan";
    default:return Neurotic::UiLiteral("ingame.preparedguidestatus.unknown_api_66d44a71", "Unknown API");
    }
}
inline void SetReason(Status& status,const char* reason) noexcept {
    size_t i=0;
    for(;i+1<sizeof(status.reason) && reason[i];++i)status.reason[i]=reason[i];
    status.reason[i]='\0';
}
inline Snapshot ValidateStatus(Status status,uint64_t nowMs) noexcept {
    Snapshot result;
    if(status.size!=sizeof(Status) || status.version!=1 ||
       static_cast<uint32_t>(status.stage)>static_cast<uint32_t>(Stage::Blocked) ||
       status.depthReady>1 || status.motionReady>1 || status.active>1)return result;
    status.reason[sizeof(status.reason)-1]='\0';
    for(auto& c:status.reason) {if(!c)break;if(static_cast<unsigned char>(c)<32 || static_cast<unsigned char>(c)>126)c=' ';}
    result.available=true;
    result.fresh=status.updatedTickMs!=0 && nowMs>=status.updatedTickMs && nowMs-status.updatedTickMs<=3000;
    if(!result.fresh) {
        status.active=status.depthReady=status.motionReady=0;status.stage=Stage::Waiting;
        SetReason(status,Neurotic::UiLiteral("ingame.preparedguidestatus.prepared_input_status_is_stale_waiting_for_a_cur_48a3aeeb", "Prepared input status is stale; waiting for a current capture"));
    } else if(!status.active || !status.session || !status.capture) {
        status.depthReady=status.motionReady=0;
        if(status.stage==Stage::Captured || status.stage==Stage::Processing ||
           status.stage==Stage::Processed || status.stage==Stage::Delivered)status.stage=Stage::Waiting;
    }
    result.status=status;return result;
}
inline Snapshot QueryStatus() noexcept {
#if defined(_WIN32) && !defined(NR_PREPARED_GUIDES_NO_WINDOWS_QUERY)
    // Acquire a reference to an already loaded addon before resolving/calling it.
    // Never load the addon merely to display diagnostics; never cache its pointer.
    HMODULE module=nullptr;
#ifdef _WIN64
    constexpr const wchar_t* name=L"NeuRotic-PreparedGuides.addon64";
#else
    constexpr const wchar_t* name=L"NeuRotic-PreparedGuides.addon32";
#endif
    if(!GetModuleHandleExW(0,name,&module))return {};
    struct Lease {HMODULE module;~Lease(){FreeLibrary(module);}} lease{module};
    const auto query=reinterpret_cast<QueryFn>(GetProcAddress(module,QueryExport));
    Status status;
    if(!query || query(&status)!=1)return {};
    return ValidateStatus(status,GetTickCount64());
#else
    return {};
#endif
}
}
