#pragma once
#include "ObservationPublisher.h"

namespace Neurotic::Feed
{
// Creation chains are an existing API input. Bound the read and reject duplicate/cyclic
// descriptions. Dispatch does not use this: existing Vulkan dispatch deliberately ignores pNext.
template<class T,class Header,class Type>const T* FindFfxCreation(const Header* head,Type type)noexcept
{
    if(!Observing())return nullptr;
    const Header* visited[16]{};std::size_t count=0;const T* found=nullptr;
    while(head)
    {
        if(count==16)return nullptr;
        for(std::size_t i=0;i<count;++i)if(visited[i]==head)return nullptr;
        visited[count++]=head;
        if(head->type==type){if(found)return nullptr;found=reinterpret_cast<const T*>(head);}
        head=head->pNext;
    }
    return found;
}
// FSR2/3/FFX native structs remain at their existing source seams. A missing schema field is
// explicitly Unknown; no NGX-translated parameter or Config mirror supplies a raw value.
template<class T>void ObserveFsrDispatch(Callback& cb,const T* d)noexcept
{
    if(!cb.Active()||!d)return;
    try
    {
#define NR_FEED_FIELD(member) if constexpr(requires{d->member;})cb.Value(#member,d->member);else cb.Missing(#member)
        NR_FEED_FIELD(jitterOffset.x);NR_FEED_FIELD(jitterOffset.y);
        NR_FEED_FIELD(motionVectorScale.x);NR_FEED_FIELD(motionVectorScale.y);
        NR_FEED_FIELD(preExposure);NR_FEED_FIELD(reset);
        NR_FEED_FIELD(renderSize.width);NR_FEED_FIELD(renderSize.height);
        NR_FEED_FIELD(upscaleSize.width);NR_FEED_FIELD(upscaleSize.height);
        NR_FEED_FIELD(cameraNear);NR_FEED_FIELD(cameraFar);NR_FEED_FIELD(cameraFovAngleVertical);
        NR_FEED_FIELD(frameTimeDelta);NR_FEED_FIELD(sharpness);NR_FEED_FIELD(enableSharpening);
#undef NR_FEED_FIELD
#define NR_FEED_RESOURCE(member,kind) if constexpr(requires{d->member.resource;})cb.Resource(#member,C::SemanticKind::kind,d->member.resource);else cb.Resource(#member,C::SemanticKind::kind,nullptr)
        NR_FEED_RESOURCE(color,Color);NR_FEED_RESOURCE(output,Color);NR_FEED_RESOURCE(depth,Depth);
        NR_FEED_RESOURCE(motionVectors,Motion);NR_FEED_RESOURCE(exposure,Exposure);
        NR_FEED_RESOURCE(reactive,Mask);NR_FEED_RESOURCE(transparencyAndComposition,Mask);
#undef NR_FEED_RESOURCE
#define NR_FEED_STATE(member) if constexpr(requires{d->member.state;})cb.Value(#member ".state",d->member.state)
        NR_FEED_STATE(color);NR_FEED_STATE(output);NR_FEED_STATE(depth);NR_FEED_STATE(motionVectors);
        NR_FEED_STATE(exposure);NR_FEED_STATE(reactive);NR_FEED_STATE(transparencyAndComposition);
#undef NR_FEED_STATE
        if constexpr(requires{d->jitterOffset.x;d->jitterOffset.y;})
            cb.Jitter(C::OptionalFact<C::Vec2>::FromKnown({d->jitterOffset.x,d->jitterOffset.y},cb.Evidence()));
    }catch(...){}
}
// Creation values are copied before host mutation; publish only on the source owner's success path.
struct FsrCreationSnapshot
{
    std::optional<std::uint64_t> flags,renderWidth,renderHeight,displayWidth,displayHeight;
    bool captured=false;
    template<class T>explicit FsrCreationSnapshot(const T* d)noexcept
    {
        if(!Observing()||!d)return;
        if constexpr(requires{d->flags;})flags=static_cast<std::uint64_t>(d->flags);
        if constexpr(requires{d->maxRenderSize;}){renderWidth=d->maxRenderSize.width;renderHeight=d->maxRenderSize.height;}
        if constexpr(requires{d->displaySize;}){displayWidth=d->displaySize.width;displayHeight=d->displaySize.height;}
        if constexpr(requires{d->maxUpscaleSize;}){displayWidth=d->maxUpscaleSize.width;displayHeight=d->maxUpscaleSize.height;}
        captured=true;
    }
    void Publish(SourceDescriptor source,const void* subject)const noexcept
    {
        if(!captured||!subject)return;Callback cb(source,subject);
        const auto add=[&](std::string_view key,const std::optional<std::uint64_t>& value){if(value)cb.Value(key,*value);else cb.Missing(key);};
        add("flags",flags);add("maxRenderSize.width",renderWidth);add("maxRenderSize.height",renderHeight);
        add("displaySize.width",displayWidth);add("displaySize.height",displayHeight);
    }
};
}
