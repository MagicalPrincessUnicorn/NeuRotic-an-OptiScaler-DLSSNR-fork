#pragma once
#include "StreamlineScopedCaps.h"
#include <optional>
#include <utility>

namespace Neurotic::Hooks
{
enum class StreamlineCapsTarget {Game,Local};
// Architecture zero is the existing game-side unavailable-replacement request.
// It must never zero a separate local publication or silently target a cached
// game object from a local callback. Other requested overrides retain policy.
inline std::optional<std::uint32_t> StreamlineArchitectureForTarget(
    std::optional<std::uint32_t> architecture,StreamlineCapsTarget target) noexcept
{
    if(target==StreamlineCapsTarget::Local&&architecture&&*architecture==0)return std::nullopt;
    return architecture;
}
enum class ManagedCapsStatus
{
    MissingParameters,MissingPublication,NullPublication,QueryException,
    InvalidCount,PolicyException,Unchanged,Applied
};
template<class Caps>
struct StreamlineCapsOverride
{
    std::uint32_t architecture;
    decltype(std::declval<Caps&>().adapters[0].vendor) vendor;
    bool rewriteDriver;
};

// Call with an empty scope at the existing managed OnLoad boundary. Query the
// ACTUAL current pointer through the supplied interface every time. The key and
// Caps type come from that caller's declared SDK contract, never DLL identity,
// consumer JSON version or a retained pointer. Neither getter nor this helper
// transfers ownership: the normal SDK callback supplies the finite borrow.
template<class Caps,class Parameters,class Resolve>
ManagedCapsStatus BeginManagedCaps(std::optional<StreamlineScopedCaps<Caps>>& scope,
                                  Parameters* parameters,const char* key,Resolve&& resolve)
{
    if(!parameters)return ManagedCapsStatus::MissingParameters;
    void* publication=nullptr;
    try
    {
        if(!parameters->get(key,&publication))return ManagedCapsStatus::MissingPublication;
    }
    catch(...){return ManagedCapsStatus::QueryException;}
    if(!publication)return ManagedCapsStatus::NullPublication;
    auto* caps=static_cast<Caps*>(publication);
    constexpr auto capacity=std::extent_v<decltype(std::declval<Caps&>().adapters)>;
    static_assert(capacity>0);
    if(static_cast<std::size_t>(caps->gpuCount)>capacity)return ManagedCapsStatus::InvalidCount;
    std::optional<StreamlineCapsOverride<Caps>> requested;
    try{requested=std::forward<Resolve>(resolve)(*caps);}
    catch(...){return ManagedCapsStatus::PolicyException;}
    if(!requested)return ManagedCapsStatus::Unchanged;
    scope.emplace(caps,requested->architecture,requested->vendor,requested->rewriteDriver);
    return scope->Applied()?ManagedCapsStatus::Applied:ManagedCapsStatus::InvalidCount;
}
}
