#pragma once
#include "MfgAdaUnlock.h"
namespace Neurotic::Mfg
{
enum class MfgSetupStatus { Unsupported, Off, Restart, Waiting, Blocked, Ready };
template<class ConfigType> void SetNativeMfgSelected(ConfigType& config, bool enabled)
{
    config.FGDLSSGNativeMfgExperimental = enabled;
    // The ratio field is also read by the legacy native DLSSG override path.
    if (!enabled) config.FGDLSSGOverrideInterpolationCount.reset();
}
struct MfgSetupInput
{
    bool supported = false;
    bool selected = false;
    bool startedSelected = false;
    bool routePending = false;
    MfgRuntimeStatus publication = MfgRuntimeStatus::Unavailable;
    MfgRuntimeReason reason = MfgRuntimeReason::None;
};
// Publication does not prove the already initialized game provider accepted a
// higher ratio. Keep late-enable advice independent from publication readiness.
constexpr bool MfgLateEnableAdvice(const MfgSetupInput& input) noexcept
{
    return input.supported && input.selected && !input.startedSelected;
}
constexpr MfgSetupStatus ResolveMfgSetup(const MfgSetupInput& input) noexcept
{
    if (!input.supported) return MfgSetupStatus::Unsupported;
    if (!input.selected) return MfgSetupStatus::Off;
    if (input.routePending) return MfgSetupStatus::Restart;
    if (input.publication == MfgRuntimeStatus::Published) return MfgSetupStatus::Ready;
    if (input.publication != MfgRuntimeStatus::Unavailable ||
        (input.reason != MfgRuntimeReason::None && input.reason != MfgRuntimeReason::WaitingWrapper &&
         input.reason != MfgRuntimeReason::WaitingAdapter && input.reason != MfgRuntimeReason::ProviderMissing))
        return MfgSetupStatus::Blocked;
    if (!input.startedSelected && input.reason == MfgRuntimeReason::None) return MfgSetupStatus::Restart;
    return MfgSetupStatus::Waiting;
}
constexpr unsigned MfgSelectableGeneratedMax(bool published, unsigned maximum,
    bool highRatioRejected = false) noexcept
{
    return !highRatioRejected && published && (maximum == 3 || maximum == 5) ? maximum : 1u;
}
}
