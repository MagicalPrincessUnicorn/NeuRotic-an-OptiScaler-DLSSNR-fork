#pragma once
#include "MenuLayout.h"
#include <imgui/imgui_internal.h>
#include <vector>
namespace Neurotic::MenuLayout {
// Only NeuRotic's owned UI queue is projected. Retain original client positions
// for trickled events so pending events are never scaled twice after a resize.
class InputProjection {
    struct Raw { ImU32 id; ImVec2 point; };
    std::vector<Raw> raw, pending;
    ImGuiContext* owner=nullptr;
    ImVec2 previousClient{};
    Area previousCanvas{};
    bool mapped=false;
    ImVec2 ClientPoint(ImVec2 point) const {
        if(point.x<=-1.e30f || point.y<=-1.e30f)return point;
        return {(point.x-previousCanvas.origin.x)*previousClient.x/previousCanvas.size.x,
                (point.y-previousCanvas.origin.y)*previousClient.y/previousCanvas.size.y};
    }
    template<class Transform> static void History(ImGuiContext& context, Transform transform) {
        context.IO.MousePos=transform(context.IO.MousePos);
        context.IO.MousePosPrev=transform(context.IO.MousePosPrev);
        for(auto& point:context.IO.MouseClickedPos)point=transform(point);
        context.MouseLastValidPos=transform(context.MouseLastValidPos);
    }
public:
    void Reset() { raw.clear();pending.clear();owner=nullptr;previousClient={};previousCanvas={};mapped=false; }
    // ImGui producers deduplicate against IO and the last queued position. Both
    // must be client coordinates between UI frames, just like the producers.
    // Call only after Render has built cursor geometry and all UI draw data.
    void RestoreProducerSpace(ImGuiContext& context) {
        if(owner!=&context || !mapped)return;
        History(context,[&](ImVec2 point){return ClientPoint(point);});
        for(auto& event:context.InputEventsQueue) {
            if(event.Type!=ImGuiInputEventType_MousePos)continue;
            const auto found=std::find_if(raw.begin(),raw.end(),[&](const auto& item){return item.id==event.EventId;});
            if(found!=raw.end()) {event.MousePos.PosX=found->point.x;event.MousePos.PosY=found->point.y;}
        }
        mapped=false;
    }
    void Apply(ImGuiContext& context, ImVec2 client, Area canvas) {
        if(!ValidExtent(client)||!ValidExtent(canvas.size)) return;
        if(owner!=&context) { Reset();owner=&context; }
        RestoreProducerSpace(context);
        const bool resized=ValidExtent(previousCanvas.size) &&
           (!Same(previousClient,client)||!Same(previousCanvas.origin,canvas.origin)||!Same(previousCanvas.size,canvas.size));
        History(context,[&](ImVec2 point){return Mouse(point,client,canvas);});
        if(resized) {
            // A resize must not turn a held window drag into a synthetic delta.
            if(context.MovingWindow) {
                const auto size=FitSize(context.MovingWindow->Size,canvas);
                const auto position=Clamp(context.MovingWindow->Pos,canvas,size);
                context.ActiveIdClickOffset={context.IO.MousePos.x-position.x,context.IO.MousePos.y-position.y};
            }
        }
        pending.clear();pending.reserve(context.InputEventsQueue.Size);
        for(auto& event:context.InputEventsQueue) {
            if(event.Type!=ImGuiInputEventType_MousePos) continue;
            auto found=std::find_if(raw.begin(),raw.end(),[&](const auto& item){return item.id==event.EventId;});
            ImVec2 point=found==raw.end()?ImVec2{event.MousePos.PosX,event.MousePos.PosY}:found->point;
            pending.push_back({event.EventId,point});
            const auto mapped=Mouse(point,client,canvas);
            event.MousePos.PosX=mapped.x;event.MousePos.PosY=mapped.y;
        }
        raw.swap(pending);previousClient=client;previousCanvas=canvas;mapped=true;
    }
};
}
