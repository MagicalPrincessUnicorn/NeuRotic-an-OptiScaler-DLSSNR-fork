#include "dlssnr/NrExperimentalPolicy.h"

#include <cassert>
#include <iostream>

struct BoolSetting
{
    bool value = false;
    bool value_or_default() const { return value; }
    BoolSetting& operator=(bool next) { value = next; return *this; }
};

struct FakeConfig
{
    BoolSetting DlssNrExperimentalMode;
    BoolSetting DlssNrOverrideMultipassGuardrails;
    BoolSetting DlssNrOverrideHdrGuardrails;
    BoolSetting DlssNrOverrideFgGuardrails;
    BoolSetting DlssNrPreSrSoftReset;
};

int main()
{
    using namespace DlssNr::ExperimentalPolicy;
    FakeConfig config;
    SessionReady.store(false);
    config.DlssNrExperimentalMode = true;
    config.DlssNrOverrideMultipassGuardrails = true;
    assert(!Capture(config).Allows(Guardrail::Multipass));

    SessionReady.store(true);
    assert(Capture(config).Allows(Guardrail::Multipass));
    assert(!Capture(config).Allows(Guardrail::Hdr));
    assert(!Capture(config).Allows(Guardrail::FrameGeneration));

    for (unsigned int mask = 0; mask < 16; ++mask)
    {
        config.DlssNrExperimentalMode = (mask & 1) != 0;
        config.DlssNrOverrideMultipassGuardrails = (mask & 2) != 0;
        config.DlssNrOverrideHdrGuardrails = (mask & 4) != 0;
        config.DlssNrOverrideFgGuardrails = (mask & 8) != 0;
        const auto policy = Capture(config);
        assert(policy.Allows(Guardrail::Multipass) == ((mask & 3) == 3));
        assert(policy.Allows(Guardrail::Hdr) == ((mask & 5) == 5));
        assert(policy.Allows(Guardrail::FrameGeneration) == ((mask & 9) == 9));
    }

    config.DlssNrExperimentalMode = true;
    config.DlssNrOverrideMultipassGuardrails = true;
    config.DlssNrOverrideHdrGuardrails = false;
    config.DlssNrOverrideFgGuardrails = false;
    config.DlssNrPreSrSoftReset = true;
    ResetDraft(config);
    Draft.active = false;
    Draft.multipass = false;
    Draft.hdr = true;
    Draft.dirty = true;
    ApplyDraft(config);
    assert(!config.DlssNrExperimentalMode.value);
    assert(config.DlssNrOverrideHdrGuardrails.value);
    assert(config.DlssNrPreSrSoftReset.value);
    assert(!Capture(config).Allows(Guardrail::Hdr));

    Draft.active = true;
    ApplyDraft(config);
    assert(Capture(config).Allows(Guardrail::Hdr));
    assert(!Capture(config).Allows(Guardrail::Multipass));
    DiscardDraft();
    assert(!Draft.initialized && !Draft.dirty);
    std::cout << "PASS: experimental master, independent guardrails, session gate and draft apply\n";
}
