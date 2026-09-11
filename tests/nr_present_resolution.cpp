#include "../OptiScaler/dlssnr/DlssNr_PresentResolution.h"
#include "../OptiScaler/dlssnr/DlssNr_PresentHistory.h"
#include "../OptiScaler/shaders/dlssnr/DlssNr_Common.h"
#include "../OptiScaler/NrConfigState.h"
#include <SimpleIni.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

namespace R = DlssNr::PresentResolution;
struct Config
{
    NrOptional<uint32_t> DlssNrRoute {0};
    NrOptional<uint32_t> DlssNrPresentResolution {1}, DlssNrPresentCustomScale {0};
    NrOptional<uint32_t> DlssNrEnhancedResolution {1}, DlssNrEnhancedCustomScale {0};
};
void Load(CSimpleIniA& ini, Config& cfg)
{
    R::LoadConfig(cfg, [&](const char* key) -> std::optional<uint32_t> {
        if (!ini.GetValue("DlssNr", key)) return {};
        return static_cast<uint32_t>(ini.GetLongValue("DlssNr", key));
    });
}
int main()
{
    for (uint32_t legacy = 0; legacy < 6; ++legacy)
    {
        CSimpleIniA ini; Config cfg;
        ini.SetLongValue("DlssNr", "PresentWorkload", legacy);
        ini.SetValue("DlssNr", "RenderingMode", "1");
        ini.SetValue("DlssNr", "WorkingScale", "0.75");
        Load(ini, cfg);
        assert(cfg.DlssNrPresentResolution.value_or_default() == (legacy ? R::Custom : R::FullOutput));
        assert(cfg.DlssNrPresentCustomScale.value_or_default() == legacy);
        assert(cfg.DlssNrEnhancedResolution.value_or_default() == R::FullOutput);
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
