#include "../OptiScaler/dlssnr/DlssNr_PresentResolution.h"
#include "../OptiScaler/dlssnr/DlssNr_PresentHistory.h"
#include "../OptiScaler/dlssnr/DlssNr_MenuStatus.h"
#include "../OptiScaler/shaders/dlssnr/DlssNr_Common.h"
#include "../OptiScaler/NrConfigState.h"
#include "../OptiScaler/CustomOptional.h"
#include <SimpleIni.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

namespace R = DlssNr::PresentResolution;
struct Config
{
    NrOptional<uint32_t> DlssNrRoute {2};
    NrOptional<uint32_t> DlssNrPresentResolution {1}, DlssNrPresentCustomScale {0};
    NrOptional<uint32_t> DlssNrEnhancedResolution {0}, DlssNrEnhancedCustomScale {0};
};
void Load(CSimpleIniA& ini, Config& cfg)
{
    R::LoadConfig(cfg, [&](const char* key) -> std::optional<uint32_t> {
        const auto value = ini.GetValue("DlssNr", key);
        if (!value || std::string(value) == "auto") return {};
        return static_cast<uint32_t>(ini.GetLongValue("DlssNr", key));
    });
}
int main()
{
    for (const char* path : {"OptiScaler.ini", "integration/OptiScaler.ini"})
    {
        CSimpleIniA ini;
        assert(ini.LoadFile(path) >= 0);
        assert(std::string(ini.GetValue("Log", "LogToFile")) == "false");
        CustomOptional<bool> logging {false};
        logging.set_from_config(ini.GetBoolValue("Log", "LogToFile", false));
        assert(!logging.value_or_default()); // same effective value read by the checkbox
        for (bool deliberate : {true, false, true})
        {
            logging = deliberate;
            const auto savedValue = logging.value_for_config();
            ini.SetValue("Log", "LogToFile", savedValue ? (*savedValue ? "true" : "false") : "auto");
            std::string saved; assert(ini.Save(saved) >= 0);
            CSimpleIniA reloaded; assert(reloaded.LoadData(saved) >= 0);
            CustomOptional<bool> restored {false};
            if (std::string(reloaded.GetValue("Log", "LogToFile")) != "auto")
                restored.set_from_config(reloaded.GetBoolValue("Log", "LogToFile"));
            assert(restored.value_or_default() == deliberate);
        }
    }
    for (bool automatic : {false, true})
    {
        CSimpleIniA ini; Config cfg;
        if (automatic) ini.SetValue("DlssNr", "EnhancedResolution", "auto");
        Load(ini, cfg);
        assert(cfg.DlssNrRoute.value_or_default() == 2);
        assert(R::Selected(cfg).mode == R::FollowNative);
        // Every explicit saved method wins over the new default, including full output.
        for (uint32_t mode = 0; mode < 3; ++mode)
        {
            ini.SetLongValue("DlssNr", "EnhancedResolution", mode);
            ini.SetLongValue("DlssNr", "EnhancedCustomScale", 4);
            Config explicitConfig; Load(ini, explicitConfig); R::SaveConfig(ini, explicitConfig);
            std::string saved; assert(ini.Save(saved) >= 0);
            CSimpleIniA reloaded; assert(reloaded.LoadData(saved) >= 0);
            Config copy; Load(reloaded, copy);
            assert(R::Selected(copy).mode == mode && R::Selected(copy).scale == 4);
        }
    }
    DlssNr::MenuStatus::SelectionObservation observation;
    assert(!observation.Fresh(1, 10)); // first observation must wait for a new frame
    assert(!observation.Fresh(1, 10));
    assert(observation.Fresh(1, 11));
    for (uint64_t selection : {2ull, 3ull, 4ull, 1ull})
    {
        assert(!observation.Fresh(selection, 11)); // route, policy, scale or enable changed
        assert(!observation.Fresh(selection, 11)); // diagnostics cannot advance observation
        assert(observation.Fresh(selection, 12));
    }
    for (uint32_t legacy = 0; legacy < 6; ++legacy)
    {
        CSimpleIniA ini; Config cfg;
        ini.SetLongValue("DlssNr", "PresentWorkload", legacy);
        ini.SetValue("DlssNr", "RenderingMode", "1");
        ini.SetValue("DlssNr", "WorkingScale", "0.75");
        Load(ini, cfg);
        assert(cfg.DlssNrPresentResolution.value_or_default() == (legacy ? R::Custom : R::FullOutput));
        assert(cfg.DlssNrPresentCustomScale.value_or_default() == legacy);
        assert(cfg.DlssNrEnhancedResolution.value_or_default() == R::FollowNative);
        cfg.DlssNrEnhancedResolution = R::FollowNative; cfg.DlssNrEnhancedCustomScale = 4u;
        R::SaveConfig(ini, cfg);
        assert(!ini.GetValue("DlssNr", "PresentWorkload"));
        std::string saved; assert(ini.Save(saved) >= 0);
        CSimpleIniA reloaded; assert(reloaded.LoadData(saved) >= 0);
        Config copy; Load(reloaded, copy);
        assert(copy.DlssNrPresentCustomScale.value_or_default() == legacy);
        assert(copy.DlssNrEnhancedResolution.value_or_default() == R::FollowNative);
        assert(copy.DlssNrEnhancedCustomScale.value_or_default() == 4);
        assert(std::string(reloaded.GetValue("DlssNr", "RenderingMode")) == "1");
        assert(std::string(reloaded.GetValue("DlssNr", "WorkingScale")) == "0.75");
        copy.DlssNrRoute = 2u; assert(R::Selected(copy).scale == 4);
        const auto enhancedKey = R::CaptureKey(copy);
        copy.DlssNrRoute = 1u; assert(R::Selected(copy).scale == legacy);
        assert(R::CaptureKey(copy) != enhancedKey);
    }
    auto policy = R::Load(0u, 2u, 4u); assert(policy.mode == R::FollowNative && policy.scale == 2);
    policy = R::Load({}, {}, 99u); assert(policy.mode == R::Custom && policy.scale == 5);
    assert(R::Load({}, {}).mode == R::FullOutput);
    for (auto output : {std::pair{3840u,2160u}, std::pair{2560u,1440u}, std::pair{1919u,1079u}})
    {
        for (uint32_t scale = 0; scale < 6; ++scale)
        {
            const auto size = R::Resolve({R::Custom, scale}, output.first, output.second);
            assert(!size.reason && size.width <= output.first && size.height <= output.second);
            if (scale == 0) assert(size.width == output.first && size.height == output.second);
            else assert(size.width % 8 == 0 && size.height % 8 == 0);
            const auto motion = DlssNrWorkingMotionScale(output.first, output.second, size.width, size.height);
            assert(std::abs(motion.x * output.first - size.width) < 0.001f);
            assert(std::abs(motion.y * output.second - size.height) < 0.001f);
        }
    }
    auto size = R::Resolve({R::Custom, 2}, 3840, 2160); assert(size.width == 2576 && size.height == 1448);
    size = R::Resolve({R::Custom, 4}, 3840, 2160); assert(size.width == 1920 && size.height == 1080);
    size = R::Resolve({R::FollowNative, 4}, 3840, 2160, 1279, 719);
    assert(size.width == 1272 && size.height == 712); // exact game metadata, never a preset name
    assert(R::Resolve({R::FollowNative, 0}, 3840, 2160).reason);
    assert(R::Resolve({R::FollowNative, 0}, 3840, 2160, 4000, 2160).reason);
    assert(R::Resolve({R::FullOutput, 0}, 0, 2160).reason);
    DlssNr::PresentHistory::Continuity history;
    history.CompleteOutputPresent(); assert(!history.ResetForNextEvaluation());
    for (auto reason : {"route", "effective size", "native subrect", "output resize", "fallback"})
    { history.Invalidate(reason); assert(history.ResetForNextEvaluation()); history.CompleteOutputPresent(); }
    std::puts("PASS Present resolution: legacy migration, real INI roundtrip, independent policies, actual sizes and per-axis motion, history interruptions.");
}
