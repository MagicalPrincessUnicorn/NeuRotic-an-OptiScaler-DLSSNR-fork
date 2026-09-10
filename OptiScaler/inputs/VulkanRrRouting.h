#pragma once

#include <cstdint>
#include <unordered_map>

namespace VulkanRrRouting
{
constexpr uint32_t UnknownViewport = UINT32_MAX;

struct Extent
{
    uint32_t width = 0;
    uint32_t height = 0;

    bool known() const { return width != 0 && height != 0; }
    bool operator==(const Extent& other) const { return width == other.width && height == other.height; }
    bool operator!=(const Extent& other) const { return !(*this == other); }
};

enum class BypassReason
{
    None,
    Uncorrelated,
    RouteMissing,
    ViewportMismatch,
    UnknownPresentation,
    MissingDimensions,
    NoMatch,
    Ambiguous,
    Auxiliary,
    Duplicate,
    ContractChanged,
};

inline const char* ReasonName(BypassReason reason)
{
    switch (reason)
    {
    case BypassReason::None: return "selected";
    case BypassReason::Uncorrelated: return "uncorrelated";
    case BypassReason::RouteMissing: return "route-missing";
    case BypassReason::ViewportMismatch: return "viewport-mismatch";
    case BypassReason::UnknownPresentation: return "presentation-unknown";
    case BypassReason::MissingDimensions: return "dimensions-unknown";
    case BypassReason::NoMatch: return "no-route-match";
    case BypassReason::Ambiguous: return "ambiguous-route-match";
    case BypassReason::Auxiliary: return "auxiliary-route";
    case BypassReason::Duplicate: return "duplicate-frame-route";
    case BypassReason::ContractChanged: return "physical-contract-changed";
    }
    return "unknown";
}

struct Decision
{
    bool execute = false;
    bool resetHistory = false;
    bool log = false;
    BypassReason reason = BypassReason::RouteMissing;
    uint64_t selectedHandle = 0;
    uint64_t executionCount = 0;
};

class Registry
{
  private:
    struct Route
    {
        uintptr_t device = 0;
        uint32_t viewport = UnknownViewport;
        Extent declared {};
        Extent observed {};
        uint64_t generation = 0;
        bool live = false;
        BypassReason lastReason = BypassReason::RouteMissing;
        bool haveLastReason = false;
    };

    std::unordered_map<uint64_t, Route> _routes;
    uint64_t _generation = 0;
    uint64_t _selectedHandle = 0;
    bool _contractLocked = false;
    bool _contractChanged = false;
    bool _resetOnReselection = false;
    uintptr_t _contractDevice = 0;
    Extent _contractExtent {};
    uint64_t _lastFrame = UINT64_MAX;
    uint32_t _lastViewport = UnknownViewport;
    uint64_t _lastHandle = 0;
    uint64_t _executionCount = 0;

    Decision finish(Route& route, Decision decision)
    {
        decision.log = !route.haveLastReason || route.lastReason != decision.reason;
        route.haveLastReason = true;
        route.lastReason = decision.reason;
        decision.executionCount = _executionCount;
        return decision;
    }

  public:
    void Register(uint64_t handle, uintptr_t device, uint32_t viewport, Extent declared)
    {
        Route route {};
        route.device = device;
        route.viewport = viewport;
        route.declared = declared;
        route.generation = ++_generation;
        route.live = true;
        _routes[handle] = route;
    }

    bool Observe(uint64_t handle, uint32_t viewport, Extent observed)
    {
        auto it = _routes.find(handle);
        if (it == _routes.end() || !it->second.live || viewport == UnknownViewport ||
            it->second.viewport != viewport)
            return false;
        it->second.observed = observed;
        return true;
    }

    bool Contains(uint64_t handle) const
    {
        const auto it = _routes.find(handle);
        return it != _routes.end() && it->second.live;
    }

    Decision Select(uint64_t handle, uintptr_t device, uint32_t viewport, uint64_t frame, Extent presentation)
    {
        auto it = _routes.find(handle);
        if (it == _routes.end() || !it->second.live)
            return { false, false, false, BypassReason::RouteMissing };

        Route& current = it->second;
        if (viewport == UnknownViewport)
            return finish(current, { false, false, false, BypassReason::Uncorrelated });
        if (current.viewport != viewport)
            return finish(current, { false, false, false, BypassReason::ViewportMismatch });
        if (!presentation.known())
            return finish(current, { false, false, false, BypassReason::UnknownPresentation });
        if (!current.declared.known() || !current.observed.known())
            return finish(current, { false, false, false, BypassReason::MissingDimensions });
        if (_contractLocked && _selectedHandle == handle &&
            (_contractDevice != device || _contractExtent != presentation || _contractExtent != current.observed))
        {
            _contractChanged = true;
            return finish(current, { false, false, false, BypassReason::ContractChanged, _selectedHandle });
        }

        uint64_t matchedHandle = 0;
        uint32_t matchCount = 0;
        for (const auto& [candidateHandle, route] : _routes)
        {
            if (route.live && route.device == device && route.viewport != UnknownViewport &&
                route.declared == presentation && route.observed == presentation)
            {
                matchedHandle = candidateHandle;
                ++matchCount;
            }
        }

        if (matchCount == 0)
            return finish(current, { false, false, false, BypassReason::NoMatch });
        if (matchCount != 1)
            return finish(current, { false, false, false, BypassReason::Ambiguous });
        if (matchedHandle != handle)
            return finish(current, { false, false, false, BypassReason::Auxiliary, matchedHandle });

        if (_contractChanged)
            return finish(current, { false, false, false, BypassReason::ContractChanged, matchedHandle });
        if (_contractLocked && (_contractDevice != device || _contractExtent != presentation))
        {
            _contractChanged = true;
            return finish(current, { false, false, false, BypassReason::ContractChanged, matchedHandle });
        }
        if (_lastHandle == handle && _lastViewport == viewport && _lastFrame == frame)
            return finish(current, { false, false, false, BypassReason::Duplicate, matchedHandle });

        const bool resetHistory = _resetOnReselection && _contractLocked && _contractDevice == device &&
                                  _contractExtent == presentation;
        _contractLocked = true;
        _contractDevice = device;
        _contractExtent = presentation;
        _selectedHandle = handle;
        _resetOnReselection = false;
        _lastHandle = handle;
        _lastViewport = viewport;
        _lastFrame = frame;
        ++_executionCount;
        auto result = finish(current, { true, resetHistory, false, BypassReason::None, matchedHandle });
        result.log = result.log || _executionCount == 1 || (_executionCount % 300) == 0;
        result.executionCount = _executionCount;
        return result;
    }

    void Release(uint64_t handle)
    {
        const auto it = _routes.find(handle);
        if (it == _routes.end()) return;
        it->second.live = false;
        if (_selectedHandle == handle)
        {
            _selectedHandle = 0;
            _resetOnReselection = true;
        }
    }

    void ResetLifecycle() { *this = Registry {}; }
};
} // namespace VulkanRrRouting
