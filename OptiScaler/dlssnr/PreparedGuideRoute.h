#pragma once
#include <Windows.h>
#include "NativeGuideRoute.h"
#include "connections/ConnectionPolicy.h"

namespace DlssNr::PreparedGuides
{
// Applied selection, not evidence that any source frame was processed.
// Reserve NR for the selected native or external guide owner on refused frames too. Original Present and
// independent frame-generation forwarding remain owned by their usual hooks.
inline bool ExternalRoute() noexcept
{
    const int source=NativeGuides::SelectedSource();
    return source==3||source==4;
}
inline bool ExclusiveRoute() noexcept {return ExternalRoute()||NativeGuides::Selected()||Connections::PreparedOwnerClaimed();}
// Built-in selection permits capture; it does not prove that a frame was
// acquired. Generic Present remains available when capture refuses before a
// claim. A live/unsafe claim or an already handled Present still excludes it.
// External producers retain their applied-policy reservation because their
// work can enter outside this Present scope.
inline bool OwnsPresentOutput() noexcept
{
    return ExternalRoute() || Connections::PreparedOwnerClaimed() || Connections::PreparedPresentHandled();
}
constexpr bool CanDispatch(bool selected,bool enabled,unsigned route) noexcept
{
    return selected && enabled && (route==1 || route==2);
}
}
