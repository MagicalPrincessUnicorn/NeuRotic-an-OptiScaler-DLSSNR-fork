#include "nr/semantic/character/CharacterInspectorOverlay.h"
#include "menu/UiBrightness.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace Neurotic::Semantic::Character;
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static unsigned Channel(ImU32 color,unsigned shift){return (color>>shift)&255;}
static double PqNits(unsigned value){
    const double p=std::pow(value/255.0,32.0/2523.0);
    return 10000*std::pow(std::max(p-3424.0/4096.0,0.0)/(2413.0/128.0-(2392.0/128.0)*p),16384.0/2610.0);
}
int main(){try{
    const auto white=IM_COL32(255,255,255,255),gray=IM_COL32(128,128,128,157);
    Check(InspectorOutputColor(gray,0)==gray,"SDR UNORM is byte preserving");
    Check(InspectorOutputColor(gray,99)==gray,"unknown encoding does not invent conversion");
    const auto pqWhite=InspectorOutputColor(white,1);
    Check(Channel(pqWhite,IM_COL32_R_SHIFT)==124&&Channel(pqWhite,IM_COL32_G_SHIFT)==124&&Channel(pqWhite,IM_COL32_B_SHIFT)==124,"PQ white is the 80-nit code, not peak white");
    Check(std::abs(PqNits(Channel(pqWhite,IM_COL32_R_SHIFT))-80)<2,"quantized PQ white stays near 80 nits");
    Check(InspectorOutputColor(white,2)==white,"scRGB one is the same 80-nit reference");
    Check(Channel(InspectorOutputColor(gray,2),IM_COL32_R_SHIFT)==55,"scRGB receives linear gray");
    Check(InspectorOutputColor(gray,4)==InspectorOutputColor(gray,2),"sRGB attachment receives linear vertex values");
    const auto red=InspectorOutputColor(IM_COL32(255,0,0,255),1);
    Check(std::abs(PqNits(Channel(red,IM_COL32_R_SHIFT))-80*.627403895934699)<2&&
          std::abs(PqNits(Channel(red,IM_COL32_G_SHIFT))-80*.069097289358232)<.4&&
          std::abs(PqNits(Channel(red,IM_COL32_B_SHIFT))-80*.016391438875150)<.1,"PQ palette primaries are converted to Rec2020");
    for(unsigned policy:{0u,1u,2u,4u})for(unsigned alpha=0;alpha<256;++alpha){
        const auto input=IM_COL32(58,220,255,alpha),output=InspectorOutputColor(input,policy);
        Check((input&IM_COL32_A_MASK)==(output&IM_COL32_A_MASK),"conversion preserves every coverage alpha");
    }

    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize={2560,1440};io.DisplayFramebufferScale={2,2};io.DeltaTime=1.f/60;
    io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();
    InspectorSettings settings;settings.enabled=true;settings.torsoEstimate=true;
    settings.maximumLabels=8;settings.labelScalePercent=150;
    for(unsigned policy:{0u,1u,2u,4u}){
        ImGui::NewFrame();BeginInspectorColors();auto& list=*ImGui::GetBackgroundDrawList();
        // Ordinary UI neighbors share the exact same background draw list.
        list.AddRectFilled({0,0},{10,10},IM_COL32(50,60,70,200));const int first=list.VtxBuffer.Size;
        DisplayContext display;display.enabled=true;display.fgActive=true;display.postFgQualified=true;
        display.frameSequence=1;display.nowNs=1'000'000'000;display.colorPolicy=policy;
        auto live=MakeCharacterFixture();live.origin=Origin::LiveModel;live.key={display.epoch,1,display.nowNs};
        ImGui::GetStyle().Colors[ImGuiCol_Text]={.1f,.2f,.3f,.4f};
        Check(InspectorDrawStyle(settings,*ImGui::GetMainViewport()).estimatedColor==white,"torso white ignores menu HDR tone mapping");
        Check(RenderInspectorLiveOverlay(live,display,settings,*ImGui::GetMainViewport())==Admission::Ready,"live HDR labels and boxes emit");
        const int last=list.VtxBuffer.Size;
        Check(last-first>100,"high DPI label text contributes glyph geometry");
        list.AddRectFilled({20,0},{30,10},IM_COL32(80,90,100,190));
        std::vector<ImDrawVert> before(list.VtxBuffer.begin(),list.VtxBuffer.end());
        ImGui::Render();auto* data=ImGui::GetDrawData();
        for(auto* draw:data->CmdLists)for(int i=0;i<draw->VtxBuffer.Size;++i)
            if(!InspectorOwnsColorVertex(draw,i))draw->VtxBuffer[i].col=Neurotic::UiBrightness::Apply(draw->VtxBuffer[i].col,3.f);
        FinalizeInspectorColors(data);
        bool background=false,textOrBox=false,torso=false;
        for(int i=0;i<list.VtxBuffer.Size;++i){
            const bool owned=i>=first&&i<last;
            Check(InspectorOwnsColorVertex(&list,i)==owned,"only the emitted Inspector range is exempt");
            const auto expected=owned?InspectorOutputColor(before[i].col,policy):Neurotic::UiBrightness::Apply(before[i].col,3.f);
            Check(list.VtxBuffer[i].col==expected,"Inspector output transfer and neighboring UI gain are independent");
            Check(list.VtxBuffer[i].pos.x==before[i].pos.x&&list.VtxBuffer[i].pos.y==before[i].pos.y,"color pass preserves positions");
            if(owned){
                background=background||before[i].col==IM_COL32(8,12,22,220);
                textOrBox=textOrBox||before[i].col==IM_COL32(58,220,255,255);
                torso=torso||before[i].col==white;
            }
        }
        Check(background&&textOrBox&&torso,"background, glyph/box palette and torso all covered");
        std::vector<ImU32> converted;for(const auto& vertex:list.VtxBuffer)converted.push_back(vertex.col);
        FinalizeInspectorColors(data);
        for(int i=0;i<list.VtxBuffer.Size;++i)Check(list.VtxBuffer[i].col==converted[i],"conversion cannot apply twice");
    }
    ImGui::NewFrame();BeginInspectorColors();auto& next=*ImGui::GetBackgroundDrawList();
    next.AddRectFilled({0,0},{10,10},white);
    Check(!InspectorOwnsColorVertex(&next,0),"new frame has no stale ownership ranges");
    ImGui::Render();FinalizeInspectorColors(ImGui::GetDrawData());
    Check(next.VtxBuffer[0].col==white,"empty Inspector frame preserves ordinary geometry");
    ImGui::DestroyContext();
    std::cout<<"PASS "<<checks<<" Inspector color checks: SDR/PQ80/scRGB80/sRGB attachment, alpha, gamut, live high-DPI label/body/torso ranges, 3x menu brightness isolation, idempotence, frame reset\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
