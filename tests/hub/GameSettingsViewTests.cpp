#include "ui/GameSettingsView.h"
#include <fstream>
#include <iostream>
int main(int argc,char** argv){
 if(argc!=2)return 2;
 nh::Json schema;std::ifstream input(argv[1]);input>>schema;
 int failures=0;
 const int childCounts[]={7,5,5,2,5,3,4};
 for(int page=0;page<7;++page)if(nh::SavedSettingsChildren(page).size()!=childCounts[page])++failures;
 for(const auto& field:schema){
  auto section=field.at("section").get<std::string>(),key=field.at("key").get<std::string>();
  auto location=nh::SavedSettingsPlacement(section,key);
  if(location.page==-3&&nh::IsInstallationCompatibilitySetting(section,key))continue;
  if(location.page==-2&&section=="Menu"&&(key=="PreflightExpanded"||key=="LightTheme"))continue;
  if(location.page<0||location.page>=7||location.child<0||location.child>=childCounts[location.page]){
   std::cerr<<"FAIL unplaced saved setting "<<section<<'/'<<key<<'\n';++failures;
  }
 }
 if(nh::SavedSettingsPlacement("Menu","PreflightExpanded").page!=-2)++failures;
 if(nh::SavedSettingsPlacement("Plugins","LoadReShade").page!=-3)++failures;
 if(nh::SavedSettingsPlacement("Hotfix","RestoreComputeSignature").page!=-3)++failures;
 if(nh::SavedSettingsPlacement("ObjectRules","ProfileHex").page!=2||nh::SavedSettingsPlacement("ObjectRules","ProfileHex").child!=2)++failures;
 if(nh::SavedSettingsPlacement("Menu","Language").child!=5)++failures;
 if(nh::SavedSettingsPlacement("Menu","AllowGameInput").child!=2)++failures;
 if(nh::SavedSettingsPlacement("CharacterInspector","Enabled").child!=4)++failures;
 if(nh::SavedSettingsPlacement("DlssNr","ExperimentalMode").child!=3)++failures;
 if(nh::SavedSettingsPlacement("DlssNr","DebugView").child!=0)++failures;
 const char* neural[]={"Main","Multi Pass","Object Rules (Experimental)","Overrides","Inspector (Experimental)"};
 for(int i=0;i<5;++i)if(std::string(nh::SavedSettingsChildren(2)[i])!=neural[i])++failures;
 if(nh::SavedSettingsVisualChild(2,0)!=0||nh::SavedSettingsVisualChild(2,1)!=1||nh::SavedSettingsVisualChild(1,0)!=0)++failures;
 if(nh::SavedSettingsPlacement("DlssNr","Route").child!=0||nh::SavedSettingsPlacement("DlssNr","PresentInputPolicy").child!=0)++failures;
 if(nh::SavedSettingsPlacement("Menu","ReduceMotion").child!=6||std::string(nh::SavedSettingsChildren(0).back())!="Animations")++failures;
 if(nh::SavedSettingsPlacement("Menu","LightTheme").page!=-2)++failures;
 if(nh::SavedSettingsPlacement("Menu","FpsScale").page!=6)++failures;
 if(nh::SavedSettingsPlacement("Screenshots","NrOff").page!=5)++failures;
 if(nh::SavedSettingsPlacement("DlssNrLayer10","WorkingScale").section!=std::string("Pass 10"))++failures;
 if(nh::SavedSettingsPlacement("FutureSection","Unknown").page!=-1)++failures;
 std::cout<<(failures?"FAIL ":"PASS ")<<schema.size()<<" saved fields map to saved tabs, separate compatibility controls or the preserved in-game layout preference; future fields remain visible\n";
 return failures?1:0;
}
