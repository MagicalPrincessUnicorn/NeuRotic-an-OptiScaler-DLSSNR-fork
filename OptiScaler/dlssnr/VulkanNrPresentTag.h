#pragma once
#include "VulkanPresentGuides.h"
#include <sl_core_types.h>
namespace DlssNr {
struct VkNrPresentTagParse {
    bool observed=false;
    std::optional<VkNrPresentGuideTag> tag;
    const char* reason="no public Backbuffer tag";
};
// Only explicit public Backbuffer semantics identify the image to be presented.
// Sparse descriptor fields may be filled from exact WSI image identity; supplied
// contradictions and partial tags refuse. No resource state/access is inferred.
template<class Resolve>
VkNrPresentTagParse ParseVkNrPresentTag(uint64_t provider,uint64_t frame,uint32_t viewport,
    const sl::ResourceTag* tags,uint32_t count,bool succeeded,Resolve&& resolve)
{
    VkNrPresentTagParse out;
    if(!tags||count>64){out.observed=true;out.reason="missing or oversized public tag batch";return out;}
    const sl::ResourceTag* backbuffer=nullptr;
    for(uint32_t i=0;i<count;++i)if(tags[i].type==sl::kBufferTypeBackbuffer){
        out.observed=true;
        if(backbuffer){out.reason="ambiguous public Backbuffer tags";return out;}backbuffer=&tags[i];
    }
    if(!backbuffer)return out;
    if(!succeeded||!provider||viewport==UINT32_MAX){out.reason="public tagging failed or identity unavailable";return out;}
    if(backbuffer->lifecycle!=sl::eValidUntilPresent){out.reason="public Backbuffer lifetime does not reach Present";return out;}
    const auto* r=backbuffer->resource;
    if(backbuffer->structVersion!=1||backbuffer->next||!r||r->structVersion!=1||r->next||
        r->type!=sl::ResourceType::eTex2d||!r->native||backbuffer->extent.left||backbuffer->extent.top){
        out.reason="public Backbuffer description unsupported or removed";return out;
    }
    auto target=resolve(reinterpret_cast<VkImage>(r->native),static_cast<VkFormat>(r->nativeFormat),VkExtent2D{r->width,r->height});
    if(!target||(backbuffer->extent.width&&backbuffer->extent.width!=target->output.width)||
        (backbuffer->extent.height&&backbuffer->extent.height!=target->output.height)){
        out.reason="public Backbuffer does not match an exact acquired image";return out;
    }
    VkNrPresentGuideTag tag;tag.providerGeneration=provider;tag.frameToken=frame;tag.viewport=viewport;
    tag.target=*target;tag.image=reinterpret_cast<VkImage>(r->native);out.tag=tag;out.reason="public Backbuffer association observed";
    return out;
}
}
