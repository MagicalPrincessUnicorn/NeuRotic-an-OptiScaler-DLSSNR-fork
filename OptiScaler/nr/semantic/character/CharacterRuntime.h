#pragma once
#include "CharacterInspectorSettings.h"
#include "CharacterPipeline.h"
#include "CharacterBoxHold.h"
#include <filesystem>
#include <string>
struct IDXGISwapChain3;
struct IDXGISwapChain;
struct ID3D11Device;
struct IUnknown;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
namespace Neurotic::Semantic::Character {
namespace Detail {
enum class CaptureSourceChoice {None,Present,Early,SwitchToPresent,SwitchToEarly};
inline bool EarlyFeatureAllowed(std::uint64_t now,std::uint64_t lastAdmittedNs,
    std::uint64_t boundFeature,std::uint64_t candidateFeature) noexcept {
    return boundFeature==candidateFeature||!lastAdmittedNs||
        (now>=lastAdmittedNs&&now-lastAdmittedNs>DisplayTtlNs);
}
inline bool EarlyFeatureCompetes(std::uint64_t now,std::uint64_t lastSeenBoundNs,
    std::uint64_t boundFeature,std::uint64_t candidateFeature) noexcept {
    return boundFeature!=candidateFeature&&
        !EarlyFeatureAllowed(now,lastSeenBoundNs,boundFeature,candidateFeature);
}
// Called only for valid completed images. Switching discards its triggering
// image so the next capture carries the new epoch from its actual origin.
struct CaptureSourcePolicy {
    bool hasSource=false,early=false,hasEarlyFeature=false;std::uint64_t lastEarlyNs=0,earlyFeature=0;
    CaptureSourceChoice Select(std::uint64_t now,bool hasPresent,std::uint64_t earlyNs,
        std::uint64_t feature=0,std::uint64_t ambiguousUntil=0) noexcept {
        if(now<ambiguousUntil||earlyNs<ambiguousUntil)earlyNs=0;
        if(now<ambiguousUntil||lastEarlyNs<ambiguousUntil)lastEarlyNs=0;
        if(!earlyNs&&early&&lastEarlyNs&&now>=lastEarlyNs&&now-lastEarlyNs<=DisplayTtlNs)
            return CaptureSourceChoice::None;
        if(!earlyNs&&!hasPresent)return CaptureSourceChoice::None;
        const bool nextEarly=earlyNs!=0;
        const bool changed=(hasSource&&early!=nextEarly)||(nextEarly&&hasEarlyFeature&&earlyFeature!=feature);
        if(nextEarly){hasEarlyFeature=true;earlyFeature=feature;}
        hasSource=true;early=nextEarly;lastEarlyNs=earlyNs;
        if(changed)return early?CaptureSourceChoice::SwitchToEarly:CaptureSourceChoice::SwitchToPresent;
        return early?CaptureSourceChoice::Early:CaptureSourceChoice::Present;
    }
};
}
enum class WorkerState {Stopped,Starting,Ready,Fault,Stopping};
struct RuntimeView {
    WorkerState worker=WorkerState::Stopped;bool sourceQualified=false,dxgiDefaultColor=false;
    std::string reason="Start live inspection to load the local provider.";
    unsigned captures=0,dropped=0,results=0,processed=0,noPose=0,expired=0,epochRejected=0,sceneResets=0;
    unsigned inferenceMs=0,replyAgeMs=0;
    unsigned maximumPersons=4,lastPersons=0,boxHoldMs=80;
    unsigned detectorCandidates=0,poseAttempts=0,usablePoses=0,returnedDetections=0;
    double detectorMs=0,poseMs=0;
    double trackingMs=0;unsigned detectionUpdates=0,poseUpdates=0,poseFailures=0;
    bool earlySource=false,fgActive=false,realFrameSource=false;unsigned earlyCaptures=0,sourceSwitches=0,fgCaptures=0,sourceApi=0;
};
// Start merely schedules control-owner work. Stop cancels admission immediately.
// No process creation, model loading, pipe IO or waits run on Present.
void StartCharacterWorker(const std::filesystem::path& root);
void StartCharacterWorkerAtDefaultLocation();
void StopCharacterWorker() noexcept;
RuntimeView TryCharacterRuntimeView();
bool CharacterWorkerRequested() noexcept;
// Parked after the enhanced-barrier game return. Inspector must not enroll
// command recording hooks or touch SR/RR inputs while using displayed frames.
inline constexpr bool CharacterEarlyCaptureEnabled=false;
void CharacterPresent(IDXGISwapChain3*,ID3D12CommandQueue*,bool fgActive,bool focused,
                      const InspectorSettings&,bool physicalOutputQualified=false,
                      std::uintptr_t applicationOwner=0,bool applicationReal=false,std::uint64_t frameToken=0) noexcept;
void CharacterPresentDx11(IDXGISwapChain*,ID3D11Device*,bool fgActive,bool focused,
                         const InspectorSettings&,bool physicalOutputQualified=false) noexcept;
// Native backend owners supply only their observed physical-output identity.
// Vulkan generation comes from its present registry; DXGI uses HDR observations.
struct ExternalCharacterSource {
    unsigned api=2,width=0,height=0,colorPolicy=0; // 1 DX11, 2 Vulkan; 12 is internal D3D12.
    std::uint64_t deviceIdentity=0,swapchainGeneration=0,resizeGeneration=0;
    bool fgActive=false,focused=true,qualified=false,dxgiDefaultColor=false,srgbAttachment=false;
};
std::optional<CpuFrame> CharacterBeginExternalFrame(const ExternalCharacterSource&,const InspectorSettings&) noexcept;
bool CharacterPublishExternalFrame(CpuFrame&&) noexcept;
// Optional pre-upscale sampling. False means graphics state restoration failed
// after recording; the caller must abort that evaluation.
bool CharacterBeforeUpscale(ID3D12GraphicsCommandList*,ID3D12Resource*,
    unsigned renderWidth,unsigned renderHeight,unsigned outputWidth,unsigned outputHeight,
    bool sceneLinear,float preExposure,std::uint64_t featureGeneration) noexcept;
bool TryCharacterDisplay(Snapshot&,DisplayContext&) noexcept;
bool TryCharacterHeldDisplay(HeldSnapshot&,DisplayContext&) noexcept;
void CharacterSourceInvalidated() noexcept;
void CharacterNativeFgStarted() noexcept;
#ifdef CHARACTER_RUNTIME_TEST
void SetCharacterAdmissionTestHook(void(*)()) noexcept;
#endif
// Resize owner must return WAS_STILL_DRAWING when retirement is incomplete.
bool CharacterBeforeResize(IUnknown* boundaryDevice=nullptr) noexcept;
// Final swapchain boundary: release private DX11 references without querying
// or waiting on its immediate context. No game GPU operations are issued.
void CharacterSwapchainReleased(IDXGISwapChain*,IUnknown* boundaryDevice) noexcept;
}
