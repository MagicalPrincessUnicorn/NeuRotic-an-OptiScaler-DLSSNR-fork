#pragma once
#include "PresentFgPolicy.h"
namespace DlssNr::PreFg::FinalColor
{
enum class NoticeKind { None, Active, OffPending, Pending, FreshAccepted, HudlessOnly, OptionalCleared, Unsafe };
inline NoticeKind PickNotice(const Controller::UiObservation& status, bool runtimeEnabled,
    bool presentRoute, bool sameScope)
{
    if (status.unsafe) return NoticeKind::Unsafe;
    if (!runtimeEnabled && (status.installed || status.restorationPending || status.exiting)) return NoticeKind::OffPending;
    if ((!sameScope && (status.installed || status.restorationPending)) ||
        (status.restorationPending && (!runtimeEnabled || !presentRoute || !status.wanted)))
        return runtimeEnabled ? NoticeKind::Pending : NoticeKind::OffPending;
    if (!sameScope) return NoticeKind::None;
    if (runtimeEnabled && presentRoute && status.wanted && status.installed) return NoticeKind::Active;
    if (status.fresh == FreshPolicy::HudlessOnly) return NoticeKind::HudlessOnly;
    if (status.fresh == FreshPolicy::OptionalCleared) return NoticeKind::OptionalCleared;
    if (status.fresh != FreshPolicy::Pending) return NoticeKind::FreshAccepted;
    if (status.restorationPending) return runtimeEnabled ? NoticeKind::Pending : NoticeKind::OffPending;
    return NoticeKind::None;
}
}
