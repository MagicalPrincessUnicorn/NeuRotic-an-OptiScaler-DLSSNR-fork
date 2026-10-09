#pragma once
#include <cstdint>
#include <optional>

namespace Neurotic::Mfg::Experimental
{
enum class Family { None, Rtx30, Rtx20 };
enum class Stage { Off, Waiting, Preparing, Ready, ActivityObserved, Unavailable, Poisoned };
enum class Reason { None, NotRequested, IdentityUnknown, WrongGpu, DisabledByEnvironment,
    OwnerConflict, UnsupportedRoute, ProviderMissing, ProviderAmbiguous, ProviderUnknown,
    TooLate, PreparationFailed, RollbackFailed, UnsupportedAbi, StructuralLimit,
    DynamicNotQualified, DeviceChanged, DeviceRemoved, WrapperMissing, WrapperChanged };
struct Preferences { bool rtx30=false, rtx20=false; bool operator==(const Preferences&) const = default; };
struct Renderer {
    bool bound=false;
    uint32_t vendor=0, architecture=0, implementation=0, device=0;
    uint64_t luid=0;
    bool consumerRtx=false;
};
// PCI families, additionally qualified by genuine NVAPI architecture and the
// actual D3D12 device LUID. TU116/117 (GTX16) and GA100 are deliberately excluded.
constexpr Family Detect(const Renderer& r) noexcept
{
    if(!r.bound || !r.luid || r.vendor!=0x10de || !r.consumerRtx) return Family::None;
    if(r.architecture==0x170 && r.implementation!=0 &&
       ((r.device>=0x2200 && r.device<=0x25ff))) return Family::Rtx30;
    if(r.architecture==0x160 &&
       ((r.device>=0x1e00 && r.device<=0x1eff) ||
        (r.device>=0x1f00 && r.device<=0x1fff))) return Family::Rtx20;
    return Family::None;
}
constexpr Family Select(Preferences p,const Renderer& r,bool killed,bool conflict) noexcept
{
    if(killed || conflict) return Family::None;
    const auto f=Detect(r);
    return (f==Family::Rtx30 && p.rtx30) || (f==Family::Rtx20 && p.rtx20) ? f : Family::None;
}
constexpr bool CanEdit(bool checked,Family row,Family renderer) noexcept { return checked || row==renderer; }
struct SessionPreferences {
    Preferences session{},saved{};
    bool begun=false;
    void Begin(Preferences p) noexcept { if(!begun) { session=saved=p; begun=true; } }
    void Saved(Preferences p,bool success) noexcept { if(success) saved=p; }
};
enum class Mode { Default, Off, Fixed, Dynamic };
struct Request { Mode mode=Mode::Default; int generated=0; };
struct Capability { bool ready=false, temporal=false; uint32_t abi=0; std::optional<uint32_t> ceiling; };
struct Decision { bool overrideRequest=false,off=false; uint32_t generated=0; Reason reason=Reason::None; };
inline Decision Decide(Request request,const Capability& c) noexcept
{
    if(request.mode==Mode::Default) return {};
    if(request.mode==Mode::Dynamic) return {false,false,0,Reason::DynamicNotQualified};
    if(c.abi<1 || c.abi>5) return {false,false,0,Reason::UnsupportedAbi};
    if(request.mode==Mode::Off) return {true,true,0,Reason::None};
    if(!c.ready || !c.temporal) return {false,false,0,Reason::PreparationFailed};
    if(!c.ceiling || *c.ceiling<1 || *c.ceiling>5 || request.generated<1 ||
       request.generated>5 || static_cast<uint32_t>(request.generated)>*c.ceiling)
        return {false,false,0,Reason::StructuralLimit};
    return {true,false,static_cast<uint32_t>(request.generated),Reason::None};
}
constexpr const char* ReasonText(Reason r) noexcept
{
    switch(r) {
    case Reason::None: return "Prepared; target GPU gameplay qualification pending";
    case Reason::NotRequested: return "Off at process startup";
    case Reason::IdentityUnknown: return "Waiting for the game's D3D12 renderer identity";
    case Reason::WrongGpu: return "The renderer is not a supported RTX 20/30 GPU";
    case Reason::DisabledByEnvironment: return "Disabled by NEUROTIC_DISABLE_EXPERIMENTAL_MFG";
    case Reason::OwnerConflict: return "Another FG replacement or unlock owner conflicts";
    case Reason::UnsupportedRoute: return "Requires game-owned D3D12 Streamline DLSS-G";
    case Reason::ProviderMissing: return "Waiting for the loaded DLSS-G provider";
    case Reason::ProviderAmbiguous: return "Multiple executable DLSS-G providers found";
    case Reason::ProviderUnknown: return "Provider identity or temporal profile is not qualified";
    case Reason::TooLate: return "Safe preparation before first FG use was not established; restart required";
    case Reason::PreparationFailed: return "Provider preparation failed; restart required";
    case Reason::RollbackFailed: return "Incomplete rollback; retained memory, restart required";
    case Reason::UnsupportedAbi: return "Unknown Streamline options layout";
    case Reason::StructuralLimit: return "Requested ratio exceeds the observed wrapper capacity";
    case Reason::DynamicNotQualified: return "Dynamic MFG is not qualified for this experimental backend";
    case Reason::DeviceChanged: return "Renderer changed; restart required";
    case Reason::DeviceRemoved: return "Device removed; restart required";
    case Reason::WrapperMissing: return "Waiting for the game's DLSS-G wrapper";
    case Reason::WrapperChanged: return "DLSS-G wrapper changed; restart required";
    }
    return "Unavailable";
}
constexpr const char* StageText(Stage s) noexcept
{
    switch(s) {
    case Stage::Off:return "Off";case Stage::Waiting:return "Waiting";
    case Stage::Preparing:return "Preparing";case Stage::Ready:return "Ready to attempt";
    case Stage::ActivityObserved:return "Activity observed";case Stage::Unavailable:return "Unavailable";
    case Stage::Poisoned:return "Failed for this session";
    } return "Unknown";
}
}
