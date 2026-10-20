#include <cstdint>
#include <iostream>
#include "../../OptiScaler/CustomOptional.h"
#include "RrPresetFields.h"

// Config's serialization contract: value_for_config null -> auto, otherwise decimal.
template<class Setting> Setting RoundTrip(Setting& setting, std::string& serialized)
{
    auto persisted = setting.value_for_config();
    serialized = persisted ? std::to_string(*persisted) : "auto";
    Setting reloaded{0};
    reloaded.set_from_config(serialized == "auto" ? std::optional<uint32_t>{}
                                                : std::optional<uint32_t>{static_cast<uint32_t>(std::stoul(serialized))});
    return reloaded;
}
int main()
{
    int failed=0;
    auto check=[&](bool ok,const char* label) { std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n'; failed+=!ok; };
    auto test=[&](auto& field) {
        std::string text;
        auto inherited=RoundTrip(field,text);
        check(text=="auto" && !inherited.has_value(),"unset survives save/reload as inheritance");
        check(inherited.value_or(4)==4,"unset inherits configured global hint");
        field=uint32_t{0};
        auto explicitDefault=RoundTrip(field,text);
        check(text=="0" && explicitDefault.has_value() && explicitDefault.value()==0,
              "explicit Default survives save/reload as zero");
        check(explicitDefault.value_or(4)==0,"explicit Default overrides configured global hint");
        field.set_volatile_value(6);
        auto original=RoundTrip(field,text);
        check(text=="0" && original.has_value() && original.value()==0,"volatile runtime hint preserves persisted Default");
        field=std::optional<uint32_t>{};
        auto reset=RoundTrip(field,text);
        check(text=="auto" && !reset.has_value(),"Use Global clears explicit and volatile provenance");
    };
    RrPresetFields settings;
    test(settings.DLSSDRenderPresetForAll);
    test(settings.DLSSDRenderPresetDLAA);
    test(settings.DLSSDRenderPresetUltraQuality);
    test(settings.DLSSDRenderPresetQuality);
    test(settings.DLSSDRenderPresetBalanced);
    test(settings.DLSSDRenderPresetPerformance);
    test(settings.DLSSDRenderPresetUltraPerformance);
    std::string text;
    settings.DLSSDRenderPresetForAll=std::optional<uint32_t>{};
    auto globalAuto=RoundTrip(settings.DLSSDRenderPresetForAll,text);
    check(settings.DLSSDRenderPresetQuality.value_or(globalAuto.value_or(6))==6,
          "unset local and global use game hint after reload");
    settings.DLSSDRenderPresetForAll=uint32_t{0};
    auto globalDefault=RoundTrip(settings.DLSSDRenderPresetForAll,text);
    check(settings.DLSSDRenderPresetQuality.value_or(globalDefault.value_or(6))==0,
          "unset local and explicit global Default use zero after reload");
    return failed;
}
