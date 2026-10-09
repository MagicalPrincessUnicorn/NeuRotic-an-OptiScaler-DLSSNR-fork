#pragma once
#include "CharacterObservation.h"
#include <vector>
namespace Neurotic::Semantic::Character {
enum class CapturePhase {Free,Recorded,Submitted,Complete};
struct CaptureSlotState {
    CapturePhase phase=CapturePhase::Free;
    bool Record() noexcept {if(phase!=CapturePhase::Free)return false;phase=CapturePhase::Recorded;return true;}
    bool Submit() noexcept {if(phase!=CapturePhase::Recorded)return false;phase=CapturePhase::Submitted;return true;}
    bool Cancel() noexcept {if(phase!=CapturePhase::Recorded)return false;phase=CapturePhase::Free;return true;}
    bool Complete() noexcept {if(phase!=CapturePhase::Submitted)return false;phase=CapturePhase::Complete;return true;}
    bool Release() noexcept {if(phase!=CapturePhase::Complete)return false;phase=CapturePhase::Free;return true;}
};
struct CpuFrame {
    FrameKey key;
    unsigned width=0,height=0,stride=0,maximumPersons=4;
    std::vector<unsigned char> pixels;
    unsigned colorPolicy=0; // 0 SDR, 1 PQ/Rec2020, 2 scRGB, 3 scene-linear NGX Color.
    float preExposure=1; // Policy 3 only; native metadata, never a worker field.
    bool earlySource=false;
    std::uint64_t earlyFeatureGeneration=0; // Native source binding, never a worker field.
    std::uint64_t captureGeneration=1; // Native capture admission; never a worker field.
    bool poseRequested=false,detectObjects=false;
    unsigned boxHoldMs=80;
    unsigned externalVkFormat=0; // Private completed native pixels; converted on control worker, never sent over wire.
};
inline bool SameKey(const FrameKey& a,const FrameKey& b) noexcept {
    return a.epoch==b.epoch && a.sequence==b.sequence && a.captureNs==b.captureNs;
}
class OutstandingRequest {
    FrameKey key_{};unsigned width_=0,height_=0;bool active_=false;
public:
    bool Begin(FrameKey key,unsigned width,unsigned height) noexcept {
        if(active_ || !key.sequence || !key.captureNs || !width || !height || width>2048 || height>2048)return false;
        key_=key;width_=width;height_=height;active_=true;return true;
    }
    void Cancel() noexcept {active_=false;}
    bool Accept(FrameKey key,unsigned width,unsigned height) noexcept {
        if(!active_ || !SameKey(key,key_) || width!=width_ || height!=height_)return false;
        active_=false;return true;
    }
};
}
