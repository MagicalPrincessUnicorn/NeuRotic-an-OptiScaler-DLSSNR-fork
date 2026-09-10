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
            Reset();
        if (!_clicks.empty() && nowSeconds - _clicks.back() > 2.0)
            Reset();
        while (!_clicks.empty() && nowSeconds - _clicks.front() > 2.0)
            _clicks.pop_front();
        _clicks.push_back(nowSeconds);
        if (!_burstActive)
        {
            if (_clicks.size() < 4)
                return std::nullopt;
            _burstActive = true;
            _togglesSinceMessage = 0;
        }
        else if (++_togglesSinceMessage < 2)
            return std::nullopt;
        else
            _togglesSinceMessage = 0;

        std::size_t candidate = Next() % messageCount;
        if (messageCount > 1 && _last && candidate == *_last)
            candidate = (candidate + 1 + Next() % (messageCount - 1)) % messageCount;
        _last = candidate;
        return candidate;
    }

  private:
    void Reset()
    {
        _clicks.clear();
        _burstActive = false;
        _togglesSinceMessage = 0;
    }

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
    std::size_t _togglesSinceMessage = 0;
    bool _burstActive = false;
};
} // namespace DlssNr
