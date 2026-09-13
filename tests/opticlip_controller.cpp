#include "menu/OptiClipController.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

static void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using namespace OptiClip;
    Controller controller(0x12345678u);
    controller.SetEnabled(true, 0.0);
    controller.Tick(0.0, true);
    Check(controller.Greeted() && controller.Active(0.0) != nullptr, "first eligible display greets once");
    Check(!controller.Report(Event::Repository, 1.0), "lower-priority event respects cooldown");
    Check(controller.Report(Event::Multipass, 1.0), "higher-priority event replaces active greeting");
    Check(controller.Active(1.0)->event == Event::Multipass, "one active message has no stale backlog");
    Check(controller.Report(Event::RapidToggle, 1.1, "toggle"), "rapid toggle replaces multipass");
    controller.Dismiss(2.0);
    Check(controller.Active(2.0) == nullptr, "dismiss closes current message");
    Check(!controller.Report(Event::Strength, 7.5), "cooldown outlives an expired seven-second bubble");
    Check(RouteRapidToggle(true, true) == RapidToggleDestination::Bubble,
          "visible enabled advisor routes qualifying burst to bubble");
    Check(RouteRapidToggle(true, false) == RapidToggleDestination::Quiet,
          "visible disabled advisor keeps qualifying burst quiet");
    Check(RouteRapidToggle(false, false) == RapidToggleDestination::Toast,
          "hidden menu retains toast even when advisor is disabled");
    Check(EffectiveMultipass(false, false, 8, 4) == 0,
          "inactive Basic profile contributes no effective passes");
    Check(EffectiveMultipass(true, false, 8, 4) == 4,
          "Basic mode derives its active count from cumulative totals");
    Check(EffectiveMultipass(true, true, 8, 4) == 8,
          "Advanced mode uses its explicit active count");

    Check(!controller.ObserveThreshold("resolution", Event::Resolution, 1.0, 1.0, false, 10.0),
          "normal threshold initializes quietly");
    Check(controller.ObserveThreshold("resolution", Event::Resolution, 1.1, 1.0, true, 10.0),
          "committed upward crossing reacts");
    Check(!controller.ObserveThreshold("resolution", Event::Resolution, 1.2, 1.0, true, 20.0),
          "remaining above does not repeat");
    Check(!controller.ObserveThreshold("resolution", Event::Resolution, 1.0, 1.0, true, 21.0),
          "return to normal rearms quietly");
    Check(controller.ObserveThreshold("resolution", Event::Resolution, 1.2, 1.0, true, 30.0),
          "rearmed control reacts on a later crossing");

    Check(!controller.ObserveThreshold("multipass.advanced", Event::Multipass, 2.0, 2.0, false, 40.0),
          "two passes initializes quietly");
    Check(controller.ObserveThreshold("multipass.advanced", Event::Multipass, 3.0, 2.0, true, 40.0),
          "advanced count reacts at three passes");
    Check(!controller.ObserveThreshold("multipass.basic", Event::Multipass, 1.0, 2.0, false, 50.0),
          "basic count initializes independently");
    Check(controller.ObserveThreshold("multipass.basic", Event::Multipass, 4.0, 2.0, true, 50.0),
          "enabled preconfigured basic count reacts");

    controller.SetEnabled(false, 51.0);
    Check(controller.Active(51.0) == nullptr && !controller.Report(Event::RapidToggle, 51.0, "quiet"),
          "disabled advisor closes and suppresses reactions");
    controller.SetEnabled(true, 500.0);
    controller.Tick(500.0, true);
    Check(controller.Active(500.0) == nullptr,
          "re-enabling after a long disabled interval restarts quiet timing");

    Controller idle(7u);
    idle.SetEnabled(true, 0.0);
    idle.Tick(0.0, true);
    idle.Tick(7.1, true);
    Check(idle.Active(7.1) == nullptr, "greeting expires after seven seconds");
    idle.Tick(60.0, true);
    Check(idle.Active(60.0) != nullptr, "quiet menu emits first support message after sixty seconds");
    idle.Tick(367.1, true);
    Check(idle.RepositoryShown() && idle.CoffeeShown(), "five-minute spacing permits the other support type");
    idle.Tick(700.0, true);
    Check(idle.Active(700.0) == nullptr, "repository and coffee each appear at most once per process");

    // Expanded pools keep the same bounds/cooldowns, separate resolution from
    // strength jokes and never repeat a line immediately.
    for (Event event : {Event::Multipass, Event::Resolution, Event::Strength, Event::Repository, Event::Coffee})
    {
        Controller quips(123u);
        std::string previous;
        std::set<std::string> observed;
        for (int i = 0; i < 200; ++i)
        {
            const double now = i * 9.0;
            Check(quips.Report(event, now), "quips remain eligible after cooldown");
            const std::string line = quips.Active(now)->text;
            Check(!line.empty() && line != previous, "expanded pools avoid immediate repeats");
            if (event == Event::Strength)
                Check(line.find("resolution") == std::string::npos && line.find("native") == std::string::npos,
                      "strength reaction never misidentifies a resolution change");
            previous = line;
            observed.insert(line);
        }
        Check(observed.size() >= 4, "every expanded category offers several quips");
    }
    std::cout << "PASS: OptiClip priority, cooldown, thresholds, dismissal, greeting, idle caps and expanded quip pools\n";
}
