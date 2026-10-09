#pragma once
#include <dlssnr/HdrObservation.h>
#include <optional>
namespace Neurotic::Semantic::Character {
struct SourceColorPolicy { unsigned conversion=0; bool dxgiDefault=false; };
inline std::optional<SourceColorPolicy> CharacterSourceColor(const DlssNr::HdrObservation::Snapshot& source) noexcept {
    if(!source.registered||source.transitioning)return {};
    const bool sdr=source.format==DXGI_FORMAT_R8G8B8A8_UNORM||source.format==DXGI_FORMAT_B8G8R8A8_UNORM||source.format==DXGI_FORMAT_R10G10B10A2_UNORM;
    const bool linear=source.format==DXGI_FORMAT_R16G16B16A16_FLOAT&&source.colorSpace==DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    // Registered integer chains start in DXGI's sRGB default; float is linear. Preserve that
    // derived provenance; do not fabricate a successful SetColorSpace1 call.
    if(!source.colorSpaceObserved){
        if(sdr&&source.colorSpace==DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
            return SourceColorPolicy{0,true};
        if(linear)return SourceColorPolicy{2,true};
        return {};
    }
    if(sdr&&source.colorSpace==DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)return SourceColorPolicy{0,false};
    if(source.format==DXGI_FORMAT_R10G10B10A2_UNORM&&source.colorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)return SourceColorPolicy{1,false};
    if(linear)return SourceColorPolicy{2,false};
    return {};
}
inline bool CharacterFgReported(bool nativeRecentWork,bool managedActive,bool managedPaused) noexcept {
    // A configured route or the FSR input request can outlive its output.
    // A recent/in-flight native evaluation and running managed output count.
    return nativeRecentWork||(managedActive&&!managedPaused);
}
}
