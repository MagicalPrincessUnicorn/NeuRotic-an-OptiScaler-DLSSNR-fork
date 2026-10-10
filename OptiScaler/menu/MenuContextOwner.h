#pragma once
#include <imgui/imgui.h>
#include <cstdint>
namespace Neurotic::MenuLayout {
// Backend calls and the common frame share this owner. A foreign context is
// restored on exit; a destroyed previous owner is never restored.
class ContextOwner {
    ImGuiContext* owned=nullptr;
    uint64_t generation=0;
public:
    ImGuiContext* Get() const {return owned;}
    void Create() {
        if(!owned) {owned=ImGui::CreateContext();++generation;}
        ImGui::SetCurrentContext(owned);
    }
    void Destroy() {
        if(!owned)return;
        ImGui::DestroyContext(owned);owned=nullptr;++generation;
    }
    class Scope {
        ContextOwner& owner;
        ImGuiContext* previous;
        ImGuiContext* initialOwned;
        uint64_t initialGeneration;
    public:
        explicit Scope(ContextOwner& value):owner(value),previous(ImGui::GetCurrentContext()),
            initialOwned(value.owned),initialGeneration(value.generation) {Refresh();}
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
        void Refresh() {ImGui::SetCurrentContext(owner.owned);}
        ~Scope() {
            ImGui::SetCurrentContext(previous==initialOwned && initialGeneration!=owner.generation?nullptr:previous);
        }
    };
};
}
