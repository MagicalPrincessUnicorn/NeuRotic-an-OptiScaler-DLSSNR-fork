#pragma once

#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include "VulkanNrTuning.h"
#include "VulkanNrFrameContract.h"
#include "NrPreflightSignals.h"
#include <array>
#include <string>
#include <type_traits>

namespace DlssNr::VkFrame
{
struct Failure
{
    std::string key;
    const char* reason = "none";
    uint32_t result = 1;
    bool readbackAttempted = false;
    void* expectedPointer = nullptr;
    void* observedPointer = nullptr;
};

// Preserve raw key presence for Signal Status; SDK defaults are not observations.
template<class Params> void ObserveNativeInputs(NrPreflightSignals::NativeInputs& report, Params* params)
{
    if(!params)return;
    float jx=0,jy=0,mx=0,my=0,pre=0;void* exposure=nullptr;
    const bool hasJx=params->Get(NVSDK_NGX_Parameter_Jitter_Offset_X,&jx)==NVSDK_NGX_Result_Success;
    const bool hasJy=params->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y,&jy)==NVSDK_NGX_Result_Success;
    const bool hasMx=params->Get(NVSDK_NGX_Parameter_MV_Scale_X,&mx)==NVSDK_NGX_Result_Success;
    const bool hasMy=params->Get(NVSDK_NGX_Parameter_MV_Scale_Y,&my)==NVSDK_NGX_Result_Success;
    const bool hasPre=params->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,&pre)==NVSDK_NGX_Result_Success;
    const bool hasExposure=params->Get(NVSDK_NGX_Parameter_ExposureTexture,&exposure)==NVSDK_NGX_Result_Success&&exposure;
    report.Record(hasJx,jx,hasJy,jy,hasMx,mx,hasMy,my,hasPre,pre,hasExposure);
}

inline bool Reject(Failure& failure, const char* key, const char* reason)
{
    failure.key = key;
    failure.reason = reason;
    return false;
}

// Inspect the discriminant before reading the union. No layout or resource substitution.
inline bool ValidateImage(const char* key, const NVSDK_NGX_Resource_VK* resource,
                          bool writable, Failure& failure)
{
    if (!resource) return Reject(failure, key, "missing wrapper");
    if (resource->Type != NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)
        return Reject(failure, key, "not an image-view wrapper");
    const auto& image = resource->Resource.ImageViewInfo;
    if (!image.Image || !image.ImageView) return Reject(failure, key, "missing image/view handle");
    if (!image.Width || !image.Height) return Reject(failure, key, "zero image extent");
    if (image.Format == VK_FORMAT_UNDEFINED) return Reject(failure, key, "undefined image format");
    if (!image.SubresourceRange.aspectMask || !image.SubresourceRange.levelCount ||
        !image.SubresourceRange.layerCount) return Reject(failure, key, "empty subresource range");
    if (writable && !resource->ReadWrite) return Reject(failure, key, "output is not writable");
    return true;
}

inline bool ValidateRect(const char* key, const NVSDK_NGX_Resource_VK* resource,
                         uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                         bool writable, Failure& failure)
{
    if (!ValidateImage(key, resource, writable, failure)) return false;
    const auto& image = resource->Resource.ImageViewInfo;
    // Subtraction avoids accepting an overflowing base + extent.
    if (!width || !height || x > image.Width || y > image.Height ||
        width > image.Width - x || height > image.Height - y)
        return Reject(failure, key, "subrect outside image extent");
    return true;
}

template<class Params, class T>
bool ReadEquals(Params* params, const char* key, T expected, Failure& failure)
{
    T observed{};
    const auto result = params->Get(key, &observed);
    if (static_cast<uint32_t>(result) == 1 && observed == expected) return true;
    failure.result = static_cast<uint32_t>(result);
    failure.readbackAttempted = true;
    if constexpr (std::is_same_v<T, void*>)
    {
        failure.expectedPointer = expected;
        failure.observedPointer = observed;
    }
    return Reject(failure, key, static_cast<uint32_t>(result) == 1 ? "readback mismatch" : "readback failed");
}

template<class Params, class T>
bool WriteChecked(Params* params, const char* key, T value, Failure& failure)
{
    params->Set(key, value);
    return ReadEquals(params, key, value, failure);
}

struct Binding
{
    const char* key;
    NVSDK_NGX_Resource_VK* resource;
    uint32_t width, height;
    bool writable;
    uint32_t x = 0, y = 0;
};

struct Inputs
{
    NVSDK_NGX_Resource_VK *color, *depth, *motion, *output;
    uint32_t width, height, guideWidth, guideHeight;
    bool depthInverted, reset;
    std::optional<VkNrTemporalMetadata> temporal;

    std::array<Binding, 4> Bindings() const
    {
        const VkNrRect d = temporal ? temporal->depth : VkNrRect{0,0,guideWidth,guideHeight};
        const VkNrRect m = temporal ? temporal->motion : VkNrRect{0,0,guideWidth,guideHeight};
        return {{{"DLSSNR.Color", color, width, height, false},
                 {"DLSSNR.Depth", depth, d.width, d.height, false, d.x, d.y},
                 {"DLSSNR.MVec", motion, m.width, m.height, false, m.x, m.y},
                 {"DLSSNR.Output", output, width, height, true}}};
    }
};

template<class Params>
bool Prepare(Params* params, const Inputs& frame, const VkTuning::Settings& applied, Failure& failure)
{
    failure = {};
    if (!params) return Reject(failure, "parameter block", "missing model parameters");
    if (frame.temporal && !ValidVkNrTemporal(*frame.temporal))
        return Reject(failure, "temporal metadata", "invalid temporal subrect or scalar");
    const auto bindings = frame.Bindings();
    for (const auto& b : bindings)
        if (!ValidateRect(b.key, b.resource, b.x, b.y, b.width, b.height, b.writable, failure)) return false;

    // Creation settings are checked, not rewritten: pending UI requests must not leak into this model.
    const auto read = [&](const char* key, auto value) { return ReadEquals(params, key, value, failure); };
    if (!read("DLSSNR.Enabled", 1u) || !read("DLSSNR.Width", frame.width) ||
        !read("DLSSNR.Height", frame.height) || !read("CreationNodeMask", 1u) ||
        !read("VisibilityNodeMask", 1u) || !read("DLSSNR.Hint.Render.Preset", applied.preset) ||
        !read("DLSSNR.Style", applied.style) || !read("DLSSNR.Intensity", applied.intensity) ||
        !read("DLSSNR.LocalStructureStrength", applied.structure) ||
        !read("DLSSNR.LocalToneStrength", applied.tone) ||
        !read("DLSSNR.SkinStructureStrength", applied.skin) ||
        !read("DLSSNR.UseAutoMask", applied.mask ? 1u : 0u) || !read("DLSSNR.UICorrection", 1u)) return false;

    const auto write = [&](const char* key, auto value) { return WriteChecked(params, key, value, failure); };
    for (const auto& b : bindings)
    {
        // Vulkan NGX consumes pointers to resource wrappers, not integer handles.
        if (!write(b.key, static_cast<void*>(b.resource))) return false;
        const std::string prefix = std::string(b.key) + "Subrect";
        if (!write((prefix + "BaseX").c_str(), b.x) || !write((prefix + "BaseY").c_str(), b.y) ||
            !write((prefix + "Width").c_str(), b.width) || !write((prefix + "Height").c_str(), b.height)) return false;
    }
    if (frame.temporal && (!write("Jitter.Offset.X", frame.temporal->jitterX) ||
                           !write("Jitter.Offset.Y", frame.temporal->jitterY))) return false;
    return write("DLSSNR.DepthInverted", static_cast<unsigned int>(frame.depthInverted)) &&
           write("DLSSNR.Reset", static_cast<unsigned int>(frame.reset)) &&
           write("DLSSNR.MVecScaleX", frame.temporal ? frame.temporal->motionScaleX : 1.0f) &&
           write("DLSSNR.MVecScaleY", frame.temporal ? frame.temporal->motionScaleY : 1.0f);
}

inline std::optional<VkNrRect> SourceRect(const NVSDK_NGX_Resource_VK* resource,
    std::optional<VkNrTemporalMetadata> temporal,bool before,Failure& failure)
{
    if(!ValidateImage("NR.Color",resource,false,failure))return {};
    if(before) {
        if(!temporal){Reject(failure,"NR.Color","active Pre-SR render metadata unavailable");return {};}
        const auto& rect=temporal->color;
        if(!ValidateRect("NR.Color",resource,rect.x,rect.y,rect.width,rect.height,false,failure))return {};
        return rect;
    }
    return VkNrRect{0,0,resource->Resource.ImageViewInfo.Width,resource->Resource.ImageViewInfo.Height};
}

// Dimensions, motion conventions and jitter must be observed for this evaluation.
// NVIDIA's optional subrect origins default to zero when unused (helpers_vk.h).
template<class Params>
std::optional<VkNrTemporalMetadata> ObserveTemporal(Params* p, uint32_t flags,
    uint64_t generation, uint64_t token, VkExtent2D output, const char** unavailable = nullptr)
{
    if(unavailable)*unavailable=nullptr;
    if (!p || !output.width || !output.height) {if(unavailable)*unavailable="Native parameters/output extent";return {};}
    VkNrTemporalMetadata v;
    const auto observed = [&](const char* key, auto& destination) {
        return static_cast<uint32_t>(p->Get(key, &destination)) == 1;
    };
    const auto required = [&](const char* key, auto& destination) {
        if(observed(key,destination))return true;
        if(unavailable)*unavailable=key;return false;
    };
    observed(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, v.color.x);
    observed(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, v.color.y);
    observed(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, v.depth.x);
    observed(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, v.depth.y);
    observed(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, v.motion.x);
    observed(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, v.motion.y);
    if (!required(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, v.color.width) ||
        !required(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, v.color.height) ||
        !required(NVSDK_NGX_Parameter_MV_Scale_X, v.motionScaleX) ||
        !required(NVSDK_NGX_Parameter_MV_Scale_Y, v.motionScaleY) ||
        !required(NVSDK_NGX_Parameter_Jitter_Offset_X, v.jitterX) ||
        !required(NVSDK_NGX_Parameter_Jitter_Offset_Y, v.jitterY)) return {};
    v.depth.width = v.color.width; v.depth.height = v.color.height;
    const bool low = (flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) != 0;
    v.motion.width = low ? v.color.width : output.width;
    v.motion.height = low ? v.color.height : output.height;
    v.featureFlags = flags; v.featureGeneration = generation; v.frameToken = token;
    v.depthInverted = (flags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0;
    unsigned int reset = 0;
    if (observed(NVSDK_NGX_Parameter_Reset, reset)) v.reset = reset != 0;
    if(!ValidVkNrTemporal(v)){if(unavailable)*unavailable="Native temporal bounds or non-finite scalars";return {};}
    return v;
}

// Both production and tests use this gate; failed preparation cannot call the vendor model.
template<class Params> std::optional<uint32_t> ObserveSrQuality(Params* p,std::optional<int> selectedCreation)
{
    int quality=0;
    if(selectedCreation)quality=*selectedCreation;
    else if(!p||static_cast<uint32_t>(p->Get(NVSDK_NGX_Parameter_PerfQualityValue,&quality))!=1)return {};
    if(quality<0||quality>static_cast<int>(NVSDK_NGX_PerfQuality_Value_DLAA))return {};
    return static_cast<uint32_t>(quality);
}
template<class Params, class Evaluate>
bool PrepareAndEvaluate(Params* params, const Inputs& frame, const VkTuning::Settings& applied,
                        Failure& failure, Evaluate evaluate)
{
    if (!Prepare(params, frame, applied, failure)) return false;
    evaluate();
    return true;
}
}
