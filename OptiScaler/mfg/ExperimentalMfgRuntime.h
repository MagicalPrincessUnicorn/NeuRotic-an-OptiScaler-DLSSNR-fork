#pragma once
#include "ExperimentalMfgPolicy.h"
#include "ExperimentalMfgSession.h"
#include <windows.h>
struct ID3D12Device;
namespace Neurotic::Mfg::Experimental {
struct Snapshot {
    Preferences session{},saved{};
    Renderer renderer{};
    std::array<char,128> rendererName{};
    Family family=Family::None;
    Stage stage=Stage::Off;
    Reason reason=Reason::NotRequested;
    uint32_t ceiling=0,rawCapacity=0,abi=0,original=0,forwarded=0;
    int result=0;
    uint64_t evaluations=0;
    bool prepared=false,requested=false,backendLoaded=false,saveFailed=false,optionsObserved=false;
};
void BeginSession(Preferences) noexcept;
void SettingsSaved(Preferences,bool success) noexcept;
Snapshot ReadSnapshot() noexcept;
bool Requested() noexcept;
bool AllowFeatureCall(bool off=false) noexcept;
void ObserveDevice(ID3D12Device*) noexcept;
// Synchronous game-owned SL boundary, never a loader callback or menu draw.
bool PrepareAtBoundary(bool gameOwnedD3D12) noexcept;
bool MatchesAdapter(LUID) noexcept;
bool RelaxStreamline(int original,LUID adapter) noexcept;
void ObserveCapacity(uint32_t capacity) noexcept;
void ObserveOptions(uint32_t abi,uint32_t original,uint32_t generated,int result) noexcept;
void ObserveEvaluation(bool success) noexcept;
void BeforeCreate() noexcept;
void DeviceRemoved() noexcept;
void RefuseRequest(Reason) noexcept;
void ObserveModuleLoad(HMODULE,DWORD flags) noexcept;
void ObserveWrapper(HMODULE) noexcept;
bool RelaxRequirements(uint32_t result,uint32_t feature,LUID adapter,uint32_t& flags,uint32_t& minArchitecture) noexcept;
class ArchitectureScope {
    ThreadScope scope;
public:
    ArchitectureScope(bool fg,LUID adapter) noexcept;
    ArchitectureScope(const ArchitectureScope&)=delete;
    ArchitectureScope& operator=(const ArchitectureScope&)=delete;
};
bool ExposeArchitecture(uintptr_t physicalGpu,int result,uint32_t& architecture) noexcept;
}
