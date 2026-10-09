#pragma once
#include <dxgi1_2.h>
#include <optional>
#include <cstdint>
#include <climits>
#include <vector>
#include <algorithm>
namespace Neurotic::Presentation {
struct CopyRegion {RECT source{};LONG x=0,y=0;};
struct IncrementalPlan {bool full=true;std::optional<CopyRegion> scroll;std::vector<RECT> dirty;};
inline std::optional<IncrementalPlan> Plan(UINT width,UINT height,const DXGI_PRESENT_PARAMETERS* p,bool history) {
    IncrementalPlan plan;
    if(!p||!p->DirtyRectsCount)return plan;
    if(!history||!p->pDirtyRects||p->DirtyRectsCount>4096||!width||!height||width>LONG_MAX||height>LONG_MAX)return {};
    const auto valid=[&](const RECT& r){return r.left>=0&&r.top>=0&&r.right>r.left&&r.bottom>r.top&&r.right<=LONG(width)&&r.bottom<=LONG(height);};
    plan.full=false;
    for(UINT i=0;i<p->DirtyRectsCount;++i){if(!valid(p->pDirtyRects[i]))return {};plan.dirty.push_back(p->pDirtyRects[i]);}
    if(bool(p->pScrollRect)!=bool(p->pScrollOffset))return {};
    if(p->pScrollRect){
        if(!valid(*p->pScrollRect))return {};
        // The scroll rectangle describes the current-frame destination; the
        // signed offset maps previous pixels into it (DXGI present contract).
        const auto& r=*p->pScrollRect;const auto& o=*p->pScrollOffset;
        const auto left=(std::max)(int64_t(r.left),int64_t(o.x));
        const auto top=(std::max)(int64_t(r.top),int64_t(o.y));
        const auto right=(std::min)(int64_t(r.right),int64_t(width)+o.x);
        const auto bottom=(std::min)(int64_t(r.bottom),int64_t(height)+o.y);
        if(left<right&&top<bottom)plan.scroll=CopyRegion{{LONG(left-o.x),LONG(top-o.y),LONG(right-o.x),LONG(bottom-o.y)},LONG(left),LONG(top)};
    }
    return plan;
}
}
