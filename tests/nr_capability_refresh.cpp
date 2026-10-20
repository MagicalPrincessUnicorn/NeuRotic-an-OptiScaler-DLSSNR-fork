#include "../OptiScaler/nr/diagnostics/capability/CapabilityRefresh.h"
#include <cassert>
#include <iostream>
#include "../OptiScaler/SynchronizedOptional.h"
using namespace DlssNr::Capability;

// Value fixtures for the adapter's telemetry inputs. Counters are deliberately
// present so an accidental dependency on per-frame progress fails the idle test.
struct NativeValues : RefreshState::Native { unsigned long long frames=0, successfulEvaluations=0; };
struct PresentValues : RefreshState::Present {
    unsigned long long presentAttempts=0, modelEvaluations=0, hdrObservationSequence=0, hdrDescriptorGeneration=0;
    // If this is removed from the refresh key, keep it as an input field: the
    // real Present owner bumps it for identical successful metadata events too.
};
struct ProviderValues : RefreshState::Provider {};

int main()
{
    NativeValues native; PresentValues present; ProviderValues provider;
    auto state=MakeRefreshState(false,0,0,native,present,provider);
    RefreshController controller; int calls=0;
    const auto collect=[&] { ++calls; return true; };
    assert(controller.Update(state,0,1,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
    for(unsigned frame=2;frame<1002;++frame) {
        ++native.frames; ++native.successfulEvaluations;
        ++present.presentAttempts; ++present.modelEvaluations;
        ++present.hdrDescriptorGeneration; ++present.hdrObservationSequence;
        state=MakeRefreshState(false,0,0,native,present,provider);
        assert(controller.Update(state,frame/100.0,frame,RefreshRequest::Automatic,collect)==RefreshOutcome::NotDue);
    }
    assert(calls==1); // An idle open panel must not collect just because time/counters advance.

    // NR on -> route switch -> off is one coalesced refresh, including an ABA
    // return to the initial values. A changed state must not be forgotten.
    RefreshController burst; calls=0;
    state=MakeRefreshState(false,0,0,native,present,provider);
    assert(burst.Update(state,0,1,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
    state.requestedEnabled=true;
    assert(burst.Update(state,0.1,2,RefreshRequest::Automatic,collect)==RefreshOutcome::NotDue);
    state.requestedRoute=2;
    assert(burst.Update(state,0.2,3,RefreshRequest::Automatic,collect)==RefreshOutcome::NotDue);
    state.requestedEnabled=false; state.requestedRoute=0;
    assert(burst.Pending());
    assert(burst.Update(state,0.499,4,RefreshRequest::Automatic,collect)==RefreshOutcome::NotDue);
    assert(burst.Update(state,0.5,5,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
    assert(calls==2 && !burst.Pending());

    // Every meaningful domain must trigger capture; failures/resets and HDR
    // metadata changes count, whereas sampling/frame counters do not.
    for(unsigned domain=0;domain<9;++domain) {
        RefreshController c; calls=0; RefreshState before;
        assert(c.Update(before,0,1,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
        auto after=before;
        switch(domain) {
        case 0: after.requestedEnabled=true; break;
        case 1: after.requestedRoute=2; break;
        case 2: after.profileGeneration=1; break;
        case 3: after.native.failed=true; break;
        case 4: after.native.lifecycleGeneration=1; break;
        case 5: after.present.backbufferWidth=1920; after.present.backbufferHeight=1080; break;
        case 6: after.present.colorSpaceObserved=true; after.present.colorSpace=12; break;
        case 7: after.present.hdrMetadataHash=123; break;
        case 8: after.provider.enabled=true; break;
        }
        assert(c.Update(after,0.5,2,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
        assert(calls==2);
    }

    // Copy always samples now, including while auto refresh is waiting. The
    // callback represents a new immutable snapshot; old success is never reused.
    RefreshController copy; calls=0; RefreshState s;
    copy.Update(s,0,1,RefreshRequest::Automatic,collect);
    s.requestedEnabled=true;
    bool copiedEnabled=false;
    assert(copy.Update(s,0.01,2,RefreshRequest::Copy,[&] { ++calls; copiedEnabled=s.requestedEnabled; return true; })==RefreshOutcome::Captured);
    assert(copiedEnabled && calls==2 && !copy.Pending());
    assert(copy.Update(s,0.02,3,RefreshRequest::Copy,collect)==RefreshOutcome::Captured);
    assert(calls==3);
    assert(copy.Update(s,0.03,4,RefreshRequest::Manual,collect)==RefreshOutcome::Captured);

    // Failed fresh copies must be distinguishable from successful old data.
    assert(copy.Update(s,0.04,5,RefreshRequest::Copy,[] { return false; })==RefreshOutcome::Unavailable);
    assert(copy.Pending());
    assert(copy.Update(s,0.1,6,RefreshRequest::Automatic,collect)==RefreshOutcome::NotDue);
    assert(copy.Update(s,0.54,7,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);

    // Closing/reopening or skipping UI frames must recapture accumulated changes.
    copy.Close();
    assert(copy.Update(s,0.55,8,RefreshRequest::Automatic,collect)==RefreshOutcome::NotDue);
    assert(copy.Update(s,1.04,9,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
    assert(copy.Update(s,2,300,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
    assert(copy.Update(s,2.1,301,RefreshRequest::Automatic,collect)==RefreshOutcome::NotDue);

    // HDR owner notifications also arrive on Native, where Present telemetry
    // need not update. Equal repeated metadata must not drive continuous refresh.
    HdrChangeTracker hdr;
    RefreshHdrState hdrState; hdrState.registered=true;
    assert(hdr.Observe(hdrState)==1);
    assert(hdr.Observe(hdrState)==1);
    hdrState.colorSpaceObserved=true; hdrState.colorSpace=12;
    assert(hdr.Observe(hdrState)==2);
    s.hdrChangeRevision=hdr.Observe(hdrState);
    assert(copy.Update(s,2.6,302,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
    assert(copy.Update(s,3.6,303,RefreshRequest::Automatic,collect)==RefreshOutcome::NotDue);
    hdrState.metadataHash=99;
    assert(hdr.Observe(hdrState)==3);
    hdrState.registered=false;
    assert(hdr.Observe(hdrState)==4);

    assert(copy.Update(s,4,304,RefreshRequest::Copy,[]()->bool { throw 1; })==RefreshOutcome::Unavailable);

    // Hiding the whole menu can stop ImGui frames. Reopening with the next
    // consecutive ImGui frame still requires a fresh collection.
    RefreshController reopened; calls=0; RefreshState menuState;
    reopened.Update(menuState,0,1,RefreshRequest::Automatic,collect);
    menuState.menuVisibilityGeneration=2;
    assert(reopened.Update(menuState,5,2,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
    assert(calls==2);

    // An ordinary NR setting edit changes the configuration owner even when
    // route, renderer state and profile generation remain unchanged.
    NrOptional<float> strength(1.0f);
    const auto revision=NrConfigSynchronization::ObservationRevision();
    auto configState=MakeRefreshState(false,0,0,native,present,provider,revision);
    RefreshController edits; calls=0;
    assert(edits.Update(configState,0,1,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
    strength=0.2f;
    const auto changed=NrConfigSynchronization::ObservationRevision();
    assert(changed!=revision);
    configState=MakeRefreshState(false,0,0,native,present,provider,changed);
    assert(edits.Update(configState,0.1,2,RefreshRequest::Automatic,collect)==RefreshOutcome::NotDue);
    assert(edits.Pending());
    assert(edits.Update(configState,0.5,3,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
    assert(calls==2&&!edits.Pending());
    configState.configurationRevision=std::nullopt; // exhausted identity remains unknown
    assert(edits.Update(configState,1,4,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);

    // A secondary layer can fail while the primary model remains healthy.
    NativeValues layerNative; PresentValues layerPresent; ProviderValues layerProvider;
    RefreshController layers; calls=0;
    auto layerState=MakeRefreshState(true,0,0,layerNative,layerPresent,layerProvider);
    layers.Update(layerState,0,1,RefreshRequest::Automatic,collect);
    layerNative.layer2Failed=true;
    layerState=MakeRefreshState(true,0,0,layerNative,layerPresent,layerProvider);
    assert(layers.Update(layerState,1,2,RefreshRequest::Automatic,collect)==RefreshOutcome::Captured);
    std::cout << "PASS: open/idle/counter exclusion, coalescing/ABA, state changes, fresh copy, failure, reopen, configuration owner revisions\n";
}
