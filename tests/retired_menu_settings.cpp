#include "../external/simpleini/SimpleIni.h"
#include "menu/LegacyMenuSettings.h"
#include <cstdio>
#include <string>
#include <stdexcept>
int main() {
 try {
  for(const char* value : {"true","false","on","off","1","0","invalid",""}) {
   CSimpleIniA ini;ini.SetUnicode();
   const std::string input=std::string("[Menu]\nOptiClip=")+value+"\nScale=1.3\nLanguage=de\n[DlssNr]\nEnabled=true\n[Private]\nOptiClip=keep\nCustom=untouched\n";
   if(ini.LoadData(input.c_str(),input.size())<0) throw std::runtime_error("legacy INI parse failed");
   Neurotic::MenuConfig::DiscardRetiredKeys(ini);
   if(ini.GetValue("Menu","OptiClip")!=nullptr) throw std::runtime_error("retired OptiClip key remains live in loaded settings");
   if(std::string(ini.GetValue("Menu","Scale",""))!="1.3" || std::string(ini.GetValue("Menu","Language",""))!="de" || !ini.GetBoolValue("DlssNr","Enabled") || std::string(ini.GetValue("Private","OptiClip",""))!="keep") throw std::runtime_error("unrelated configuration changed");
   Neurotic::MenuConfig::DiscardRetiredKeys(ini);
   std::string saved; if(ini.Save(saved)<0) throw std::runtime_error("INI save failed");
   CSimpleIniA reload;if(reload.LoadData(saved.data(),saved.size())<0 || reload.GetValue("Menu","OptiClip")!=nullptr || std::string(reload.GetValue("Private","Custom",""))!="untouched") throw std::runtime_error("legacy retirement did not round-trip safely");
  }
  CSimpleIniA absent;Neurotic::MenuConfig::DiscardRetiredKeys(absent);
  if(absent.GetSectionSize("Menu")!=-1) throw std::runtime_error("absent menu was created");
  std::puts("PASS: retired OptiClip true/false/on/off/numeric/malformed/empty/absent INIs load and round-trip without changing unrelated settings");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
