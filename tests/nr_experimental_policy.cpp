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
    BoolSetting DlssNrPreparedDepth;
};

int main()
{
    using namespace DlssNr::ExperimentalPolicy;
    FakeConfig config;
    SessionReady.store(false);
    ResetDraft(config);
    Draft.multipass = true;
    assert(!AnySelected(Draft)); // Retired multipass opt-in cannot activate an experiment.
    assert(!Capture(config).active); // An unsaved checkbox cannot unlock the live policy.
    DiscardDraft();
    assert(Capture(config).Allows(Guardrail::Hdr));
    assert(Capture(config).Allows(Guardrail::FrameGeneration));
    assert(Capture(config).Allows(Guardrail::Multipass));

    config.DlssNrExperimentalMode = true;
    config.DlssNrOverrideMultipassGuardrails = true;
    assert(!Requested(config)); // retired override alone does not start an experimental session
    assert(Capture(config).Allows(Guardrail::Multipass));
    SessionReady.store(true);
    assert(Capture(config).Allows(Guardrail::Multipass));

    config.DlssNrExperimentalMode = false;
    config.DlssNrOverrideHdrGuardrails = true;
    config.DlssNrOverrideFgGuardrails = true;
    assert(!Requested(config));
    ResetDraft(config);
    assert(!AnySelected(Draft));
    assert(!NewChoicesRequested(config, Draft));
    assert(Capture(config).Allows(Guardrail::Hdr));
    assert(Capture(config).Allows(Guardrail::FrameGeneration));

    Draft.multipass = true;
    assert(!NewChoicesRequested(config, Draft));
    ApplyDraft(config);
    assert(!config.DlssNrExperimentalMode.value);
    assert(!config.DlssNrOverrideMultipassGuardrails.value);
    assert(!config.DlssNrOverrideHdrGuardrails.value);
    assert(!config.DlssNrOverrideFgGuardrails.value);
    assert(!NewChoicesRequested(config, Draft));

    Draft.multipass = false;
    assert(!Capture(config).active); // Ordinary multipass never activated an experiment.
    Draft.preSrSoftReset = true;
    assert(NewChoicesRequested(config, Draft));
    ApplyDraft(config);
    assert(Requested(config));
    assert(Capture(config).Allows(Guardrail::Multipass));
    Draft.preSrSoftReset = false;
    ApplyDraft(config);
    assert(!Requested(config));
    DiscardDraft();
    assert(!Draft.initialized && !Draft.dirty);

    // A saved depth-only selection must participate in the existing consent/session policy.
    config.DlssNrExperimentalMode = true;
    config.DlssNrPreparedDepth = true;
    ResetDraft(config);
    assert(AnySelected(Draft));
    assert(Requested(config));
    assert(Capture(config).active);
    assert(Capture(config).preparedDepth);
    assert(Capture(config).Allows(Guardrail::Multipass));
    SessionReady.store(false);
    assert(!Capture(config).active);
    assert(!Capture(config).preparedDepth);
    SessionReady.store(true);
    config.DlssNrExperimentalMode = false;
    ResetDraft(config);
    assert(!AnySelected(Draft));
    assert(!Requested(config));
    assert(!Capture(config).preparedDepth);
    Draft.preparedDepth = true;
    assert(NewChoicesRequested(config, Draft));
    assert(!Capture(config).preparedDepth); // Unsaved depth opt-in is inert.
    ApplyDraft(config);
    assert(Capture(config).preparedDepth);
    assert(!NewChoicesRequested(config, Draft));
    Draft.preparedDepth = false;
    assert(Capture(config).preparedDepth); // Unsaved removal does not affect rendering.
    ApplyDraft(config);
    assert(!Requested(config) && !Capture(config).preparedDepth);
    Draft.preparedDepth = true;
    Draft.dirty = true;
    assert(!SaveDraft(config, [&](const UiDraft&) {
        assert(!Capture(config).preparedDepth); // Neither failed nor in-flight saves may activate it.
        return false;
    }));
    assert(!Capture(config).preparedDepth && Draft.dirty);
    assert(SaveDraft(config, [&](const UiDraft&) {
        assert(!Capture(config).preparedDepth);
        return true;
    }));
    assert(!Capture(config).preparedDepth); // Marker readiness is published separately by Applied.
    SessionReady.store(true);
    assert(Capture(config).preparedDepth);
    Draft.preparedDepth = false;
    assert(!SaveDraft(config, [&](const UiDraft&) {
        assert(Capture(config).preparedDepth); // Failed removal also preserves the applied choice.
        return false;
    }));
    assert(Capture(config).preparedDepth);
    assert(SaveDraft(config, [](const UiDraft&) { return true; }));
    assert(!Capture(config).preparedDepth);
    std::cout << "PASS: experimental consent, saved depth compatibility, session readiness and unsaved drafts\n";
    std::cout << "PASS: Multipass/HDR/FG ordinary paths, inert legacy overrides and camera-cut Experimental guard\n";
}
