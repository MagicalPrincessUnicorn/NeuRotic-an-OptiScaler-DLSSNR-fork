#pragma once
#include "../ConfigPersistence.h"
#include "Localization.h"
#include "BoundedPopup.h"

namespace Neurotic {
inline void RenderPersistenceFailure(const ConfigPersistence::Snapshot& status)
{
    const char* title=UiLiteral("ingame.persistence_failure.title","Settings could not be saved##PersistenceFailure");
    auto* storage=ImGui::GetStateStorage();
    const auto low=ImGui::GetID("##PersistenceFailureSequenceLow");
    const auto high=ImGui::GetID("##PersistenceFailureSequenceHigh");
    const auto seen=uint64_t(uint32_t(storage->GetInt(low))) | (uint64_t(uint32_t(storage->GetInt(high)))<<32);
    if(status.attempted&&status.sequence!=seen){
        storage->SetInt(low,int(uint32_t(status.sequence)));
        storage->SetInt(high,int(uint32_t(status.sequence>>32)));
        if(!status.succeeded)ImGui::OpenPopup(title);
    }
    SetBoundedPopupSize(36.f,20.f);
    if(ImGui::BeginPopupModal(title,nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings)){
        if(status.succeeded){ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
        ImGui::BeginChild("##PersistenceFailureBody",{0,-ImGui::GetFrameHeightWithSpacing()});
        if(status.errorCode==ERROR_SHARING_VIOLATION){
            bool currentProcess=false;for(const auto& owner:status.fileUsers.owners)currentProcess|=owner.currentProcess;
            if(currentProcess)
                ImGui::TextWrapped(UiLiteral("ingame.persistence_failure.current_process","The game process has this file open. The owning component is unknown."));
            else
                ImGui::TextWrapped(UiLiteral("ingame.persistence_failure.file_in_use","The settings file is in use. Close other apps using it, then save again."));
            ImGui::TextWrapped(UiLiteral("ingame.persistence_failure.session_only","Unsaved changes last only for this session."));
            if(status.fileUsersPending){
                ImGui::TextWrapped(UiLiteral("ingame.persistence_failure.finding_file_users","Checking which apps use this file..."));
            }else if(!status.fileUsers.owners.empty()){
                ImGui::TextWrapped(UiLiteral("ingame.persistence_failure.file_users","Apps using this file (may include the game):"));
                for(const auto& owner:status.fileUsers.owners)
                    ImGui::TextWrapped(UiLiteral("ingame.persistence_failure.file_user_detail","%s (PID %lu)"),owner.name.c_str(),static_cast<unsigned long>(owner.processId));
            }else{
                ImGui::TextWrapped(UiLiteral("ingame.persistence_failure.file_user_unknown","Windows could not identify the app using this file."));
            }
        }else{
            ImGui::TextWrapped(UiLiteral("ingame.persistence_failure.changes_retained","Your changes remain available. Resolve the file error, then save again."));
        }
        ImGui::Separator();
        ImGui::TextWrapped(UiLiteral("ingame.persistence_failure.file","File: %s"),status.path.c_str());
        ImGui::TextWrapped(UiLiteral("ingame.persistence_failure.stage","Stage: %s"),status.stage.c_str());
        ImGui::TextWrapped(UiLiteral("ingame.persistence_failure.windows_error","Windows error %lu: %s"),static_cast<unsigned long>(status.errorCode),status.message.c_str());
        ImGui::EndChild();
        if(ImGui::Button(UiLiteral("desktop.hubshell.close_4bae39a9","Close"))||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
}
