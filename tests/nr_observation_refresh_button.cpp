#include "dlssnr/ObservationRefreshButton.h"
#include "menu/SleekShell.h"
#include <cassert>
#include <iostream>
int main() {
    ImGui::CreateContext();auto& io=ImGui::GetIO();
    io.IniFilename=io.LogFilename=nullptr;io.DisplaySize=ImVec2(800,600);io.DeltaTime=1.0f/60.0f;
    io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();
    for(float scale:{1.0f,1.5f,2.0f}) {
        io.FontGlobalScale=scale;
        for(float width:{210.0f,450.0f}) {
            ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(width,200));ImGui::Begin("Refresh layout");
            const float buttonWidth=DlssNr::ObservationRefreshButtonWidth("Refresh observations");
            ImGui::PushID("idle");
            const auto first=ImGui::GetWindowDrawList()->VtxBuffer.Size;
            assert(!DlssNr::ObservationRefreshButton("Refresh observations",false,buttonWidth));
            const auto idle=ImGui::GetItemRectSize();
            const auto origin=ImGui::GetItemRectMin();
            const float iconRight=origin.x+ImGui::GetStyle().FramePadding.x+ImGui::GetFontSize();
            const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
            const auto white=ImGui::GetFontTexUvWhitePixel();
            bool leadingIcon=false;
            for(int vertex=first;vertex<ImGui::GetWindowDrawList()->VtxBuffer.Size;++vertex) {
                const auto& v=ImGui::GetWindowDrawList()->VtxBuffer[vertex];
                if(v.col==ink && v.uv.x==white.x && v.uv.y==white.y && v.pos.x<iconRight)
                    leadingIcon=true;
            }
            assert(leadingIcon);
            ImGui::PopID();
            ImGui::NewLine();const auto before=ImGui::GetWindowDrawList()->VtxBuffer.Size;
            ImGui::PushID("busy");
            assert(!DlssNr::ObservationRefreshButton("Refresh observations",true,buttonWidth));
            const auto busy=ImGui::GetItemRectSize();
            assert(idle.x==busy.x && idle.y==busy.y && busy.x==buttonWidth);
            assert(ImGui::GetWindowDrawList()->VtxBuffer.Size>before);
            ImGui::PopID();
            ImGui::SameLine();assert(!ImGui::Button("Copy observation report"));
            ImGui::End();ImGui::Render();
        }
    }
    io.FontGlobalScale=1.0f;
    for(float width:{420.f,900.f}) {
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,400});
        ImGui::Begin("Actions below chart");
        const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
        ImGui::Dummy({80,70});const float chartBottom=ImGui::GetItemRectMax().y;
        Neurotic::Sleek::ObservationActions();
        assert(ImGui::GetItemRectMin().y>chartBottom);
        assert(std::abs(ImGui::GetItemRectMax().x-right)<1.f);
        ImGui::End();ImGui::Render();
    }
    ImGui::DestroyContext();std::cout<<"leading icon, spinner geometry, idle/busy sizing, DPI and below-chart right alignment passed\n";
}
