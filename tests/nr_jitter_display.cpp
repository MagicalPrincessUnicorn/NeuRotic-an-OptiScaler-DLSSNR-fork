#include "dlssnr/NrJitterDisplay.h"
#include <cmath>
#include <iostream>
#include <string>

static int failures = 0;
static void Expect(const std::string& value, const char* expected, const char* description)
{
    if (value != expected) { ++failures; std::cerr << "FAIL: " << description << ": " << value << '\n'; }
}
static NrPreflightSignals::NativeInputs Input(float x, float y, unsigned long long sequence)
{
    NrPreflightSignals::NativeInputs input;
    input.Record(true, x, true, y, false, 0, false, 0, false, 0, false);
    input.observations = sequence;
    return input;
}
int main()
{
    using namespace DlssNr::JitterDisplay;
    constexpr auto first = "X -0.25, Y 0.125 (last Native DLSS input)";
    constexpr auto next = "X 0.5, Y -0.375 (last Native DLSS input)";
    Snapshot snapshot;
    Context context {1, 2, 0, 3, 4, 5, 6, 7, 8};
    Expect(snapshot.Update(context, Input(-.25f,.125f,1), true, 10), first, "first value appears immediately");
    // Catch accidental per-frame publication of either component before the boundary.
    for (int frame = 1; frame < 120; ++frame)
        Expect(snapshot.Update(context, Input(.5f,-.375f,1+frame), true, 10+frame/60.0), first,
               "frame changes retain the same display snapshot for two seconds");
    Expect(snapshot.Update(context, Input(.5f,-.375f,121), true, 12), next, "exact two-second boundary publishes latest pair");
    Expect(snapshot.Update(context, Input(-.25f,.125f,122), true, 12.01), next, "new interval retains full snapshot");
    auto missing = Input(0,0,123); missing.jitter.reset(); missing.jitterSupplied = false;
    Expect(snapshot.Update(context, missing, true, 12.02), "Not supplied on last Native DLSS input", "missing input clears held value immediately");
    Expect(snapshot.Update(context, Input(-.25f,.125f,124), true, 12.03), first, "recovery publishes immediately");
    Expect(snapshot.Update(context, Input(INFINITY,0,125), true, 12.04), "Supplied but incomplete or non-finite", "invalid input clears immediately");
    Expect(snapshot.Update(context, Input(-.25f,.125f,126), true, 12.05), first, "finite recovery restarts interval");
    Expect(snapshot.Update(context, Input(.5f,-.375f,127), false, 12.06), "No Native DLSS input observed", "closed lifecycle hides historical pair");
    Expect(snapshot.Update(context, Input(-.25f,.125f,128), true, 12.07), first, "new observation publishes immediately");
    Expect(snapshot.Update(context, Input(.5f,-.375f,1), true, 12.08), next, "sequence rewind clears previous session");
    Expect(snapshot.Update(context, Input(-.25f,.125f,2), true, 11), first, "clock rewind restarts display snapshot");
    // Every owner/selection/session boundary must publish the new value immediately.
    for (int boundary = 0; boundary < 9; ++boundary)
    {
        Snapshot isolated;
        isolated.Update(context, Input(-.25f,.125f,1), true, 20);
        auto changed = context;
        switch (boundary) {
        case 0: ++changed.owner; break; case 1: ++changed.api; break;
        case 2: ++changed.route; break; case 3: ++changed.profile; break;
        case 4: ++changed.lifecycle; break; case 5: ++changed.resource; break;
        case 6: ++changed.resets; break; case 7: ++changed.builds; break;
        case 8: ++changed.rebuilds; break;
        }
        Expect(isolated.Update(changed, Input(.5f,-.375f,2), true, 20.01), next,
               "owner/route/session boundary replaces old display immediately");
    }
    if (failures) return 1;
    std::cout << "PASS: jitter display cadence, exact boundary, unavailable/invalid recovery, owner/route/session and clock/sequence resets\n";
}
