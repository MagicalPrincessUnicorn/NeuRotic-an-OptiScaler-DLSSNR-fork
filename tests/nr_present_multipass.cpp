#include "NrConfigFixture.h"
#include "../OptiScaler/dlssnr/DlssNr_Multipass.h"
#include "../OptiScaler/dlssnr/StartupReadiness.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

int main()
{
    namespace MP = DlssNr::Multipass;
    TestConfig config;
    auto settings = config.GetDlssNrConfigSnapshot();
    settings.DlssNrPasses = 10u;
    CHECK(MP::RequestedCount(settings) == 1); // disabled cannot execute saved children
    settings.DlssNrMultipassEnabled = true;
    auto profile = settings.DlssNrBasicMultipass.value_or_default();
    profile.advanced = true;
    settings.DlssNrBasicMultipass = profile;
    CHECK(MP::RequestedCount(settings) == 10);
    settings.DlssNrPasses = 100u;
    CHECK(MP::RequestedCount(settings) == 10);
    settings.DlssNrPasses = 0u;
    CHECK(MP::RequestedCount(settings) == 1);

    settings.DlssNrApplyModel = false;
    settings.DlssNrSecondLayerApplyModel = false;
    for (auto& layer : settings.DlssNrExtraLayers) layer.applyModel = false;
    settings.DlssNrPasses = 10u;
    CHECK(!MP::AppliesModel(settings));
    settings.DlssNrExtraLayers[7].applyModel = true;
    CHECK(MP::AppliesModel(settings)); // Pass 10 may apply while Pass 1 only seeds
    settings.DlssNrPasses = 9u;
    CHECK(!MP::AppliesModel(settings)); // inactive profiles cannot cause copyback
    settings.DlssNrSecondLayerApplyModel = true;
    CHECK(MP::AppliesModel(settings));
    settings.DlssNrMultipassEnabled = false;
    CHECK(!MP::AppliesModel(settings));

    // Saved profiles still obey Native Temporal's global Apply switch even
    // when their independent per-pass switches remain enabled in storage.
    config.DlssNrMultipassEnabled = true;
    config.DlssNrBasicMultipass = profile;
    config.DlssNrPasses = 10u;
    config.DlssNrApplyModel = false;
    const auto effective = TryNrConfigSnapshot(config);
    CHECK(effective && !MP::AppliesModel(*effective));
    CHECK(!effective->DlssNrSecondLayerApplyModel.value_or_default());
    CHECK(!effective->DlssNrExtraLayers[7].applyModel.value_or_default());
    CHECK(config.DlssNrSecondLayerApplyModel.value_or_default()); // saved child untouched

    for (unsigned requested = 1; requested <= 10; ++requested)
    {
        CHECK(!MP::CanDeliver(requested, 0, false)); // creation is never output
        CHECK(!MP::CanDeliver(requested, 0, true));
        for (unsigned completed = 1; completed <= requested; ++completed)
        {
            CHECK(MP::CanDeliver(requested, completed, false)); // Native-style valid prefix
            CHECK(MP::CanDeliver(requested, completed, true) == (completed == requested));
        }
        CHECK(!MP::CanDeliver(requested, requested + 1, true));
    }
    CHECK(!MP::CanDeliver(0, 0, true));
    CHECK(!MP::CanDeliver(11, 11, true));

    profile.advanced = false;
    profile.model = profile.detail = 0.0f;
    settings.DlssNrBasicMultipass = profile;
    settings.DlssNrMultipassEnabled = true;
    CHECK(MP::RequestedCount(settings) == 0);
    CHECK(!MP::AppliesModel(settings));
    profile.model = 2.5f;
    profile.maximum = 4;
    settings.DlssNrBasicMultipass = profile;
    CHECK(MP::RequestedCount(settings) == 3);

    // The real readiness owner revokes a previously submitted certificate when
    // ANY child session is rebuilt or retired; the first model can stay unchanged.
    DlssNr::PreFg::ReadinessIdentity key {};
    key.model = 1;
    key.additionalModels = 9;
    key.additionalRetirements = 0;
    key.requestedPasses = 10;
    auto changed = key;
    ++changed.additionalModels;
    CHECK(changed != key);
    changed = key;
    ++changed.additionalRetirements;
    CHECK(changed != key);
    changed = key;
    changed.requestedPasses = 2;
    CHECK(changed != key);
    std::puts("PASS: bounded Basic/Advanced counts, all-pass application, valid-prefix/full-FG completion and child readiness identities");
}
