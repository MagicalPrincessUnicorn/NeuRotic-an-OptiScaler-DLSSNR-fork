#include "SynchronizedOptional.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool value, const char* reason)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", reason); std::exit(1); }
}

int main()
{
    NrOptional<int32_t, NoDefault> arrival;
    arrival = 8;
    bool legacyThrew = false;
    if (arrival.has_value())
    {
        arrival.reset(); // Deterministic menu reset between the two old reads.
        try { (void)arrival.value(); }
        catch (const std::bad_optional_access&) { legacyThrew = true; }
    }
    Check(legacyThrew, "legacy split reads expose reset as bad_optional_access");

    arrival = 8;
    const auto legacyEntry = arrival.value();
    arrival = 64; // Deterministic adapter or menu edit during evaluation.
    Check(legacyEntry != arrival.value(), "legacy restore observes a different arrival state");

    arrival = 8;
    const auto captured = arrival.snapshot();
    arrival.reset();
    Check(captured.has_value() && captured.value() == 8, "copied entry survives menu reset");
    arrival = 64;
    Check(captured.has_value() && captured.value() == 8, "restore preserves original entry state after edit");

    arrival.reset();
    const auto absent = arrival.snapshot();
    arrival = 64;
    Check(!absent.has_value(), "absent entry remains absent for restore after edit");
    std::puts("PASS: legacy reset exception and restore mismatch reproduced; snapshot preserves entry/restore");
}
