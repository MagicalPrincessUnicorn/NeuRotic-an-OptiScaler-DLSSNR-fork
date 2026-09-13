#pragma once

#include <atomic>
#include <cstdint>

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

    bool Allows(Guardrail guardrail) const noexcept
    {
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
    bool active = false;
    bool multipass = false;
    bool hdr = false;
    bool frameGeneration = false;
    bool preSrSoftReset = false;
};

inline UiDraft Draft;

template<class C> void ResetDraft(const C& config) noexcept
{
    Draft = { true, false,
        config.DlssNrExperimentalMode.value_or_default(),
        config.DlssNrOverrideMultipassGuardrails.value_or_default(),
        config.DlssNrOverrideHdrGuardrails.value_or_default(),
        config.DlssNrOverrideFgGuardrails.value_or_default(),
        config.DlssNrPreSrSoftReset.value_or_default() };
}

template<class C> void EnsureDraft(const C& config) noexcept
{
    if (!Draft.initialized) ResetDraft(config);
}

template<class C> void ApplyDraft(C& config) noexcept
{
    EnsureDraft(config);
    config.DlssNrExperimentalMode = Draft.active;
    config.DlssNrOverrideMultipassGuardrails = Draft.multipass;
    config.DlssNrOverrideHdrGuardrails = Draft.hdr;
    config.DlssNrOverrideFgGuardrails = Draft.frameGeneration;
    config.DlssNrPreSrSoftReset = Draft.preSrSoftReset;
}

inline void DiscardDraft() noexcept { Draft.initialized = false; Draft.dirty = false; }

inline void Changed() noexcept { Generation.fetch_add(1, std::memory_order_relaxed); }

template<class C> Snapshot Capture(const C& config) noexcept
{
    const bool ready = SessionReady.load(std::memory_order_acquire);
    return {
        ready && config.DlssNrExperimentalMode.value_or_default(),
        config.DlssNrOverrideMultipassGuardrails.value_or_default(),
        config.DlssNrOverrideHdrGuardrails.value_or_default(),
        config.DlssNrOverrideFgGuardrails.value_or_default(),
        Generation.load(std::memory_order_relaxed)
    };
}

inline std::uint32_t Key(const Snapshot& value) noexcept
{
    return (value.active ? 1u : 0u) | (value.multipass ? 2u : 0u) |
           (value.hdr ? 4u : 0u) | (value.frameGeneration ? 8u : 0u);
}
} // namespace DlssNr::ExperimentalPolicy
