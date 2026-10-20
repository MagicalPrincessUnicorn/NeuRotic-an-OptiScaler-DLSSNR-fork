#include "../contracts/Test002Support.h"
#include <CustomOptional.h>
#if __has_include(<nr/protocol/LegacySettingsProjection.h>)
#include <nr/protocol/LegacySettingsProjection.h>
#define NR_PROTOCOL_SETTINGS 1
#endif
using namespace NrTest002;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("protocol-settings",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_PROTOCOL_SETTINGS
    namespace P=Neurotic::Protocol;
    struct Snapshot
    {
        CustomOptional<std::uint32_t> DlssNrPreset{0},DlssNrStyle{0};CustomOptional<bool> DlssNrAutoMask{true};
        CustomOptional<float> DlssNrIntensity{1},DlssNrLocalStructure{1},DlssNrLocalTone{1},DlssNrSkinStructure{-1};
    } original;
    const C::OwnerValueReference source{C::OwnerDomain::Configuration,Sym("NR.RenderSettings"),{1,0},Key("settings"),1};
    const auto captured=P::ProjectLegacySettings(original,source,Ev());
    r.Check("T25.config-projects-seven",captured&&captured->controls.Size()==7,true,"ProjectLegacySettings");
    if(captured)
    {
        r.Check("T25.config-default-raw-unknown",!captured->controls.Get(0)->raw.IsKnown(),true,"ProjectLegacySettings");
        original.DlssNrIntensity=0.25f;
        r.Check("T25.config-held-snapshot",std::get<double>(captured->controls.Get(3)->effective.KnownPart()->value)==1,true,"ProjectLegacySettings");
        const auto updated=P::ProjectLegacySettings(original,source,Ev());
        r.Check("T25.config-explicit-provenance",updated&&updated->controls.Get(3)->raw.IsKnown()&&std::get<double>(updated->controls.Get(3)->effective.KnownPart()->value)==0.25,true,"ProjectLegacySettings");
    }
    original.DlssNrIntensity=std::numeric_limits<float>::quiet_NaN();
    r.Check("T25.config-nonfinite-refused",!P::ProjectLegacySettings(original,source,Ev()),true,"ProjectLegacySettings");
#else
    r.Check("T25.settings-projection-production",false,true,"ProjectLegacySettings");
#endif
    return r.Finish();});
}
