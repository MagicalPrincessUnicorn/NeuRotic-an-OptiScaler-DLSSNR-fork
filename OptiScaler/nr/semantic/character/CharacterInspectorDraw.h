// SPDX-License-Identifier: MIT
// Adapted from the 2026-10-02 Character Inspector ImGui reference; notice in
// docs/character-inspector/REFERENCE_LICENSE.txt. Uses the existing ImGui only.
#pragma once
#include "CharacterObservation.h"
#include "CharacterClassNames.h"
#include "../object_rules/ObjectRules.h"
#include <imgui/imgui.h>
#include <cstdio>

namespace Neurotic::Semantic::Character {
struct ViewportRect {float x=0,y=0,width=0,height=0;};
struct DrawStyle {
    ImU32 color=0,estimatedColor=0;
    float thickness=1.5f,labelHeight=16.0f;
    std::size_t maxLabels=8;
    bool bodyBoxes=true,torsoBand=false,editRules=false;
    std::string_view label="Person";
    std::shared_ptr<const Rules::Snapshot> rules;
};
inline Admission EmitCharacterGeometry(ImDrawList& list,const Snapshot& snapshot,
                                       ViewportRect viewport,const DrawStyle& style) {
    if(!ValidSnapshot(snapshot)) return Admission::Malformed;
    for(float f:{viewport.x,viewport.y,viewport.width,viewport.height,style.thickness,style.labelHeight})
        if(!std::isfinite(f)) return Admission::Malformed;
    if(viewport.width<=0||viewport.height<=0||viewport.width>32768||viewport.height>32768||
       std::abs(viewport.x)>1e6||std::abs(viewport.y)>1e6||style.thickness<=0||style.thickness>16||
       style.labelHeight<=0||style.labelHeight>128) return Admission::Malformed;
    if(!snapshot.count || (!style.bodyBoxes&&!style.torsoBand&&!style.maxLabels)) return Admission::Ready;
    const auto point=[&](double x,double y) {
        return ImVec2(viewport.x+static_cast<float>(x)*viewport.width,
                      viewport.y+static_cast<float>(y)*viewport.height);
    };
    list.PushClipRect({viewport.x,viewport.y},{viewport.x+viewport.width,viewport.y+viewport.height},true);
    struct ClipScope {ImDrawList& list;~ClipScope(){list.PopClipRect();}} clip{list};
    const auto label=SanitizeLabel(style.label);
    std::array<unsigned,512> emitted{};
    for(std::size_t i=0;i<snapshot.count;++i) {
        const auto& item=snapshot.items[i];
        // A detector query can feed many saved rules; draw only the winning style
        // for each observation so a 512-rule catalog cannot multiply GPU geometry.
        const Rules::Rule* selected=nullptr;size_t selectedIndex=0;
        if(style.rules)for(size_t r=0;r<style.rules->rules.size();++r){
            const auto& candidate=style.rules->rules[r];
            if(!Rules::Eligible(candidate,item.classId,item.score,
                item.confirmedNs?std::min(item.confirmedNs,snapshot.key.captureNs):snapshot.key.captureNs)||emitted[r]>=candidate.maxInstances)continue;
            if(!selected||candidate.priority>selected->priority||(candidate.priority==selected->priority&&candidate.id<selected->id)){selected=&candidate;selectedIndex=r;}
        }
        if(style.rules&&!selected)continue;
        if(selected)++emitted[selectedIndex];
        {const auto* rule=selected;
        const auto a=point(item.body.x0,item.body.y0),b=point(item.body.x1,item.body.y1);
        const ImU32 palette[]={IM_COL32(58,220,255,255),IM_COL32(181,131,255,255),IM_COL32(91,235,165,255),IM_COL32(255,204,99,255)};
        auto color=palette[(item.id-1)%4],labelColor=color;float thickness=style.thickness;
        bool box=style.bodyBoxes,showLabel=i<std::min(style.maxLabels,MaxInstances);
        if(rule){const auto convert=[&](unsigned c){return IM_COL32((c>>16)&255,(c>>8)&255,c&255,int(255*rule->opacity));};color=convert(rule->boxRgb);labelColor=convert(rule->labelRgb);thickness=rule->lineWidth/std::max(.25f,ImGui::GetIO().DisplayFramebufferScale.y);box=box&&rule->showBox;showLabel=showLabel&&rule->showLabel;
            const auto mouse=ImGui::GetIO().MousePos;
            if(style.editRules&&Rules::Selection().armed&&!ImGui::GetIO().WantCaptureMouse&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&mouse.x>=a.x&&mouse.x<=b.x&&mouse.y>=a.y&&mouse.y<=b.y){Rules::Selection().pending=rule->id;Rules::Selection().armed=false;}
        }
        if(box) list.AddRect(a,b,color,2,0,thickness);
        if(showLabel) {
            char id[48]{};std::snprintf(id,sizeof(id)," #%llu  %.0f%%",static_cast<unsigned long long>(item.id),item.score*100);
            const auto name=item.geometry==BoxGeometry::DetectorBox&&item.classId<ObjectClassNames.size() ?
                SanitizeLabel(ObjectClassNames[item.classId]):label;
            const auto text=rule?SanitizeLabel(rule->label)+id:name+(item.geometry==BoxGeometry::EstimatedPersonRegion?" region":"")+id;
            const auto size=ImGui::GetFont()->CalcTextSizeA(style.labelHeight,1e6f,0,text.c_str());
            const float padding=style.labelHeight*.2f;
            const ImVec2 at{a.x,std::max(viewport.y,a.y-style.labelHeight-padding*2)};
            list.AddRectFilled(at,{at.x+size.x+padding*2,at.y+size.y+padding*2},IM_COL32(8,12,22,rule?int(220*rule->opacity):220),2);
            list.AddText(ImGui::GetFont(),style.labelHeight,{at.x+padding,at.y+padding},labelColor,
                         text.c_str(),text.c_str()+text.size());
        }
        }
        if(style.torsoBand&&item.hasPose) {
            const auto band=TorsoBand(item.pose,.1,.5);
            if(band) list.AddRect(point(band->x0,band->y0),point(band->x1,band->y1),
                                  style.estimatedColor,0,0,style.thickness);
        }
    }
    return Admission::Ready;
}
// Value admission follows source/capture/queue qualification by CharacterRuntime.
inline Admission DrawCharacterOverlay(ImDrawList& list,const Snapshot& snapshot,
    const DisplayContext& display,ViewportRect viewport,const DrawStyle& style) {
    const auto admission=Admit(snapshot,display);
    return admission==Admission::Ready ? EmitCharacterGeometry(list,snapshot,viewport,style):admission;
}
// Explicit editor-canvas entry point. It never changes provenance or fabricates a key.
inline Admission DrawFixtureObservation(ImDrawList& list,const Snapshot& snapshot,
    ViewportRect viewport,const DrawStyle& style,bool enabled) {
    if(!enabled) return Admission::Disabled;
    if(snapshot.origin!=Origin::Fixture) return Admission::NotLive;
    return EmitCharacterGeometry(list,snapshot,viewport,style);
}
inline Snapshot MakeCharacterFixture() noexcept {
    Snapshot fixture{};fixture.count=2; // Default Origin::Fixture and absent live key.
    fixture.items[0].id=1;fixture.items[0].body={.12,.15,.4,.9};fixture.items[0].score=.8;
    fixture.items[1].id=2;fixture.items[1].body={.58,.25,.85,.9};fixture.items[1].score=.7;
    for(auto& item:fixture.items) {
        if(!item.id) continue;
        item.hasPose=true;
        const auto& box=item.body;
        const double w=box.x1-box.x0,h=box.y1-box.y0;
        item.pose[11]={{box.x0+w*.25,box.y0+h*.2},1,1};
        item.pose[12]={{box.x0+w*.75,box.y0+h*.2},1,1};
        item.pose[23]={{box.x0+w*.3,box.y0+h*.6},1,1};
        item.pose[24]={{box.x0+w*.7,box.y0+h*.6},1,1};
    }
    return fixture;
}
}
