#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace DlssNr
{
class ToggleBurstTracker
{
  public:
    explicit ToggleBurstTracker(std::uint32_t seed = DefaultSeed()) : _random(seed != 0 ? seed : 1) {}

    std::optional<std::size_t> Click(double nowSeconds, std::size_t messageCount)
    {
        if (messageCount == 0)
            return std::nullopt;
        if (!_clicks.empty() && nowSeconds < _clicks.back())
            _clicks.clear();
        while (!_clicks.empty() && nowSeconds - _clicks.front() > 2.0)
            _clicks.pop_front();
        _clicks.push_back(nowSeconds);
        if (_clicks.size() < 4 || (_clicks.size() & 1u) != 0)
            return std::nullopt;

        std::size_t candidate = Next() % messageCount;
        if (messageCount > 1 && _last && candidate == *_last)
            candidate = (candidate + 1 + Next() % (messageCount - 1)) % messageCount;
        _last = candidate;
        return candidate;
    }

  private:
    static std::uint32_t DefaultSeed()
    {
        return static_cast<std::uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }

    std::uint32_t Next()
    {
        _random ^= _random << 13;
        _random ^= _random >> 17;
        _random ^= _random << 5;
        return _random;
    }

    std::deque<double> _clicks;
    std::optional<std::size_t> _last;
    std::uint32_t _random;
};
} // namespace DlssNr
