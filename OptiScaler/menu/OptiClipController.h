#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace OptiClip
{
enum class Event : std::uint8_t
{
    None,
    Greeting,
    RapidToggle,
    Multipass,
    Resolution,
    Strength,
    Repository,
    Coffee
};

struct Message
{
    Event event = Event::Greeting;
    const char* text = "";
    double expiresAt = 0.0;
};

enum class RapidToggleDestination : std::uint8_t { Quiet, Bubble, Toast };

constexpr RapidToggleDestination RouteRapidToggle(bool menuVisible, bool advisorEnabled)
{
    if (!menuVisible) return RapidToggleDestination::Toast;
    return advisorEnabled ? RapidToggleDestination::Bubble : RapidToggleDestination::Quiet;
}

constexpr unsigned int EffectiveMultipass(bool enabled, bool advanced,
                                          unsigned int advancedCount, unsigned int basicCount)
{
    if (!enabled) return 0;
    return advanced ? advancedCount : basicCount;
}

class Controller
{
  public:
    explicit Controller(std::uint32_t seed = 0x4f505449u) : _random(seed != 0 ? seed : 1) {}

    void SetEnabled(bool enabled, double now)
    {
        if (_enabled != enabled)
        {
            _active.reset();
            _quietSince = now;
        }
        _enabled = enabled;
        if (_quietSince < 0.0) _quietSince = now;
    }

    bool Enabled() const { return _enabled; }

    void Interaction(double now)
    {
        _quietSince = now;
    }

    void Dismiss(double now)
    {
        _active.reset();
        Interaction(now);
    }

    const Message* Active(double now)
    {
        if (_active && now >= _active->expiresAt) _active.reset();
        return _active ? &*_active : nullptr;
    }

    bool Report(Event event, double now, const char* suppliedText = nullptr)
    {
        if (!_enabled) return false;
        const int incoming = Priority(event);
        const int current = _active ? Priority(_active->event) : -1;
        if (_lastReactionAt >= 0.0 && now - _lastReactionAt < 8.0 &&
            (!_active || incoming <= current))
            return false;

        const char* text = suppliedText != nullptr ? suppliedText : Select(event);
        if (text == nullptr || text[0] == 0) return false;
        _active = Message { event, text, now + 7.0 };
        _lastReactionAt = now;
        _quietSince = now;
        return true;
    }

    bool ObserveThreshold(std::string_view identity, Event event, double value, double boundary,
                          bool committed, double now)
    {
        auto& state = _thresholds[std::string(identity)];
        const bool above = value > boundary;
        if (!state.initialized)
        {
            state.initialized = true;
            state.above = above;
            return false;
        }
        if (!committed) return false;
        const bool crossed = !state.above && above;
        state.above = above;
        return crossed ? Report(event, now) : false;
    }

    void Tick(double now, bool menuVisible)
    {
        Active(now);
        if (!_enabled || !menuVisible) return;
        if (_quietSince < 0.0) _quietSince = now;
        if (!_greeted)
        {
            _greeted = true;
            Report(Event::Greeting, now);
            return;
        }
        if (now - _quietSince < 60.0 || (_lastIdleAt >= 0.0 && now - _lastIdleAt < 300.0))
            return;

        Event idle = Event::Repository;
        if (_repositoryShown && _coffeeShown) return;
        if (_repositoryShown) idle = Event::Coffee;
        else if (!_coffeeShown && (Next() & 1u) != 0) idle = Event::Coffee;
        if (Report(idle, now))
        {
            _lastIdleAt = now;
            if (idle == Event::Repository) _repositoryShown = true;
            else _coffeeShown = true;
        }
    }

    bool Greeted() const { return _greeted; }
    bool RepositoryShown() const { return _repositoryShown; }
    bool CoffeeShown() const { return _coffeeShown; }

  private:
    struct ThresholdState
    {
        bool initialized = false;
        bool above = false;
    };

    static int Priority(Event event)
    {
        switch (event)
        {
        case Event::None: return 0;
        case Event::RapidToggle: return 5;
        case Event::Multipass: return 4;
        case Event::Resolution:
        case Event::Strength: return 3;
        case Event::Greeting: return 2;
        case Event::Repository:
        case Event::Coffee: return 1;
        }
        return 0;
    }

    std::uint32_t Next()
    {
        _random ^= _random << 13;
        _random ^= _random >> 17;
        _random ^= _random << 5;
        return _random;
    }

    template <std::size_t N> const char* Choose(Event event, const std::array<const char*, N>& pool)
    {
        auto& last = _lastChoice[static_cast<std::size_t>(event)];
        std::size_t selected = Next() % N;
        if (N > 1 && last && selected == *last)
            selected = (selected + 1 + Next() % (N - 1)) % N;
        last = selected;
        return pool[selected];
    }

    const char* Select(Event event)
    {
        static constexpr std::array<const char*, 10> multipass = {
            "Three passes? Oh, I see what kind of person you are.",
            "Multipass, eh? You seem the type.",
            "One pass was a suggestion, apparently.",
            "I'm a paperclip and even I think that's a lot of layers.",
            "At this point your GPU would like representation.",
            "Layers. Because apparently the first two were a rehearsal.",
            "You're not rendering an image. You're making lasagna.",
            "Another pass? The pixels haven't even unpacked.",
            "Subtlety has left the settings menu.",
            "Your pass count has ambitions."
        };
        static constexpr std::array<const char*, 9> strength = {
            "Nope. Still look like a paperclip.",
            "More strength. A timeless solution.",
            "You turned it up. The pixels have been informed.",
            "Restraint was considered and rejected.",
            "The slider has a normal range. An interesting historical detail.",
            "A little stronger. Famous last words in a settings menu.",
            "You've promoted seasoning to the main course.",
            "Tasteful? Debatable. Committed? Absolutely.",
            "Apparently the word 'maximum' sounded like encouragement."
        };
        static constexpr std::array<const char*, 8> resolution = {
            "Above native? Your GPU had plans, you know.",
            "That is a lot of resolution for one little paperclip.",
            "More pixels. Because the existing pixels seemed lonely.",
            "Supersampling: the scenic route to the same screen.",
            "You ordered extra pixels. Bold of you.",
            "The screen stayed the same size. Your ambitions did not.",
            "Small slider. Surprisingly large consequences.",
            "Native resolution was apparently just the opening offer."
        };
        static constexpr std::array<const char*, 4> repository = {
            "Please star the repo. I have engagement targets.",
            "A GitHub star costs less than another graphics card.",
            "Enjoying the experiment? The repository accepts stars.",
            "I file pixels. GitHub files stars. Everyone needs a hobby."
        };
        static constexpr std::array<const char*, 4> coffee = {
            "Please buy the developer a coffee. Apparently I am not payroll eligible.",
            "If this helped, the developer accepts coffee. I accept compliments.",
            "The coffee link is below. My expense account remains theoretical.",
            "You can fuel the developer with coffee. Please don't water the paperclip."
        };
        switch (event)
        {
        case Event::None: return nullptr;
        case Event::Greeting: return "Hi. I'm OptiClip. I watch sliders so you don't have to.";
        case Event::Multipass: return Choose(event, multipass);
        case Event::Resolution: return Choose(event, resolution);
        case Event::Strength: return Choose(event, strength);
        case Event::Repository: return Choose(event, repository);
        case Event::Coffee: return Choose(event, coffee);
        case Event::RapidToggle: return nullptr;
        }
        return nullptr;
    }

    bool _enabled = true;
    bool _greeted = false;
    bool _repositoryShown = false;
    bool _coffeeShown = false;
    double _lastReactionAt = -1.0;
    double _quietSince = -1.0;
    double _lastIdleAt = -1.0;
    std::uint32_t _random;
    std::optional<Message> _active;
    std::array<std::optional<std::size_t>, 8> _lastChoice {};
    std::unordered_map<std::string, ThresholdState> _thresholds;
};
} // namespace OptiClip
