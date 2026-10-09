#pragma once

#include <atomic>
#include <cstdint>
#include "../SynchronizedOptional.h"

namespace DlssNr::ExperimentalPolicy
{
enum class Guardrail : std::uint8_t
{
    None,
    Multipass,
    Hdr,
    FrameGeneration
};

struct Snapshot
{
    bool active = false;
    bool multipass = false;
    bool hdr = false;
    bool frameGeneration = false;
    std::uint64_t generation = 0;
    bool preparedDepth = false;

    bool Allows(Guardrail guardrail) const noexcept
    {
        if (guardrail == Guardrail::Multipass || guardrail == Guardrail::Hdr ||
            guardrail == Guardrail::FrameGeneration) return true;
        if (!active) return false;
        switch (guardrail)
        {
        case Guardrail::Multipass: return multipass;
        case Guardrail::Hdr: return hdr;
        case Guardrail::FrameGeneration: return frameGeneration;
        default: return false;
        }
    }
};

inline std::atomic<std::uint64_t> Generation { 1 };
inline std::atomic<bool> SessionReady { false };

struct UiDraft
{
    bool initialized = false;
    bool dirty = false;
    bool multipass = false;
    bool hdr = false;
    bool frameGeneration = false;
    bool preSrSoftReset = false;
    bool preparedDepth = false;
};

inline UiDraft Draft;

inline constexpr bool AnySelected(const UiDraft& draft) noexcept
{
    return draft.preSrSoftReset || draft.preparedDepth;
}

template<class C> bool Requested(const C& config) noexcept
{
    return config.DlssNrExperimentalMode.value_or_default() &&
        (config.DlssNrPreSrSoftReset.value_or_default() || config.DlssNrPreparedDepth.value_or_default());
}

// A saved legacy child without its master is inert and still needs consent when selected.
template<class C> bool NewChoicesRequested(const C& config, const UiDraft& draft) noexcept
{
    const bool saved = config.DlssNrExperimentalMode.value_or_default();
    return draft.preSrSoftReset && !(saved && config.DlssNrPreSrSoftReset.value_or_default()) ||
           (draft.preparedDepth && !(saved && config.DlssNrPreparedDepth.value_or_default()));
}

template<class C> void ResetDraft(const C& config) noexcept
{
    const bool legacyMaster = config.DlssNrExperimentalMode.value_or_default();
    Draft = { true, false,
        false,
        false,
        false,
        legacyMaster && config.DlssNrPreSrSoftReset.value_or_default(),
        legacyMaster && config.DlssNrPreparedDepth.value_or_default() };
}

template<class C> void EnsureDraft(const C& config) noexcept
{
    if (!Draft.initialized) ResetDraft(config);
}

template<class C> void ApplyDraft(C& config, const UiDraft& draft) noexcept
{
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    config.DlssNrExperimentalMode = AnySelected(draft);
    config.DlssNrOverrideMultipassGuardrails = false; // retired INI opt-in
    config.DlssNrOverrideHdrGuardrails = false; // retired INI opt-ins
    config.DlssNrOverrideFgGuardrails = false;
    config.DlssNrPreSrSoftReset = draft.preSrSoftReset;
    config.DlssNrPreparedDepth = draft.preparedDepth;
}

template<class C> void ApplyDraft(C& config) noexcept
{
    EnsureDraft(config);
    ApplyDraft(config, Draft);
}

template<class C, class Persist> bool SaveDraft(C& config, Persist persist)
{
    EnsureDraft(config);
    const auto choice = Draft;
    if (!persist(choice)) return false;
    NrConfigSynchronization::Guard lock(NrConfigSynchronization::Mutex());
    // Leave the previous ready policy intact on failed persistence. Successful
    // publication stays inactive until Applied has written the session marker.
    SessionReady.store(false, std::memory_order_release);
    ApplyDraft(config, choice);
    return true;
}

inline void DiscardDraft() noexcept { Draft.initialized = false; Draft.dirty = false; }

inline void Changed() noexcept { Generation.fetch_add(1, std::memory_order_relaxed); }

template<class C> Snapshot Capture(const C& config) noexcept
{
    const bool ready = SessionReady.load(std::memory_order_acquire);
    return {
        ready && Requested(config),
        config.DlssNrOverrideMultipassGuardrails.value_or_default(),
        true,
        true,
        Generation.load(std::memory_order_relaxed),
        ready && Requested(config) && config.DlssNrPreparedDepth.value_or_default()
    };
}

inline std::uint32_t Key(const Snapshot& value) noexcept
{
    return (value.active ? 1u : 0u) | (value.multipass ? 2u : 0u) |
           (value.hdr ? 4u : 0u) | (value.frameGeneration ? 8u : 0u) |
           (value.preparedDepth ? 16u : 0u);
}
} // namespace DlssNr::ExperimentalPolicy
