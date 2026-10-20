// Actual option/snapshot contract. A source without the option reaches a behavioral failure.
#include "NrConfigFixture.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

template<class C> static void CheckConfig()
{
    if constexpr (requires(C& c) { c.DlssNrVulkanPresentSameFrameInit; })
    {
        C cfg;
        Check(!cfg.DlssNrVulkanPresentSameFrameInit.value_or_default(), "opt-in defaults off");
        const auto before = cfg.GetDlssNrConfigSnapshot();
        cfg.DlssNrVulkanPresentSameFrameInit.set_from_config(true);
        const auto enabled = cfg.GetDlssNrConfigSnapshot();
        Check(before.ObservationRevision() != enabled.ObservationRevision(), "option mutation invalidates captured preference revision");
        Check(enabled.DlssNrVulkanPresentSameFrameInit.value_or_default(), "explicit true reaches snapshot");
        Check(!before.DlssNrVulkanPresentSameFrameInit.value_or_default(), "old snapshot remains immutable");
        Check(cfg.DlssNrVulkanPresentSameFrameInit.value_for_config().value_or(false), "explicit opt-in retained for normal save");
        C restartedOff;
        restartedOff.DlssNrVulkanPresentSameFrameInit.set_from_config(false);
        Check(!restartedOff.GetDlssNrConfigSnapshot().DlssNrVulkanPresentSameFrameInit.value_or_default(), "restart with explicit false disables");
        cfg.DlssNrVulkanPresentSameFrameInit = false;
        Check(!cfg.GetDlssNrConfigSnapshot().DlssNrVulkanPresentSameFrameInit.value_or_default(), "explicit false disables");
        Check(enabled.DlssNrVulkanPresentSameFrameInit.value_or_default(), "enabled snapshot is not mutated by disable");
        C restartedAbsent;
        restartedAbsent.DlssNrVulkanPresentSameFrameInit.set_from_config(std::nullopt);
        Check(!restartedAbsent.GetDlssNrConfigSnapshot().DlssNrVulkanPresentSameFrameInit.value_or_default(), "restart with absent key returns to false");
        std::puts("PASS config option and actual snapshot; INI parser/roundtrip covered separately");
    }
    else Check(false, "Vulkan Present same-frame initialization opt-in is missing");
}

int main() { CheckConfig<TestConfig>(); }
