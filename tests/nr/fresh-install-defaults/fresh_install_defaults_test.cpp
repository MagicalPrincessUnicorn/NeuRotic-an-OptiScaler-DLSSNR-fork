#include "dlssnr/FreshInstallDefaults.h"
#include "dlssnr/NativeNrResolution.h"
#include "SynchronizedOptional.h"
#include <SimpleIni.h>
#include <cassert>
#include <cstdio>
#include <string>
#ifdef NDEBUG
#error Fresh-default checks require assertions enabled.
#endif
namespace {
unsigned checks=0;
void Check(bool result){++checks;assert(result);}
struct Configuration {
 NrOptional<uint32_t> DlssNrRoute{DlssNr::SetupRequirements::Route};
 NrOptional<uint32_t> DlssNrNativeResolution{DlssNr::SetupRequirements::NativeResolution};
 NrOptional<int32_t> DlssNrRenderingMode{DlssNr::SetupRequirements::RenderingMode};
 NrOptional<bool> DlssNrEnabled{false};
};
bool Saved(const CSimpleIniA& ini){return DlssNr::FreshInstallDefaults::HasSavedSelection(
 [&](const char* key){return ini.GetValue("DlssNr",key)!=nullptr;});}
}
int main(){
 using namespace DlssNr::FreshInstallDefaults;
 Configuration fresh;
 Check(fresh.DlssNrRoute.value_or_default()==0);
 Check(fresh.DlssNrNativeResolution.value_or_default()==1);
 Check(fresh.DlssNrRenderingMode.value_or_default()==0);
 Check(!fresh.DlssNrEnabled.value_or_default());
 for(const auto* text:{"","[Other]\nRoute=0\n","[DlssNr]\nEnabled=false\nStyle=2\n"}){
  CSimpleIniA ini;Check(ini.LoadData(text)==SI_OK);Check(!Saved(ini));
  const auto d=Missing(Saved(ini));Check(d.route==0&&d.nativeResolution==1&&d.renderingMode==0);
 }
 // Every raw saved selection key, including auto/invalid/unknown values, stays
 // on the historical missing-key contract. No parser success is needed.
 for(const auto* key:DlssNr::SetupRequirements::SavedSelectionKeys){
  for(const auto* value:{"auto","garbage","99","0"}){
   CSimpleIniA ini;Check(ini.SetValue("DlssNr",key,value)>=SI_OK);Check(Saved(ini));
   const auto d=Missing(Saved(ini));Check(d.route==2&&d.nativeResolution==0&&d.renderingMode==1);
  }
 }
 for(uint32_t route=0;route<=3;++route){
  Configuration cfg;cfg.DlssNrRoute=route;
  CSimpleIniA saved;SaveRoute(saved,cfg);
  Check(saved.GetLongValue("DlssNr","Route",-1)==static_cast<long>(route));
  std::string bytes;Check(saved.Save(bytes)==SI_OK);
  CSimpleIniA reloaded;Check(reloaded.LoadData(bytes.c_str())==SI_OK);Check(Saved(reloaded));
  Check(reloaded.GetLongValue("DlssNr","Route",-1)==static_cast<long>(route));
 }
 // An absent route resets a previous in-memory route, as the original loader
 // did; its fallback then depends on this profile's raw saved-key boundary.
 {Configuration cfg;cfg.DlssNrRoute=3u;LoadRoute(cfg,std::nullopt,0u);Check(cfg.DlssNrRoute.value_or_default()==0);}
 {Configuration cfg;cfg.DlssNrRoute=0u;LoadRoute(cfg,std::nullopt,2u);Check(cfg.DlssNrRoute.value_or_default()==2);}
 {Configuration cfg;LoadRoute(cfg,0u,2u);Check(cfg.DlssNrRoute.value_or_default()==0);}
 {Configuration cfg;LoadRoute(cfg,3u,0u);Check(cfg.DlssNrRoute.value_or_default()==3);}
 {Configuration cfg;LoadRoute(cfg,99u,0u);Check(cfg.DlssNrRoute.value_or_default()==2);}
 // A never-configured Native default must also serialize explicitly, rather
 // than auto (which belongs to the older Present-Enhanced saved contract).
 {Configuration cfg;CSimpleIniA ini;SaveRoute(ini,cfg);Check(ini.GetLongValue("DlssNr","Route",-1)==0);}
 // Preserve the optional's original persistent intent during a volatile route.
 {Configuration cfg;cfg.DlssNrRoute.set_from_config(2u);cfg.DlssNrRoute.set_volatile_value(0u);
  CSimpleIniA ini;SaveRoute(ini,cfg);Check(ini.GetLongValue("DlssNr","Route",-1)==2);}
 {Configuration cfg;DlssNr::NativeResolution::LoadSelection(cfg,99u);Check(cfg.DlssNrNativeResolution.value_or_default()==99);}
 {Configuration cfg;DlssNr::NativeResolution::LoadSelection(cfg,0u);Check(cfg.DlssNrNativeResolution.value_or_default()==0);}
 std::printf("fresh_install_defaults_test: %u checks, 0 failures\n",checks);
}
