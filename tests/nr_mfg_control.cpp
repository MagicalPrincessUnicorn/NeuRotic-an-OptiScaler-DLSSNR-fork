#include "../OptiScaler/mfg/MfgControl.h"
#include "../OptiScaler/mfg/MfgOptionsSnapshot.h"

#include <cassert>
#include <cstdint>
#include <cstring>

using namespace Neurotic::Mfg;

static void TestGameAndOff()
{
    const auto game = ResolveMfgSelection(MfgSelection::Game, true, 2, false, 7, 0);
    assert(game.status == MfgDecisionStatus::PassThrough && game.enabled && game.generated == 2);
    const auto selectedOff = ResolveMfgSelection(MfgSelection::Off, true, 1, false, 7, 0);
    assert(selectedOff.status == MfgDecisionStatus::SelectedOff && !selectedOff.enabled &&
           selectedOff.generated == 1);
    const auto gameOff = ResolveMfgSelection(MfgSelection::X4, false, 1, true, 7, 7);
    assert(gameOff.status == MfgDecisionStatus::PreserveGameOff && !gameOff.enabled &&
           gameOff.generated == 1);
}

static void TestFixedChoicesAndCurrentness()
{
    const auto x2 = ResolveMfgSelection(MfgSelection::X2, true, 3, false, 7, 7);
    const auto x3 = ResolveMfgSelection(MfgSelection::X3, true, 1, true, 7, 7);
    const auto x4 = ResolveMfgSelection(MfgSelection::X4, true, 2, true, 7, 7);
    assert(x2.status == MfgDecisionStatus::Fixed && x2.generated == 1);
    assert(x3.status == MfgDecisionStatus::Fixed && x3.generated == 2);
    assert(x4.status == MfgDecisionStatus::Fixed && x4.generated == 3);
    for (auto selection : {MfgSelection::X5, MfgSelection::X6})
    {
        const auto bounded = ResolveMfgSelection(selection, true, 1, true, 7, 7, 3);
        assert(bounded.status == MfgDecisionStatus::Unavailable && bounded.generated == 1);
        const auto higher = ResolveMfgSelection(selection, true, 1, true, 7, 7, 5);
        assert(higher.status == MfgDecisionStatus::Fixed &&
            higher.generated == (selection == MfgSelection::X5 ? 4u : 5u));
        const auto missing = ResolveMfgSelection(selection, true, 2, true, 7, 7, 0);
        assert(missing.status == MfgDecisionStatus::Unavailable && missing.generated == 2);
        const auto disabled = ResolveMfgSelection(selection, false, 1, true, 7, 7, 5);
        assert(disabled.status == MfgDecisionStatus::PreserveGameOff && !disabled.enabled);
    }
    const auto unqualified = ResolveMfgSelection(MfgSelection::X4, true, 1, false, 7, 7);
    assert(unqualified.status == MfgDecisionStatus::Unavailable && unqualified.generated == 1);
    const auto stale = ResolveMfgSelection(MfgSelection::X4, true, 1, true, 8, 7);
    assert(stale.status == MfgDecisionStatus::Stale && stale.generated == 1);
    const auto unknown = ResolveMfgSelection(MfgSelection::X2, true, 1, false, 0, 0);
    assert(unknown.status == MfgDecisionStatus::Stale && unknown.generated == 1);
}

static void TestReceiptDoesNotPromoteAcceptanceToObservation()
{
    MfgRequestJournal journal;
    const auto decision = ResolveMfgSelection(MfgSelection::X4, true, 1, true, 9, 9);
    const auto attempt = journal.Begin(3, 9, 2, true, 1, MfgSelection::X4, decision);
    assert(attempt == 1);
    journal.Complete(attempt, 0);
    auto receipt = journal.Current();
    assert(receipt.attempt == 1 && receipt.viewport == 3 && receipt.generation == 9);
    assert(receipt.gameGenerated == 1 && receipt.selected == MfgSelection::X4 &&
        receipt.forwardedGenerated == 3);
    assert(receipt.setterResult.has_value() && receipt.setterResult.value() == 0);
    assert(receipt.accepted && !receipt.observedPresented.has_value());
    assert(!journal.Observe(3, 8, 0, 0));
    assert(!journal.Observe(4, 9, 0, 0));
    assert(journal.Observe(3, 9, 0, 0));
    receipt = journal.Current();
    assert(receipt.observedMax.has_value() && receipt.observedMax.value() == 0);
    assert(receipt.observedPresented.has_value() && receipt.observedPresented.value() == 0);
    journal.Invalidate(10);
    receipt = journal.Current();
    assert(receipt.stale && !journal.Observe(3, 9, 3, 4));
}

static void TestFailedProviderResultStaysFailed()
{
    MfgRequestJournal journal;
    const auto decision = ResolveMfgSelection(MfgSelection::X3, true, 1, true, 2, 2);
    const auto attempt = journal.Begin(0, 2, 4, true, 1, MfgSelection::X3, decision);
    journal.Complete(attempt, -9);
    const auto receipt = journal.Current();
    assert(receipt.setterResult.has_value() && receipt.setterResult.value() == -9);
    assert(!receipt.accepted && !receipt.observedPresented.has_value());
}

static void TestMaximumAndFallback()
{
    assert(AdvertisedMfgGeneratedMax(1, true, true) == 3);
    assert(AdvertisedMfgGeneratedMax(2, true, true) == 3);
    assert(AdvertisedMfgGeneratedMax(4, true, true) == 4);
    assert(AdvertisedMfgGeneratedMax(1, false, true) == 1);
    assert(AdvertisedMfgGeneratedMax(1, true, false) == 1);
    assert(AdvertisedMfgGeneratedMax(1, true, true, 5) == 5);
    assert(AdvertisedMfgGeneratedMax(1, true, true, 0) == 1);
    assert(AdvertisedMfgGeneratedMax(5, true, true, 3) == 5); // preserve raw native capability
    assert(AdvertisedMfgGeneratedMax(1, true, false, 5) == 1);
    assert(ShouldFallbackMfgOptions(MfgDecisionStatus::Fixed, 3, 1, false));
    assert(!ShouldFallbackMfgOptions(MfgDecisionStatus::Fixed, 1, 1, false));
    assert(!ShouldFallbackMfgOptions(MfgDecisionStatus::Fixed, 3, 1, true));
    assert(!ShouldFallbackMfgOptions(MfgDecisionStatus::Unavailable, 1, 1, false));
    MfgRequestJournal journal;
    const auto decision = ResolveMfgSelection(MfgSelection::X4, true, 1, true, 6, 6);
    const auto attempt = journal.Begin(0, 6, 5, true, 1, MfgSelection::X4, decision);
    journal.CompleteFallback(attempt, -7, 0, true, 1);
    const auto receipt = journal.Current();
    assert(receipt.fellBackToGame && receipt.overrideResult == -7 &&
        receipt.setterResult == 0 && receipt.accepted && receipt.forwardedGenerated == 1);
}

static void TestOptionsSnapshotUsesDeclaredAbi()
{
    sl::DLSSGOptions v1{};
    v1.structVersion = 1;
    v1.numFramesToGenerate = 2;
    const uint32_t poison = 0x7f7f7f7f;
    std::memcpy(reinterpret_cast<unsigned char*>(&v1) + 112, &poison, sizeof(poison));
    const auto first = CaptureOptions(v1);
    assert(first.supported && first.copiedBytes == 104);
    assert(first.value.numFramesToGenerate == 2);
    assert(first.value.enableUserInterfaceRecomposition == sl::Boolean::eFalse);
    assert(v1.structVersion == 1 && v1.numFramesToGenerate == 2);

    sl::DLSSGOptions v5{};
    v5.structVersion = 5;
    v5.dynamicTargetFrameRate = 117.0f;
    const auto fifth = CaptureOptions(v5);
    assert(fifth.supported && fifth.value.dynamicTargetFrameRate == 117.0f);
    sl::DLSSGOptions unknown{};
    unknown.structVersion = 77;
    const auto rejected = CaptureOptions(unknown);
    assert(!rejected.supported && rejected.copiedBytes == 0);

    sl::DLSSGOptions callback{};
    callback.onErrorCallback = [](const sl::APIError&) {};
    const auto borrowed = CaptureOptions(callback);
    assert(borrowed.supported && !borrowed.replayable && borrowed.value.onErrorCallback == nullptr);
    sl::DLSSGOptions chained{};
    sl::DLSSGOptions extension{};
    chained.next = &extension;
    const auto borrowedChain = CaptureOptions(chained);
    assert(borrowedChain.supported && !borrowedChain.replayable && borrowedChain.value.next == nullptr);
}

// Losing the rejection when the next per-frame request begins must not
// re-enable the same failed override or erase the fallback explanation.
template<class Journal> static void TestRejectionSurvivesSubsequentRequests()
{
    Journal journal;
    if constexpr (requires { journal.HighRatioRefusal(); journal.RejectHighRatio(3u, 38); })
    {
        assert(!journal.HighRatioRefusal().blocked);
        journal.RejectHighRatio(1, 38); // ordinary 2x is not a high-ratio refusal
        journal.RejectHighRatio(3, 0); // successful setter must not latch
        assert(!journal.HighRatioRefusal().blocked);
        journal.RejectHighRatio(3, 38);
        assert(journal.HighRatioRefusal().blocked);
        assert(journal.HighRatioRefusal().requestedGenerated == 3);
        assert(journal.HighRatioRefusal().result == 38);
        unsigned overrideCalls = 0;
        for (unsigned i = 0; i < 1244; ++i)
        {
            const auto selected = static_cast<MfgSelection>(static_cast<unsigned>(MfgSelection::X3) + i % 4);
            const auto decision = ResolveMfgSelection(selected, true, 1,
                true, 3, 3, 5, journal.HighRatioRefusal().blocked);
            if (decision.status == MfgDecisionStatus::Fixed && decision.generated > 1) ++overrideCalls;
            assert(decision.status == MfgDecisionStatus::Unavailable && decision.generated == 1);
            const auto attempt = journal.Begin(0, 3, 5, true, 1, selected, decision);
            journal.Complete(attempt, 0);
        }
        assert(overrideCalls == 0);
        journal.Invalidate(4); // cached-export/hook churn is not a new game session
        assert(journal.HighRatioRefusal().blocked);
        for (auto selection : {MfgSelection::Game, MfgSelection::Off, MfgSelection::X2})
        {
            const auto decision = ResolveMfgSelection(selection, true, 1,
                !journal.HighRatioRefusal().blocked, 4, 4, 5);
            assert(decision.status != MfgDecisionStatus::Unavailable);
        }
        const auto off = ResolveMfgSelection(MfgSelection::X4, false, 1, false, 4, 4, 5);
        assert(!off.enabled && off.status == MfgDecisionStatus::PreserveGameOff);
        Journal nextSession;
        assert(!nextSession.HighRatioRefusal().blocked);
    }
    else
        assert(!"Missing session-level high-ratio rejection latch");
}

static void TestRejectedCapacityAndEffectiveSetting()
{
    assert(AdvertisedMfgGeneratedMax(1, true, true, 5, true) == 1);
    assert(AdvertisedMfgGeneratedMax(5, true, true, 5, true) == 1);
    assert(AdvertisedMfgGeneratedMax(0, true, true, 5, true) == 0);
    assert(AdvertisedMfgGeneratedMax(5, false, true, 5, true) == 5);
    MfgRequestJournal journal;
    const auto decision = ResolveMfgSelection(MfgSelection::X4, true, 1, true, 2, 2, 5);
    const auto attempt = journal.Begin(0, 2, 5, true, 1, MfgSelection::X4, decision);
    assert(!AcceptedMfgMultiplier(journal.Current()).has_value());
    journal.CompleteFallback(attempt, 38, -9, true, 1);
    assert(!AcceptedMfgMultiplier(journal.Current()).has_value());
    journal.CompleteFallback(attempt, 38, 0, true, 1);
    assert(AcceptedMfgMultiplier(journal.Current()) == 2u);
    journal.CompleteFallback(attempt, 38, 0, false, 1);
    assert(AcceptedMfgMultiplier(journal.Current()) == 0u);
    journal.Invalidate(3);
    assert(!AcceptedMfgMultiplier(journal.Current()).has_value());
}

template<class Journal> static void TestUntrackedAndOverlappingSettersAreUnconfirmed()
{
    Journal journal;
    if constexpr (requires { journal.BeginSetter(); journal.EndSetter(); })
    {
        journal.BeginSetter();
        const auto decision = ResolveMfgSelection(MfgSelection::X4, true, 1, true, 2, 2, 5);
        const auto a = journal.Begin(0, 2, 5, true, 1, MfgSelection::X4, decision);
        journal.CompleteFallback(a, 38, 0, true, 1);
        journal.RejectHighRatio(3, 38);
        journal.EndSetter();
        assert(AcceptedMfgMultiplier(journal.Current()) == 2u);
        journal.BeginSetter(); // callback/extension/unknown ABI pass-through
        journal.EndSetter();
        assert(!AcceptedMfgMultiplier(journal.Current()).has_value());
        assert(journal.HighRatioRefusal().blocked);
        journal.BeginSetter();
        const auto first = journal.Begin(0, 2, 5, true, 1, MfgSelection::X4, decision);
        journal.BeginSetter();
        const auto off = ResolveMfgSelection(MfgSelection::Off, true, 1, true, 2, 2, 5);
        const auto second = journal.Begin(0, 2, 5, true, 1, MfgSelection::Off, off);
        journal.Complete(second, 0);
        journal.EndSetter();
        journal.CompleteFallback(first, 38, 0, true, 1);
        journal.EndSetter();
        assert(!AcceptedMfgMultiplier(journal.Current()).has_value());
        // A later isolated setter can establish a fresh accepted setting.
        journal.BeginSetter();
        const auto fresh = journal.Begin(0, 2, 5, true, 1, MfgSelection::Off, off);
        journal.Complete(fresh, 0);
        journal.EndSetter();
        assert(AcceptedMfgMultiplier(journal.Current()) == 0u);
    }
    else
        assert(!"Untracked/overlapping setters must invalidate accepted setting");
}

int main()
{
    assert(SelectionFromStoredGenerated(0) == MfgSelection::Off);
    assert(SelectionFromStoredGenerated(1) == MfgSelection::X2);
    assert(SelectionFromStoredGenerated(2) == MfgSelection::X3);
    assert(SelectionFromStoredGenerated(3) == MfgSelection::X4);
    assert(SelectionFromStoredGenerated(4) == MfgSelection::X5);
    assert(SelectionFromStoredGenerated(5) == MfgSelection::X6);
    assert(SelectionFromStoredGenerated(6) == MfgSelection::Game);
    TestGameAndOff();
    TestFixedChoicesAndCurrentness();
    TestReceiptDoesNotPromoteAcceptanceToObservation();
    TestFailedProviderResultStaysFailed();
    TestMaximumAndFallback();
    TestOptionsSnapshotUsesDeclaredAbi();
    TestRejectionSurvivesSubsequentRequests<MfgRequestJournal>();
    TestRejectedCapacityAndEffectiveSetting();
    TestUntrackedAndOverlappingSettersAreUnconfirmed<MfgRequestJournal>();
}
