#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace DlssNr::PresentPacing
{
enum class Route : unsigned int
{
    NativeTemporal,
    PresentImageOnly,
    PresentEnhanced
};

struct MetricSummary
{
    std::size_t samples = 0;
    double average = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double maximum = 0.0;
};

struct WindowSummary
{
    bool valid = false;
    Route route = Route::NativeTemporal;
    std::uint64_t serial = 0;
    std::uint64_t firstCall = 0;
    std::uint64_t lastCall = 0;
    std::uint64_t warmupDiscarded = 0;
    std::uint64_t failedPresents = 0;
    std::uint64_t expectedGpuSamples = 0;
    std::uint64_t missingGpuSamples = 0;
    unsigned int pendingSlotsHighWater = 0;
    MetricSummary frameInterval;
    MetricSummary adapterCpu;
    MetricSummary hookCpu;
    MetricSummary originalPresentCpu;
    MetricSummary presentGpu;
    MetricSummary completionObservation;
    MetricSummary fenceAge;
};

struct CallToken
{
    Route route = Route::NativeTemporal;
    std::uint64_t serial = 0;
    std::uint64_t call = 0;
    bool eligible = false;
};

struct CallSample
{
    double frameIntervalMs = 0.0;
    double adapterCpuMs = 0.0;
    double hookCpuMs = 0.0;
    double originalPresentCpuMs = 0.0;
    bool presentFailed = false;
};

template <std::size_t Capacity>
class MetricWindow
{
    std::array<double, Capacity> _values {};
    std::size_t _count = 0;

  public:
    bool add(double value)
    {
        if (!std::isfinite(value) || value < 0.0 || _count == Capacity)
            return false;
        _values[_count++] = value;
        return true;
    }

    void clear() { _count = 0; }
    std::size_t size() const { return _count; }
    bool full() const { return _count == Capacity; }

    MetricSummary summary() const
    {
        MetricSummary result {};
        result.samples = _count;
        if (_count == 0)
            return result;

        std::array<double, Capacity> sorted {};
        double total = 0.0;
        for (std::size_t i = 0; i < _count; ++i)
        {
            sorted[i] = _values[i];
            total += _values[i];
        }
        std::sort(sorted.begin(), sorted.begin() + _count);

        result.average = total / static_cast<double>(_count);
        result.maximum = sorted[_count - 1];
        result.median = (_count & 1u) != 0
                            ? sorted[_count / 2]
                            : (sorted[_count / 2 - 1] + sorted[_count / 2]) * 0.5;
        const auto p95Index = static_cast<std::size_t>(
            std::ceil(static_cast<double>(_count) * 0.95)) - 1;
        result.p95 = sorted[std::min(p95Index, _count - 1)];
        return result;
    }
};

template <std::size_t Capacity = 2048, unsigned int WarmupCalls = 32>
class Window
{
    Route _route = Route::NativeTemporal;
    bool _active = false;
    std::uint64_t _serial = 0;
    std::uint64_t _firstCall = 0;
    std::uint64_t _lastCall = 0;
    unsigned int _warmupRemaining = 0;
    std::uint64_t _warmupDiscarded = 0;
    std::uint64_t _failedPresents = 0;
    std::uint64_t _expectedGpuSamples = 0;
    unsigned int _pendingSlotsHighWater = 0;
    MetricWindow<Capacity> _frameInterval;
    MetricWindow<Capacity> _adapterCpu;
    MetricWindow<Capacity> _hookCpu;
    MetricWindow<Capacity> _originalPresentCpu;
    MetricWindow<Capacity> _presentGpu;
    MetricWindow<Capacity> _completionObservation;
    MetricWindow<Capacity> _fenceAge;

    void reset(Route route, bool warmup)
    {
        _route = route;
        _active = true;
        ++_serial;
        _firstCall = 0;
        _lastCall = 0;
        _warmupRemaining = warmup ? WarmupCalls : 0;
        _warmupDiscarded = 0;
        _failedPresents = 0;
        _expectedGpuSamples = 0;
        _pendingSlotsHighWater = 0;
        _frameInterval.clear();
        _adapterCpu.clear();
        _hookCpu.clear();
        _originalPresentCpu.clear();
        _presentGpu.clear();
        _completionObservation.clear();
        _fenceAge.clear();
    }

  public:
    std::optional<WindowSummary> beginCall(Route route, std::uint64_t call, CallToken& token)
    {
        std::optional<WindowSummary> completed;
        if (!_active)
            reset(route, true);
        else if (_route != route)
        {
            completed = summary();
            reset(route, true);
        }
        else if (_frameInterval.full())
        {
            completed = summary();
            reset(route, false);
        }

        token.route = route;
        token.serial = _serial;
        token.call = call;
        if (_warmupRemaining != 0)
        {
            --_warmupRemaining;
            ++_warmupDiscarded;
            token.eligible = false;
        }
        else
        {
            token.eligible = true;
            if (_firstCall == 0)
                _firstCall = call;
            _lastCall = call;
        }
        return completed;
    }

    bool recordCall(const CallToken& token, const CallSample& sample)
    {
        if (!token.eligible || !_active || token.serial != _serial || token.route != _route)
            return false;
        if (!std::isfinite(sample.frameIntervalMs) || sample.frameIntervalMs < 0.0 ||
            !std::isfinite(sample.adapterCpuMs) || sample.adapterCpuMs < 0.0 ||
            !std::isfinite(sample.hookCpuMs) || sample.hookCpuMs < 0.0 ||
            !std::isfinite(sample.originalPresentCpuMs) || sample.originalPresentCpuMs < 0.0)
            return false;
        const bool recorded = _frameInterval.add(sample.frameIntervalMs);
        _adapterCpu.add(sample.adapterCpuMs);
        _hookCpu.add(sample.hookCpuMs);
        _originalPresentCpu.add(sample.originalPresentCpuMs);
        if (sample.presentFailed)
            ++_failedPresents;
        return recorded;
    }

    bool expectGpu(const CallToken& token)
    {
        if (!token.eligible || !_active || token.serial != _serial ||
            token.route == Route::NativeTemporal || token.route != _route)
            return false;
        ++_expectedGpuSamples;
        return true;
    }

    bool recordGpu(const CallToken& token, double gpuMs, double completionObservationMs,
                   std::uint64_t fenceAge)
    {
        if (!token.eligible || !_active || token.serial != _serial ||
            token.route == Route::NativeTemporal || token.route != _route)
            return false;
        if (!std::isfinite(gpuMs) || gpuMs < 0.0 ||
            !std::isfinite(completionObservationMs) || completionObservationMs < 0.0)
            return false;
        const bool recorded = _presentGpu.add(gpuMs);
        _completionObservation.add(completionObservationMs);
        _fenceAge.add(static_cast<double>(fenceAge));
        return recorded;
    }

    void observePending(const CallToken& token, unsigned int pendingSlots)
    {
        if (token.eligible && _active && token.serial == _serial && token.route == _route)
            _pendingSlotsHighWater = std::max(_pendingSlotsHighWater, pendingSlots);
    }

    WindowSummary summary() const
    {
        WindowSummary result {};
        result.valid = _active && (_frameInterval.size() != 0 || _warmupDiscarded != 0);
        result.route = _route;
        result.serial = _serial;
        result.firstCall = _firstCall;
        result.lastCall = _lastCall;
        result.warmupDiscarded = _warmupDiscarded;
        result.failedPresents = _failedPresents;
        result.expectedGpuSamples = _expectedGpuSamples;
        result.missingGpuSamples = _expectedGpuSamples > _presentGpu.size()
                                       ? _expectedGpuSamples - _presentGpu.size()
                                       : 0;
        result.pendingSlotsHighWater = _pendingSlotsHighWater;
        result.frameInterval = _frameInterval.summary();
        result.adapterCpu = _adapterCpu.summary();
        result.hookCpu = _hookCpu.summary();
        result.originalPresentCpu = _originalPresentCpu.summary();
        result.presentGpu = _presentGpu.summary();
        result.completionObservation = _completionObservation.summary();
        result.fenceAge = _fenceAge.summary();
        return result;
    }
};
} // namespace DlssNr::PresentPacing
