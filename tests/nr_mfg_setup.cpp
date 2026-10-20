#include "../OptiScaler/mfg/MfgSetup.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <optional>
using namespace Neurotic::Mfg;

template<class Input> bool TestLateEnableAdvice(Input input)
{
    if constexpr (requires { MfgLateEnableAdvice(input); })
    {
        input.supported = input.selected = true;
        input.startedSelected = false;
        input.routePending = false;
        input.publication = MfgRuntimeStatus::Published;
        if (!MfgLateEnableAdvice(input) || ResolveMfgSetup(input) != MfgSetupStatus::Ready)
            return false; // Published unlock and late-enable advice are independent.
        input.publication = MfgRuntimeStatus::Unavailable;
        input.reason = MfgRuntimeReason::WaitingWrapper;
        if (!MfgLateEnableAdvice(input)) return false;
        input.startedSelected = true;
        if (MfgLateEnableAdvice(input)) return false;
        input.startedSelected = false;
        input.selected = false;
        if (MfgLateEnableAdvice(input)) return false;
        input.selected = true;
        input.supported = false;
        return !MfgLateEnableAdvice(input);
    }
    else return false;
}

int main()
{
    MfgSetupInput input;
    if (!TestLateEnableAdvice(input))
    {
        std::fputs("FAIL: late enablement must retain restart advice independently of unlock publication\n", stderr);
        return 1;
    }
    input.supported = true;
    input.selected = true;
    input.startedSelected = true;
    input.reason = MfgRuntimeReason::ProviderAmbiguous;
    assert(ResolveMfgSetup(input) == MfgSetupStatus::Blocked);
    input.startedSelected = false;
    assert(ResolveMfgSetup(input) == MfgSetupStatus::Blocked);
    input.routePending = true;
    assert(ResolveMfgSetup(input) == MfgSetupStatus::Restart);
    input.routePending = false;
    input.reason = MfgRuntimeReason::None;
    assert(ResolveMfgSetup(input) == MfgSetupStatus::Restart);
    input.startedSelected = true;
    assert(ResolveMfgSetup(input) == MfgSetupStatus::Waiting);
    input.reason = MfgRuntimeReason::ProviderProfile;
    assert(ResolveMfgSetup(input) == MfgSetupStatus::Blocked);
    for (auto reason : {MfgRuntimeReason::WaitingWrapper, MfgRuntimeReason::WaitingAdapter,
                       MfgRuntimeReason::ProviderMissing})
    {
        input.reason = reason;
        assert(ResolveMfgSetup(input) == MfgSetupStatus::Waiting);
    }
    for (auto status : {MfgRuntimeStatus::Refused, MfgRuntimeStatus::Indeterminate, MfgRuntimeStatus::Stale})
    {
        input.publication = status;
        assert(ResolveMfgSetup(input) == MfgSetupStatus::Blocked);
    }
    input.publication = MfgRuntimeStatus::Published;
    assert(ResolveMfgSetup(input) == MfgSetupStatus::Ready);
    input.selected = false;
    assert(ResolveMfgSetup(input) == MfgSetupStatus::Off);
    input.supported = false;
    assert(ResolveMfgSetup(input) == MfgSetupStatus::Unsupported);
    assert(MfgSelectableGeneratedMax(false, 5) == 1);
    assert(MfgSelectableGeneratedMax(true, 3) == 3);
    assert(MfgSelectableGeneratedMax(true, 5) == 5);
    assert(MfgSelectableGeneratedMax(true, 99) == 1);
    assert(MfgSelectableGeneratedMax(true, 5, true) == 1);
    assert(MfgSelectableGeneratedMax(true, 3, true) == 1);
    struct Settings
    {
        bool FGDLSSGNativeMfgExperimental = true;
        std::optional<int> FGDLSSGOverrideInterpolationCount = 3;
        bool unrelated = true;
    } settings;
    SetNativeMfgSelected(settings, false);
    assert(!settings.FGDLSSGNativeMfgExperimental &&
        !settings.FGDLSSGOverrideInterpolationCount.has_value() && settings.unrelated);
    std::puts("PASS: native setup status, real restart transitions, failure reasons and qualified ratio bounds");
}
