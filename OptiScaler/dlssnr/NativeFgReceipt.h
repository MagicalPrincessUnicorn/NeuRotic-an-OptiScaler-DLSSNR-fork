#pragma once
#include "NativeIdentity.h"
#include <sl.h>
#include <array>
#include <cstdint>

namespace DlssNr::NativeFgReceipt
{
// Observation values only. No resources/interfaces or rendering permissions.
struct Context
{
    uint64_t provider = 0, epoch = 0, key = 0, viewport = 0, sequence = 0;
    bool operator==(const Context&) const = default;
};
struct Tag
{
    uint32_t index = 0, type = 0;
    uint64_t version = 0, resourceVersion = 0;
    uint32_t lifecycle = 0, state = UINT32_MAX;
    uint32_t left = 0, top = 0, width = 0, height = 0;
    uint32_t optionalWidth = 0, optionalHeight = 0, optionalFormat = 0;
    uintptr_t supplied = 0, canonical = 0;
    uint64_t descriptorWidth = 0;
    uint32_t descriptorHeight = 0, descriptorFormat = 0, descriptorMips = 0, descriptorLayers = 0;
    HRESULT nativeResult = E_PENDING;
    bool resourceSupplied = false, resourceSchemaKnown = false, descriptorKnown = false;
};
struct Tags
{
    std::array<Tag,16> values {};
    uint32_t count = 0, declaredCount = 0, inspected = 0, omitted = 0, unknownSchemas = 0;
    uintptr_t commandList = 0;
    Context context {};
    bool legacy = false;
};
inline bool Relevant(sl::BufferType type) noexcept
{
    return type == sl::kBufferTypeBackbuffer || type == sl::kBufferTypeHUDLessColor ||
        type == sl::kBufferTypeUIColorAndAlpha || type == sl::kBufferTypeUIAlpha;
}
inline uint64_t Mix(uint64_t hash, uint64_t value) noexcept
{ return (hash ^ value) * 1099511628211ull; }
// Pointer values are deliberately absent from the sampling signature: ordinary
// alternating backbuffers must not consume the change-sample budget every frame.
inline uint64_t Layout(const sl::ResourceTag* input, uint32_t count, Context context, bool legacy) noexcept
{
    uint64_t hash = Mix(Mix(Mix(Mix(1469598103934665603ull,context.provider),context.epoch),context.viewport),legacy);
    hash = Mix(hash,count);
    for (uint32_t i=0;input && i<count && i<32;++i)
    {
        const auto& tag=input[i];
        hash=Mix(hash,tag.structVersion);
        if (tag.structVersion != sl::kStructVersion1 || tag.structType != sl::ResourceTag::s_structType) continue;
        if (!Relevant(tag.type)) continue;
        hash=Mix(hash,tag.type);hash=Mix(hash,static_cast<uint32_t>(tag.lifecycle));
        hash=Mix(hash,tag.extent.left);hash=Mix(hash,tag.extent.top);
        hash=Mix(hash,tag.extent.width);hash=Mix(hash,tag.extent.height);
        hash=Mix(hash,tag.resource!=nullptr);
        if (tag.resource)
        {
            hash=Mix(hash,tag.resource->structVersion);
            if (tag.resource->structVersion==sl::kStructVersion1 && tag.resource->structType==sl::Resource::s_structType)
            { hash=Mix(hash,tag.resource->native!=nullptr);hash=Mix(hash,tag.resource->state); }
        }
    }
    return hash;
}
// Caller serializes this bounded diagnostic policy only; it is never consulted
// by model, Present, tag publication, queue or resource admission.
struct Samples
{
    static constexpr uint32_t limit = 64;
    uint64_t attempts = 0, previous = 0;
    uint32_t emitted = 0;
    bool Take(uint64_t signature) noexcept
    {
        const auto call=++attempts;
        const bool changed=signature!=previous;
        previous=signature;
        if (emitted>=limit || !(call<=8 || changed || call%600==0)) return false;
        ++emitted;return true;
    }
};
// Called only while the foreign callback's descriptors/resources are valid,
// before upstream. All temporary COM references end inside this function.
inline Tags Capture(const sl::ResourceTag* input,uint32_t count,sl::RenderAPI api,
                    Context context,bool legacy,void* commandList)
{
    Tags out;out.context=context;out.legacy=legacy;out.declaredCount=count;
    out.commandList=reinterpret_cast<uintptr_t>(commandList);
    if (!input) { out.omitted=count;return out; }
    for (uint32_t i=0;i<count && i<32;++i)
    {
        ++out.inspected;
        const auto& tag=input[i];
        if (tag.structVersion!=sl::kStructVersion1 || tag.structType!=sl::ResourceTag::s_structType)
        { ++out.unknownSchemas;continue; }
        if (!Relevant(tag.type)) continue;
        if (out.count==out.values.size()) { ++out.omitted;continue; }
        auto& value=out.values[out.count++];
        value.index=i;value.type=tag.type;value.version=tag.structVersion;
        value.lifecycle=static_cast<uint32_t>(tag.lifecycle);
        value.left=tag.extent.left;value.top=tag.extent.top;
        value.width=tag.extent.width;value.height=tag.extent.height;
        value.resourceSupplied=tag.resource!=nullptr;
        if (!tag.resource) continue; // explicit null/replacement, not a missing observation
        value.resourceVersion=tag.resource->structVersion;
        value.resourceSchemaKnown=tag.resource->structVersion==sl::kStructVersion1 &&
            tag.resource->structType==sl::Resource::s_structType;
        if (!value.resourceSchemaKnown) continue;
        const auto& resource=*tag.resource;
        value.supplied=reinterpret_cast<uintptr_t>(resource.native);value.state=resource.state;
        value.optionalWidth=resource.width;value.optionalHeight=resource.height;value.optionalFormat=resource.nativeFormat;
        if (api!=sl::RenderAPI::eD3D12 || !resource.native) continue;
        const auto native=NativeIdentity::Resolve<ID3D12Resource>(reinterpret_cast<IUnknown*>(resource.native));
        value.nativeResult=native.result;
        if (!native.object) continue;
        Microsoft::WRL::ComPtr<IUnknown> identity;
        value.nativeResult=native.object.As(&identity);
        if (FAILED(value.nativeResult) || !identity) continue;
        value.canonical=reinterpret_cast<uintptr_t>(identity.Get());
        const auto descriptor=native.object->GetDesc();
        value.descriptorKnown=true;value.descriptorWidth=descriptor.Width;value.descriptorHeight=descriptor.Height;
        value.descriptorFormat=descriptor.Format;value.descriptorMips=descriptor.MipLevels;
        value.descriptorLayers=descriptor.DepthOrArraySize;
    }
    if (count>out.inspected) out.omitted+=count-out.inspected;
    return out;
}
// A same-thread prior returned Present is a correlation observation, NOT an
// attestation that the provider's fence retires these particular UI resources.
struct ReturnHistory
{
    Context previous {};
    uint32_t depth = 0;
    HRESULT result = E_PENDING;
    void Enter() noexcept { ++depth; }
    void Leave(Context context,HRESULT returned,bool forwarded) noexcept
    {
        if (depth) --depth;
        previous=forwarded ? context : Context{};result=forwarded ? returned : E_PENDING;
    }
    bool Candidate(uint64_t provider,uint64_t epoch,uint64_t viewport) const noexcept
    {
        return !depth && result==S_OK && previous.key && previous.viewport &&
            previous.provider==provider && previous.epoch && previous.epoch==epoch && previous.viewport==viewport;
    }
};
}
