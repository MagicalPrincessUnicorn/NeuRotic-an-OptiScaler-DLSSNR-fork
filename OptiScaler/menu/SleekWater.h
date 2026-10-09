#pragma once
#include "SleekWaterColors.h"
#include <imgui/imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace Neurotic::Sleek
{
// Same surface, rim lift and 1.15 second travel as the Hub's water transition.
inline float WaterSurface(float x,float level,float time)
{
    const float p=std::clamp(level,0.f,1.f),a=std::sin(p*3.14159265f);
    return 1.12f-1.24f*p+a*(.023f*std::sin(x*18-time*5)+.012f*std::sin(x*33+time*3)
        -.06f*(std::exp(-x*9)+std::exp(-(1-x)*9)));
}
class WaterTransition
{
    float level=0;
    double previous=0,phase=0;
    bool initialized=false,target=false,drawingLight=false;
    struct Polygon { ImDrawVert v[16]; int count=0; };
    static ImDrawVert Lerp(const ImDrawVert& a,const ImDrawVert& b,float t)
    {
        ImDrawVert v;v.pos=ImLerp(a.pos,b.pos,t);v.uv=ImLerp(a.uv,b.uv,t);
        const auto ac=ImGui::ColorConvertU32ToFloat4(a.col),bc=ImGui::ColorConvertU32ToFloat4(b.col);
        v.col=ImGui::ColorConvertFloat4ToU32(ImLerp(ac,bc,t));return v;
    }
    template<class F> static Polygon Clip(const Polygon& p,F distance)
    {
        Polygon out;if(!p.count)return out;
        auto a=p.v[p.count-1];float da=distance(a.pos);
        for(int i=0;i<p.count;++i){const auto b=p.v[i];const float db=distance(b.pos);
            if((da>=0)!=(db>=0))out.v[out.count++]=Lerp(a,b,da/(da-db));
            if(db>=0)out.v[out.count++]=b;
            a=b;da=db;
        }return out;
    }
public:
    void Reset() { initialized=false;level=0;previous=phase=0; }
    void Update(bool light,bool reduced,double now)
    {

        if(!initialized || reduced || !std::isfinite(now) || now<previous) {
            level=light?1.f:0.f;phase=0;initialized=true;
        } else {
            const float dt=static_cast<float>((std::max)(0.,now-previous));
            const float goal=target?1.f:0.f;
            if(level<goal)level=(std::min)(goal,level+dt/1.15f);
            else if(level>goal)level=(std::max)(goal,level-dt/1.15f);
            if(level>0 && level<1)phase+=dt;
        }
        target=light;previous=std::isfinite(now)?now:0;
        if(!Active()) drawingLight=light;
    }
    float Level() const { return level; }
    bool DrawingLight() const { return drawingLight; }
    bool Active() const { return initialized && level!=(target?1.f:0.f); }
    static bool Owns(const ImGuiWindow* window,const ImGuiWindow* root)
    {
        // Popups/children submitted by this menu have an explicit Begin ancestry.
        for(auto* w=window;w;w=w->ParentWindowInBeginStack)if(w==root)return true;
        return false;
    }
    void Transform(ImDrawList& list,const ImRect& bounds,const WaterColors& colors) const
    {
        if(!Active() || bounds.GetWidth()<=0 || bounds.GetHeight()<=0)return;
        // Callback payload offsets belong to the original list. Leave a callback
        // list intact rather than altering its callback's geometry contract.
        for(const auto& cmd:list.CmdBuffer)if(cmd.UserCallback)return;
        ImVector<ImDrawVert> vertices;ImVector<ImDrawIdx> indices;ImVector<ImDrawCmd> commands;
        vertices.reserve(list.VtxBuffer.Size*2);indices.reserve(list.IdxBuffer.Size*2);
        const float amplitude=std::sin(level*3.14159265f),time=static_cast<float>(phase);
        const float width=bounds.GetWidth(),height=bounds.GetHeight();
        const float step=(std::max)(4.f,width/96.f),band=(std::max)(2.f,height*.004f);
        auto surface=[&](float x){return bounds.Min.y+height*WaterSurface((x-bounds.Min.x)/width,level,time);};
        float surfaceMin=bounds.Max.y+height,surfaceMax=bounds.Min.y-height;
        for(float x=bounds.Min.x;x<=bounds.Max.x+step;x+=step){const float y=surface(x);surfaceMin=(std::min)(surfaceMin,y);surfaceMax=(std::max)(surfaceMax,y);}
        const float rippleReach=height*.06f;
        for(const auto& source:list.CmdBuffer)
        {
            if(!source.ElemCount)continue;
            ImRect clip({source.ClipRect.x,source.ClipRect.y},{source.ClipRect.z,source.ClipRect.w});clip.ClipWith(bounds);
            if(clip.IsInverted())continue;
            auto command=source;command.ClipRect={clip.Min.x,clip.Min.y,clip.Max.x,clip.Max.y};
            command.IdxOffset=indices.Size;command.VtxOffset=vertices.Size;command.ElemCount=0;
            commands.push_back(command);
            auto append=[&](const ImDrawVert& a,const ImDrawVert& b,const ImDrawVert& c) {
                if(vertices.Size-commands.back().VtxOffset+3>65535){auto next=command;next.VtxOffset=vertices.Size;next.IdxOffset=indices.Size;next.ElemCount=0;commands.push_back(next);}
                const unsigned base=vertices.Size-commands.back().VtxOffset;
                vertices.push_back(a);vertices.push_back(b);vertices.push_back(c);
                indices.push_back(static_cast<ImDrawIdx>(base));indices.push_back(static_cast<ImDrawIdx>(base+1));indices.push_back(static_cast<ImDrawIdx>(base+2));commands.back().ElemCount+=3;
            };
            auto emit=[&](const Polygon& p,bool light,float slope,float intercept)
            {
                if(p.count<3)return;
                ImDrawVert transformed[16];
                for(int i=0;i<p.count;++i){auto v=p.v[i];const float distance=v.pos.y-(slope*v.pos.x+intercept);
                    if(light!=drawingLight)v.col=colors.Opposite(v.col);
                    // A compact refracted band. It vanishes exactly at the shared
                    // water edge and clip boundaries, so adjacent regions never tear.
                    const float ripple=amplitude*std::exp(-std::abs(distance)/((std::max)(1.f,height*.025f)));
                    const float edge=std::clamp((std::min)({v.pos.x-clip.Min.x,clip.Max.x-v.pos.x,v.pos.y-clip.Min.y,clip.Max.y-v.pos.y})/6.f,0.f,1.f);
                    const float bend=std::sin(distance/band)*ripple*edge;
                    v.pos.x+=std::sin((v.pos.y-bounds.Min.y)/height*55+time*6)*width*.0025f*bend;
                    // Keep the dividing surface fixed; distort the live menu on either side.
                    v.pos.y+=std::sin((v.pos.x-bounds.Min.x)/width*25-time*7)*height*.002f*bend;
                    v.pos.x=std::clamp(v.pos.x,clip.Min.x,clip.Max.x);v.pos.y=std::clamp(v.pos.y,clip.Min.y,clip.Max.y);
                    const float foam=amplitude*(std::max)(0.f,1-std::abs(distance)/band)*.27f;
                    auto color=ImGui::ColorConvertU32ToFloat4(v.col);
                    color.x+=(.48f-color.x)*foam;color.y+=(.72f-color.y)*foam;color.z+=(.88f-color.z)*foam;
                    v.col=ImGui::ColorConvertFloat4ToU32(color);transformed[i]=v;
                }
                for(int i=1;i<p.count-1;++i){
                    append(transformed[0],transformed[i],transformed[i+1]);
                }
            };
            for(unsigned index=0;index+2<source.ElemCount;index+=3){
                Polygon original;original.count=3;
                for(int v=0;v<3;++v)original.v[v]=list.VtxBuffer[source.VtxOffset+list.IdxBuffer[source.IdxOffset+index+v]];
                original=Clip(original,[&](ImVec2 p){return p.y-clip.Min.y;});original=Clip(original,[&](ImVec2 p){return clip.Max.y-p.y;});
                original=Clip(original,[&](ImVec2 p){return p.x-clip.Min.x;});original=Clip(original,[&](ImVec2 p){return clip.Max.x-p.x;});
                if(original.count<3)continue;
                float minY=original.v[0].pos.y,maxY=minY;
                for(int v=1;v<original.count;++v){minY=(std::min)(minY,original.v[v].pos.y);maxY=(std::max)(maxY,original.v[v].pos.y);}
                // Most text/control triangles are far from the surface. Preserve
                // their exact shape without rim tessellation or transcendental math.
                if(maxY<surfaceMin-rippleReach || minY>surfaceMax+rippleReach) {
                    const bool light=minY>surfaceMax;
                    if(light!=drawingLight)for(int v=0;v<original.count;++v)original.v[v].col=colors.Opposite(original.v[v].col);
                    for(int v=1;v<original.count-1;++v)append(original.v[0],original.v[v],original.v[v+1]);
                    continue;
                }
                float minX=clip.Max.x,maxX=clip.Min.x;for(int v=0;v<original.count;++v){minX=(std::min)(minX,original.v[v].pos.x);maxX=(std::max)(maxX,original.v[v].pos.x);}
                minX=(std::max)(minX,clip.Min.x);maxX=(std::min)(maxX,clip.Max.x);
                const int first=static_cast<int>(std::floor((minX-bounds.Min.x)/step));
                for(float x=bounds.Min.x+first*step;x<maxX;x+=step){const float left=(std::max)(x,clip.Min.x),right=(std::min)(x+step,clip.Max.x);
                    auto slab=Clip(original,[&](ImVec2 p){return p.x-left;});slab=Clip(slab,[&](ImVec2 p){return right-p.x;});if(slab.count<3)continue;
                    const float y=surface(x),slope=(surface(x+step)-y)/step,intercept=y-slope*x;
                    // Subdivide the rim so foam/refraction remain local even on huge background triangles.
                    const float cuts[]={-1e20f,-band,0,band,1e20f};
                    for(int region=0;region<4;++region){auto piece=Clip(slab,[&](ImVec2 p){return p.y-(slope*p.x+intercept)-cuts[region];});
                        piece=Clip(piece,[&](ImVec2 p){return cuts[region+1]-(p.y-(slope*p.x+intercept));});emit(piece,region>=2,slope,intercept);}
                }
            }
        }
        list.VtxBuffer.swap(vertices);list.IdxBuffer.swap(indices);list.CmdBuffer.swap(commands);
    }
    void Render(ImDrawData* data,ImGuiWindow* root,const WaterColors& colors,bool settle=false) const
    {
        if(!data || !root || !Active())return;
        for(auto* window:GImGui->Windows)if(window->Active && Owns(window,root))
            for(auto* list:data->CmdLists)if(list==window->DrawList)
            {
                if(settle) {
                    // Reduce motion can be clicked after this frame was drawn.
                    // Recolor its fixed source palette now; leave geometry exact.
                    if(target!=drawingLight)for(auto& vertex:list->VtxBuffer)vertex.col=colors.Opposite(vertex.col);
                    continue;
                }
                // A dropdown/tooltip may extend outside the menu; its own root
                // supplies the water bounds instead of truncating the popup.
                Transform(*list,window->RootWindow->Rect(),colors);
            }
        data->TotalIdxCount=data->TotalVtxCount=0;
        for(auto* list:data->CmdLists){data->TotalIdxCount+=list->IdxBuffer.Size;data->TotalVtxCount+=list->VtxBuffer.Size;}
    }
};
}
