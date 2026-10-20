// Owner-05 native execution only. Tests the real production request builder;
// no HubModel, renderer, process, provider, game, or test-only implementation.
#include "ui/HubViewModel.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main(){
 unsigned failures=0,checks=0;
 auto check=[&](bool pass,const char* label){++checks;failures+=!pass;std::cout<<(pass?"PASS ":"FAIL ")<<label<<'\n';};
 nh::SettingsField field;field.section="DlssNr";field.key="NativeResolutionChoice";field.available=true;
 field.original="full";field.nativeManualScale=field.originalNativeManualScale="0.25";
 // The real parser projects 2.5/99, unknown strings and missing UiManualScale as
 // 0.25. The companion PowerShell test checks all four real read/write cases.
 for(const char* stage:{"pre","post"}){
  strcpy_s(field.draft.data(),field.draft.size(),stage);
  auto patches=nh::BuildSavedSettingsPatches({field});
  check(patches.size()==1&&patches[0].at("value")==stage&&!patches[0].contains("nativeManualScale"),"automatic stage-only edit omits unchanged projected Manual memory");
 }
 field.nativeManualScale="0.5";
 auto edited=nh::BuildSavedSettingsPatches({field});
 check(edited.size()==1&&edited[0].at("nativeManualScale")=="0.5","actual Manual memory edit travels with automatic resolution action");
 field.original="post";
 check(nh::SavedSettingsFieldChanged(field),"return to original Post remains dirty after actual Manual edit");
 auto returned=nh::BuildSavedSettingsPatches({field});
 check(returned.size()==1&&returned[0].at("value")=="post"&&returned[0].at("nativeManualScale")=="0.5","original automatic selection can save a memory-only change");
 field.nativeManualScale=field.originalNativeManualScale;
 check(!nh::SavedSettingsFieldChanged(field)&&nh::BuildSavedSettingsPatches({field}).empty(),"Undo-restored original memory clears dirty state and yields no action");
 field.original="manual";field.nativeManualScale=field.originalNativeManualScale="0.67";
 auto fromManual=nh::BuildSavedSettingsPatches({field});
 check(fromManual.size()==1&&!fromManual[0].contains("nativeManualScale"),"unchanged saved Manual scale uses writer's existing saved-Manual transition");
 strcpy_s(field.draft.data(),field.draft.size(),"manual");field.original="full";
 auto manual=nh::BuildSavedSettingsPatches({field});
 check(manual.size()==1&&manual[0].at("value")=="manual"&&!manual[0].contains("nativeManualScale"),"explicit Manual action retains existing writer semantics");
 std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
