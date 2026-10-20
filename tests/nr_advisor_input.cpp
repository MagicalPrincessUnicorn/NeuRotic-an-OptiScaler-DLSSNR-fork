#include "../OptiScaler/dlssnr/NrAdvisorSampling.h"
#include "../OptiScaler/dlssnr/NrAdvisorPolicy.h"
#include "../OptiScaler/menu/input/MenuInputPolicy.h"
#include <cassert>
#include <limits>
#include <iostream>
#include <string_view>
struct AdvisorConfig
{
    NrOptional<uint32_t> DlssNrRoute {2};
    NrOptional<int32_t> DlssNrRenderingMode {1};
    NrOptional<bool> DlssNrRunBeforeSr {true};
    NrOptional<float> DlssNrWorkingScale {1.0f};
    NrOptional<bool> DlssNrUiManualResolution {false};
};
struct AdvisorSnapshot
{
    CustomOptional<uint32_t> DlssNrRoute {2};
    CustomOptional<int32_t> DlssNrRenderingMode {1};
    CustomOptional<bool> DlssNrRunBeforeSr {true};
    CustomOptional<float> DlssNrWorkingScale {1.0f};
    CustomOptional<bool> DlssNrUiManualResolution {false};
};
int main()
{
    using namespace DlssNr::AdvisorSampling;
    for (bool temporary : {false, true})
        for (bool enabled : {false, true})
            for (unsigned int route = 0; route < 3; ++route)
                for (uint32_t chains = 0; chains < 3; ++chains)
                {
                    TemporarySettings.store(temporary);
                    assert(ObserveNativeCadence(enabled, route, chains) ==
                           (temporary && enabled && route == 0 && chains == 1));
                }
    TemporarySettings.store(false);
    for (unsigned mask = 0; mask < 8; ++mask)
    {
        OptiInput::MenuInputPolicy p {bool(mask & 1), bool(mask & 2), bool(mask & 4)};
        assert(p.Mouse(true) == !(mask & 1));
        assert(p.Keyboard(true) == !(mask & 2));
        assert(p.Controller(true) == !(mask & 4));
        assert(!p.Mouse(false) && !p.Keyboard(false) && !p.Controller(false));
        p.captureKey = true;
        assert(p.Mouse(true) && p.Keyboard(true) && p.Controller(true));
        p.captureKey = false;
        assert(p.Controller(true) == !(mask & 4));
    }
    OptiInput::MenuInputPolicy defaults;
    assert(defaults.Mouse(true) && !defaults.Keyboard(true) && !defaults.Controller(true));
    OptiInput::EscapeCloseGesture escape;
    assert(!escape.Update(true, true, false)); // Press arms a close; releasing performs it.
    assert(escape.Update(true, false, true));
    assert(!escape.Update(true, false, true)); // One press cannot close twice.
    assert(!escape.Update(false, true, false)); // A popup/key capture owns this press.
    assert(!escape.Update(true, false, true)); // Dismissing it must not also close the menu.
    assert(!escape.Update(true, true, false));
    assert(!escape.Update(false, false, true)); // Turning the option off or hiding cancels it.
    assert(!escape.Update(true, false, true));
    Window w;
    Cadence c {1, 9, 5, 2, 16.0, false};
    assert(!w.Consume(c)); // Unknown FG cadence cannot qualify, even at a fast interval.
    c.native = true;
    assert(!w.Consume(c)); // Re-reading the same observation never creates a sample.
    c.sequence = 2;
    assert(w.Consume(c));
    assert(!w.Consume(c));
    c.sequence = 3; c.intervalMs = std::numeric_limits<double>::quiet_NaN();
    assert(!w.Consume(c));
    w.baselineMedianMs = 80;
    assert(!w.Stall(640) && w.Stall(641));
    w.baselineMedianMs = 16;
    assert(!w.Stall(500) && w.Stall(501));
    w.samples = 119; w.totalMs = 119 * 16.0;
    assert(!w.Complete(3.1));
    ++w.samples; w.totalMs += 16;
    assert(!w.Complete(2.99) && w.Complete(3));
    assert(w.Fps() == 62.5); // Already-native interval is never divided by an FG ratio.
    using namespace DlssNr::AdvisorPolicy;
    assert(RouteCount(Before) == 1 && RouteCount(After) == 3);
    AdvisorConfig cfg;
    for (int originalStage : {Before, After})
        for (int stage : {Before, After})
            for (int route = 0; route < 3; ++route)
                for (int resolution = 0; resolution < 3; ++resolution)
                {
                    SelectPlacement(cfg, originalStage, 0);
                    const auto oldRoute = cfg.DlssNrRoute.snapshot();
                    const auto oldMode = cfg.DlssNrRenderingMode.snapshot();
                    const auto oldBefore = cfg.DlssNrRunBeforeSr.snapshot();
                    const bool unsupported = !Contains(stage, route) ||
                        (route == 0 && ((stage == Before && resolution == 0) || (stage == After && resolution == 1)));
                    assert(bool(Refusal(cfg, stage, route, resolution, false, false)) == unsupported);
                    assert(cfg.DlssNrRoute.snapshot() == oldRoute && cfg.DlssNrRenderingMode.snapshot() == oldMode &&
                           cfg.DlssNrRunBeforeSr.snapshot() == oldBefore); // preflight never changes live setup
                    if (!unsupported)
                    {
                        SelectPlacement(cfg, stage, route);
                        assert(DlssNr::StageUi::Stage(cfg) == stage);
                        assert(cfg.DlssNrRoute.value_or_default() == unsigned(route));
                        assert(cfg.DlssNrRunBeforeSr.value_or_default() == (stage == Before));
                    }
                }
    // Automatic resolution can follow forced-After reconstruction; manual Before
    // requests must be refused without silently changing the requested placement.
    assert(!Refusal(cfg, Before, 0, 1, false, true));
    assert(!Refusal(cfg, Before, 0, 1, true, false));
    cfg.DlssNrUiManualResolution = true;
    assert(Refusal(cfg, Before, 0, 1, false, true));
    assert(Refusal(cfg, Before, 0, 1, true, false));
    cfg.DlssNrUiManualResolution = false;
    assert(Refusal(cfg, After, 1, 0, true, false)); // no Vulkan Present adapter
    assert(!Refusal(cfg, After, 0, 0, false, true));
    AdvisorSnapshot snapshot;
    for (int stage : {Before, After})
        for (int route = 0; route < 3; ++route)
            for (int resolution = 0; resolution < 3; ++resolution)
            {
                const auto before = snapshot;
                assert(bool(Refusal(snapshot, stage, route, resolution, false, false)) ==
                       bool(Refusal(cfg, stage, route, resolution, false, false)));
                assert(std::optional<uint32_t>(snapshot.DlssNrRoute) == std::optional<uint32_t>(before.DlssNrRoute) &&
                       std::optional<int32_t>(snapshot.DlssNrRenderingMode) == std::optional<int32_t>(before.DlssNrRenderingMode) &&
                       std::optional<bool>(snapshot.DlssNrRunBeforeSr) == std::optional<bool>(before.DlssNrRunBeforeSr));
                if (Contains(stage, route))
                {
                    SelectPlacement(snapshot, stage, route);
                    assert(DlssNr::StageUi::Stage(snapshot) == stage);
                }
            }
    Window startup;
    assert(!startup.RejectStall(false, 1200, true, 1200)); // model startup is not a scored frame
    assert(!startup.WarmupFrame(true, true, 1200, 1200) && startup.warmFrames == 0);
    assert(!startup.StartupExpired(4.999) && startup.StartupExpired(5) && startup.StartupExpired(15));
    for (int i=0; i<29; ++i) assert(!startup.WarmupFrame(true, true, 16, 16));
    assert(!startup.WarmupFrame(false, true, 16, 16) && startup.warmFrames == 0);
    for (int i=0; i<29; ++i) assert(!startup.WarmupFrame(true, true, 16, 16));
    assert(startup.WarmupFrame(true, true, 16, 16));
    assert(startup.RejectStall(true, 1200, true, 16)); // actual measurement stalls still fail
    assert(startup.RejectStall(true, 16, true, 1200));
    assert(!startup.RejectStall(true, 16, false, 1200)); // stale sample not counted
    for (unsigned mask = 0; mask < 8; ++mask)
    {
        const DlssNr::ExperimentalPolicy::Snapshot policy {bool(mask & 1), false,
            bool(mask & 2), bool(mask & 4), 7};
        assert(policy.Allows(DlssNr::ExperimentalPolicy::Guardrail::Hdr));
        assert(policy.Allows(DlssNr::ExperimentalPolicy::Guardrail::FrameGeneration));
    }
    Cadence failure {20, 4, 2, 2, 16.0, false, 10};
    assert(CurrentFailure(2, 10, 30, failure, 31)); // failed/unknown cadence can carry a real policy refusal
    assert(!CurrentFailure(2, 10, 30, failure, 30)); // old attempt
    assert(!CurrentFailure(2, 11, 30, failure, 31)); // old trial
    assert(!CurrentFailure(1, 10, 30, failure, 31)); // previous card
    assert(!CurrentFailure(0, 10, 30, failure, 31)); // Native must not inherit Present reasons
    failure.sequence = 0;
    assert(!CurrentFailure(2, 10, 30, failure, 31));
    assert(std::string_view(MissingFeedback(false)).find("No model feedback") != std::string_view::npos);
    assert(std::string_view(MissingFeedback(true)).find("frame timing") != std::string_view::npos);
    assert(std::string_view(MissingFeedback(false)).find("Experimental") == std::string_view::npos);
    assert(std::string_view(MissingFeedback(true)).find("Experimental") == std::string_view::npos);
    std::cout << "PASS five-second feedback boundary, normal FG/HDR policy, current-trial failure identity and model-vs-timing diagnosis\n";
    std::cout << "PASS Advisor stage/route/resolution matrix, pure preflight, explicit Native placement, forced-path limits, bounded startup, stable warmup and strict measurement stalls\n";
    std::cout << "PASS eight input policies, capture ownership, defaults, native cadence freshness, unknown/invalid refusal, sample bounds and stall thresholds\n";
}
