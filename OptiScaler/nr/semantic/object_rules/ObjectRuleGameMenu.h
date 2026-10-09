#include <menu/Localization.h>
#pragma once
#include "ObjectRuleEditor.h"
#include "ObjectControlRegistry.h"
namespace Neurotic::Semantic::Rules {
template<class ConfigT> void DrawGameEditor(ConfigT& config){
    static Editor editor;
    if(!LoadError().empty()){ImGui::TextWrapped(Neurotic::UiLiteral("ingame.objectrulegamemenu.saved_rules_could_not_be_loaded_s_5a84ca2f", "Saved rules could not be loaded: %s"),Neurotic::Translate(LoadError().c_str()).c_str());return;}
    if(!Selection().pending.empty()){Select(editor,Selection().pending);Selection().pending.clear();}
    ImGui::Checkbox(Neurotic::UiLiteral("ingame.objectrulegamemenu.pick_a_rule_from_a_box_587017e3", "Pick a rule from a box"),&Selection().armed);
    if(Selection().armed)ImGui::TextWrapped(Neurotic::UiLiteral("ingame.objectrulegamemenu.with_this_menu_open_click_a_detected_box_outside_93aa4202", "With this menu open, click a detected box outside the menu to select its rule."));
    auto before=GameStore().Read()->revision;
    Host host;host.globalControls=[&](){
        ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.objectrulegamemenu.current_global_nr_controls_read_only_here_b8590a67", "Current global NR controls (read-only here)"));
        if(ImGui::TreeNode(Neurotic::UiLiteral("ingame.objectrulegamemenu.all_global_nr_controls_864f1ce1", "All global NR controls"))){VisitGlobalControls(config,[](const char* id,const std::string& value){ImGui::TextWrapped(Neurotic::UiLiteral("ingame.objectrulegamemenu.s_s_global_only_a04b3f50", "%s: %s (global only)"),Neurotic::Translate(id).c_str(),Neurotic::Translate(value.c_str()).c_str());});ImGui::TreePop();}
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.objectrulegamemenu.edit_global_settings_in_the_existing_neural_rend_c78f1233", "Edit global settings in the existing Neural Rendering controls. These values are inherited live; object rules never set provider model parameters."));
    };
    DrawEditor(GameStore(),editor,host);Activity().store(editor.mode);
    if(GameStore().Read()->revision!=before)config.ObjectRulesProfileHex=EncodeIni(GameStore().Read()->profile);
}
}
