#pragma once
#include <imgui/imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Neurotic::Semantic::Character {
// ImGui's packed vertex colors cannot carry scRGB values above one. Use the
// same fixed 80-nit reference white for PQ and scRGB, without changing menus.
// Policy 4 is SDR with an sRGB render-target view (hardware performs encoding).
inline ImU32 InspectorOutputColor(ImU32 color,unsigned policy) noexcept {
    if(policy!=1&&policy!=2&&policy!=4)return color;
    const auto linear=[](double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
    double r=linear(((color>>IM_COL32_R_SHIFT)&255)/255.0);
    double g=linear(((color>>IM_COL32_G_SHIFT)&255)/255.0);
    double b=linear(((color>>IM_COL32_B_SHIFT)&255)/255.0);
    if(policy==1){
        // Same Rec.709 -> Rec.2020 matrix and ST.2084 transfer as PresentColor.hlsl.
        const double rr=.627403895934699*r+.329283038377884*g+.043313065687417*b;
        const double gg=.069097289358232*r+.919540395075459*g+.011362315566309*b;
        const double bb=.016391438875150*r+.088013307877226*g+.895595253247624*b;
        const auto pq=[](double v){
            const double p=std::pow(std::clamp(v/125.0,0.0,1.0),2610.0/16384.0);
            return std::pow((3424.0/4096.0+(2413.0/128.0)*p)/(1.0+(2392.0/128.0)*p),2523.0/32.0);
        };
        r=pq(rr);g=pq(gg);b=pq(bb);
    }
    const auto byte=[](double v){return static_cast<ImU32>(std::clamp(v,0.0,1.0)*255.0+.5);};
    return (color&IM_COL32_A_MASK)|(byte(r)<<IM_COL32_R_SHIFT)|
        (byte(g)<<IM_COL32_G_SHIFT)|(byte(b)<<IM_COL32_B_SHIFT);
}

struct InspectorColorRange {ImDrawList* list=nullptr;int first=0,last=0;unsigned policy=0;};
struct InspectorFrameColors {
    ImGuiContext* context=nullptr;int frame=-1;bool converted=false;
    std::vector<InspectorColorRange> ranges;
};
inline InspectorFrameColors& InspectorColors(){static InspectorFrameColors value;return value;}
inline void BeginInspectorColors(){
    auto& state=InspectorColors();state.context=ImGui::GetCurrentContext();
    state.frame=ImGui::GetFrameCount();state.converted=false;state.ranges.clear();
}
inline bool InspectorColorsCurrent() noexcept {
    const auto& state=InspectorColors();
    return ImGui::GetCurrentContext()&&state.context==ImGui::GetCurrentContext()&&state.frame==ImGui::GetFrameCount();
}
inline void RecordInspectorColors(ImDrawList& list,int first,unsigned policy){
    if(first<0||first>=list.VtxBuffer.Size)return;
    auto& state=InspectorColors();
    if(!InspectorColorsCurrent()){
        BeginInspectorColors();
    }
    state.ranges.push_back({&list,first,list.VtxBuffer.Size,policy});
}
inline bool InspectorOwnsColorVertex(const ImDrawList* list,int index) noexcept {
    if(!InspectorColorsCurrent())return false;
    for(const auto& range:InspectorColors().ranges)
        if(range.list==list&&index>=range.first&&index<range.last)return true;
    return false;
}
inline void FinalizeInspectorColors(ImDrawData* data) noexcept {
    if(!data||!InspectorColorsCurrent()||InspectorColors().converted)return;
    auto& state=InspectorColors();
    struct Converted {ImU32 input=0,output=0;unsigned policy=0;};
    std::array<Converted,64> cache{};std::size_t count=0;
    // Only lists in the finalized frame are eligible; cached pointers from an
    // earlier frame/context are never dereferenced.
    for(auto* list:data->CmdLists)for(const auto& range:state.ranges)if(range.list==list)
        for(int i=range.first;i<std::min(range.last,list->VtxBuffer.Size);++i){
            auto& color=list->VtxBuffer[i].col;std::size_t selected=0;
            while(selected<count&&(cache[selected].input!=color||cache[selected].policy!=range.policy))++selected;
            if(selected<count)color=cache[selected].output;
            else {
                const auto output=InspectorOutputColor(color,range.policy);
                if(count<cache.size())cache[count++]={color,output,range.policy};
                color=output;
            }
        }
    state.converted=true;
}
}
