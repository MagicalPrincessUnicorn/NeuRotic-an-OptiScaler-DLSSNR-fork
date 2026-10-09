#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

namespace Neurotic::Mfg
{
enum class MfgSelection : uint8_t { Game, Off, X2, X3, X4, X5, X6 };
constexpr MfgSelection SelectionFromStoredGenerated(int value) noexcept
{
    switch (value)
    {
    case 0: return MfgSelection::Off;
    case 1: return MfgSelection::X2;
    case 2: return MfgSelection::X3;
    case 3: return MfgSelection::X4;
    case 4: return MfgSelection::X5;
    case 5: return MfgSelection::X6;
    default: return MfgSelection::Game;
    }
}
enum class MfgDecisionStatus : uint8_t { PassThrough, PreserveGameOff, SelectedOff, Fixed, Unavailable, Stale };

constexpr uint32_t AdvertisedMfgGeneratedMax(uint32_t nativeMax, bool experimental,
    bool ownedPublication, uint32_t qualifiedMax = 3, bool highRatioRejected = false) noexcept
{
    if (experimental && highRatioRejected) return nativeMax > 1 ? 1 : nativeMax;
    return experimental && ownedPublication && (qualifiedMax == 3 || qualifiedMax == 5) &&
        nativeMax < qualifiedMax ? qualifiedMax : nativeMax;
}

constexpr bool ShouldFallbackMfgOptions(MfgDecisionStatus status, uint32_t forwarded,
    uint32_t gameRequested, bool accepted) noexcept
{
    return !accepted && status == MfgDecisionStatus::Fixed && forwarded != gameRequested;
}

struct MfgDecision
{
    MfgDecisionStatus status = MfgDecisionStatus::PassThrough;
    bool enabled = false;
    uint32_t generated = 0;
    uint64_t providerGeneration = 0;
};

// Selection is a total multiplier; the Streamline field counts generated frames.
// High ratios require the current exact Ada publication, whereas ordinary 2x
// uses the provider's existing DLSS-G path.
constexpr MfgDecision ResolveMfgSelection(MfgSelection selection, bool gameEnabled,
    uint32_t gameGenerated, bool qualified, uint64_t providerGeneration,
    uint64_t selectedGeneration, uint32_t qualifiedMax = 3, bool highRatioRejected = false) noexcept
{
    MfgDecision decision {MfgDecisionStatus::PassThrough, gameEnabled, gameGenerated, providerGeneration};
    if (!gameEnabled)
    {
        decision.status = MfgDecisionStatus::PreserveGameOff;
        return decision;
    }
    if (selection == MfgSelection::Game) return decision;
    if (selection == MfgSelection::Off)
    {
        decision.status = MfgDecisionStatus::SelectedOff;
        decision.enabled = false;
        return decision;
    }
    if (!providerGeneration || providerGeneration != selectedGeneration)
    {
        decision.status = MfgDecisionStatus::Stale;
        return decision;
    }
    const auto generated = static_cast<uint32_t>(selection) - static_cast<uint32_t>(MfgSelection::X2) + 1;
    if (generated > 5 || (generated > 1 && highRatioRejected) || (selection != MfgSelection::X2 &&
        (!qualified || (qualifiedMax != 3 && qualifiedMax != 5) || generated > qualifiedMax)))
    {
        decision.status = MfgDecisionStatus::Unavailable;
        return decision;
    }
    decision.status = MfgDecisionStatus::Fixed;
    decision.generated = generated;
    return decision;
}

// Value-only diagnostics. A setter result and a later state query remain
// separate observations; neither certifies distinct generated images.
struct MfgRequestReceipt
{
    uint64_t attempt = 0;
    uint32_t viewport = 0;
    uint64_t generation = 0;
    uint32_t callerAbi = 0;
    bool gameEnabled = false;
    uint32_t gameGenerated = 0;
    MfgSelection selected = MfgSelection::Game;
    bool forwardedEnabled = false;
    uint32_t forwardedGenerated = 0;
    MfgDecisionStatus selectionStatus = MfgDecisionStatus::PassThrough;
    std::optional<int> setterResult;
    std::optional<int> overrideResult;
    bool fellBackToGame = false;
    bool accepted = false;
    std::optional<uint32_t> observedMax;
    std::optional<uint32_t> observedPresented;
    bool stale = false;
};

// An accepted setting is not proof of distinct generated-frame output.
// Zero means FG off; absent means the latest setter outcome is unconfirmed.
constexpr std::optional<uint32_t> AcceptedMfgMultiplier(const MfgRequestReceipt& request) noexcept
{
    if (!request.attempt || request.stale || !request.setterResult.has_value() || !request.accepted)
        return std::nullopt;
    if (!request.forwardedEnabled) return 0u;
    if (request.forwardedGenerated < 1 || request.forwardedGenerated > 5) return std::nullopt;
    return request.forwardedGenerated + 1;
}

struct MfgHighRatioRefusal
{
    bool blocked = false;
    uint32_t requestedGenerated = 0;
    int result = 0;
};

class MfgRequestJournal
{
    mutable std::mutex mutex_;
    uint64_t nextAttempt_ = 0;
    MfgRequestReceipt current_;
    MfgHighRatioRefusal highRatioRefusal_;
    uint32_t activeSetters_ = 0;
    bool overlappingSetters_ = false;

public:
    void BeginSetter() noexcept
    {
        std::lock_guard lock(mutex_);
        current_.stale = true; // Includes calls whose options cannot be captured.
        if (activeSetters_++) overlappingSetters_ = true;
    }

    void EndSetter() noexcept
    {
        std::lock_guard lock(mutex_);
        if (overlappingSetters_) current_.stale = true;
        if (activeSetters_ && --activeSetters_ == 0) overlappingSetters_ = false;
    }

    // A failed setter is not a permanent hardware/provider capacity claim.
    // Conservatively stop high-ratio overrides until the process restarts;
    // per-frame receipts, FG toggles and cached-export churn cannot clear it.
    void RejectHighRatio(uint32_t generated, int result) noexcept
    {
        if (generated <= 1 || result == 0) return;
        std::lock_guard lock(mutex_);
        if (!highRatioRefusal_.blocked) highRatioRefusal_ = {true, generated, result};
    }

    MfgHighRatioRefusal HighRatioRefusal() const noexcept
    {
        std::lock_guard lock(mutex_);
        return highRatioRefusal_;
    }

    uint64_t Begin(uint32_t viewport, uint64_t generation, uint32_t callerAbi,
        bool gameEnabled, uint32_t gameGenerated, MfgSelection selected,
        const MfgDecision& decision) noexcept
    {
        std::lock_guard lock(mutex_);
        if (++nextAttempt_ == 0) ++nextAttempt_;
        current_ = {nextAttempt_, viewport, generation, callerAbi, gameEnabled, gameGenerated,
            selected,
            decision.enabled, decision.generated, decision.status};
        current_.stale = overlappingSetters_;
        return nextAttempt_;
    }

    void Complete(uint64_t attempt, int result) noexcept
    {
        std::lock_guard lock(mutex_);
        if (current_.attempt != attempt || current_.stale) return;
        current_.setterResult = result;
        current_.accepted = result == 0;
    }

    void CompleteFallback(uint64_t attempt, int overrideResult, int finalResult,
        bool gameEnabled, uint32_t gameGenerated) noexcept
    {
        std::lock_guard lock(mutex_);
        if (current_.attempt != attempt || current_.stale) return;
        current_.overrideResult = overrideResult;
        current_.setterResult = finalResult;
        current_.accepted = finalResult == 0;
        current_.fellBackToGame = true;
        current_.forwardedEnabled = gameEnabled;
        current_.forwardedGenerated = gameGenerated;
    }

    bool Observe(uint32_t viewport, uint64_t generation, std::optional<uint32_t> max,
        std::optional<uint32_t> presented) noexcept
    {
        std::lock_guard lock(mutex_);
        if (!current_.attempt || current_.stale || current_.viewport != viewport ||
            !generation || current_.generation != generation) return false;
        current_.observedMax = max;
        current_.observedPresented = presented;
        return true;
    }

    void Invalidate(uint64_t generation) noexcept
    {
        std::lock_guard lock(mutex_);
        if (current_.attempt && current_.generation != generation) current_.stale = true;
    }

    MfgRequestReceipt Current() const noexcept
    {
        std::lock_guard lock(mutex_);
        return current_;
    }
};

class MfgSetterScope
{
    MfgRequestJournal& journal_;
public:
    explicit MfgSetterScope(MfgRequestJournal& journal) noexcept : journal_(journal) { journal_.BeginSetter(); }
    ~MfgSetterScope() { journal_.EndSetter(); }
    MfgSetterScope(const MfgSetterScope&) = delete;
    MfgSetterScope& operator=(const MfgSetterScope&) = delete;
};
}
