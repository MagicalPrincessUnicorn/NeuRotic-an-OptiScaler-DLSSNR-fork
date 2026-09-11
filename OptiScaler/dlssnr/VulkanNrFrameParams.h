#pragma once

#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include "VulkanNrTuning.h"
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
};

struct Inputs
{
    NVSDK_NGX_Resource_VK *color, *depth, *motion, *output;
    uint32_t width, height, guideWidth, guideHeight;
    bool depthInverted, reset;

    std::array<Binding, 4> Bindings() const
    {
        return {{{"DLSSNR.Color", color, width, height, false},
                 {"DLSSNR.Depth", depth, guideWidth, guideHeight, false},
                 {"DLSSNR.MVec", motion, guideWidth, guideHeight, false},
                 {"DLSSNR.Output", output, width, height, true}}};
    }
};

template<class Params>
bool Prepare(Params* params, const Inputs& frame, const VkTuning::Settings& applied, Failure& failure)
{
    failure = {};
    if (!params) return Reject(failure, "parameter block", "missing model parameters");
    const auto bindings = frame.Bindings();
    for (const auto& b : bindings)
        if (!ValidateRect(b.key, b.resource, 0, 0, b.width, b.height, b.writable, failure)) return false;

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
        if (!write((prefix + "BaseX").c_str(), 0u) || !write((prefix + "BaseY").c_str(), 0u) ||
            !write((prefix + "Width").c_str(), b.width) || !write((prefix + "Height").c_str(), b.height)) return false;
    }
    return write("DLSSNR.DepthInverted", static_cast<unsigned int>(frame.depthInverted)) &&
           write("DLSSNR.Reset", static_cast<unsigned int>(frame.reset)) &&
           write("DLSSNR.MVecScaleX", 1.0f) && write("DLSSNR.MVecScaleY", 1.0f);
}

// Both production and tests use this gate; failed preparation cannot call the vendor model.
template<class Params, class Evaluate>
bool PrepareAndEvaluate(Params* params, const Inputs& frame, const VkTuning::Settings& applied,
                        Failure& failure, Evaluate evaluate)
{
    if (!Prepare(params, frame, applied, failure)) return false;
    evaluate();
    return true;
}
}
