#pragma once
#include <imgui/imgui.h>
#include <unordered_map>

namespace Neurotic::Sleek
{
// Per-frame palette correspondence, including animated widget blends. Only RGB
// changes: disabled/AA/reveal alpha and image content keep their original meaning.
class WaterColors
{
    std::unordered_map<ImU32, ImU32> pairs;
    static ImU32 Rgb(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c) & ~IM_COL32_A_MASK; }
public:
    void Set(const ImVec4* dark, const ImVec4* light, bool drawingLight)
    {
        pairs.clear();
        for (int i=0;i<ImGuiCol_COUNT;++i)
            if (dark[i].w>0 && light[i].w>0)
                pairs.try_emplace(Rgb(drawingLight?light[i]:dark[i]),Rgb(drawingLight?dark[i]:light[i]));
    }
    ImU32 Opposite(ImU32 c) const
    {
        const auto it=pairs.find(c & ~IM_COL32_A_MASK);
        return it==pairs.end()?c:it->second|(c & IM_COL32_A_MASK);
    }
    void Blend(const ImVec4& a,const ImVec4& b,float t,const ImVec4& result)
    {
        if(pairs.size()>=2048) return; // bounded even with large diagnostic tables
        const auto ca=ImGui::ColorConvertFloat4ToU32(a),cb=ImGui::ColorConvertFloat4ToU32(b);
        const auto oa=Opposite(ca),ob=Opposite(cb);
        if(oa==ca && ob==cb) return;
        const auto x=ImGui::ColorConvertU32ToFloat4(oa),y=ImGui::ColorConvertU32ToFloat4(ob);
        pairs.try_emplace(Rgb(result),Rgb({x.x+(y.x-x.x)*t,x.y+(y.y-x.y)*t,x.z+(y.z-x.z)*t,result.w}));
    }
};
inline thread_local WaterColors* waterColorCapture=nullptr;
}
