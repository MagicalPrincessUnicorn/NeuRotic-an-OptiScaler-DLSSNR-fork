// The runner includes the actual production retirement gate and rebuild branch.
// Fake completion lets this exercise pending/replayable work without a game/provider.
#include <cstdio>
#include <memory>
#include <vector>

using DXGI_FORMAT = int;
struct Resource {
    struct Desc { DXGI_FORMAT Format = 1; };
    Desc GetDesc() const { return {}; }
};
using ID3D12Resource = Resource;
Resource oldOutput;
struct State {
    void* feature = reinterpret_cast<void*>(1);
    void* resumeFeatureAwaitingRelease = nullptr;
    void* preSrScratch = nullptr;
    void* preSrRejitter = nullptr;
    bool preSrScratchPrimed = true, preSrAwaitingEvaluation = false;
    Resource* output = &oldOutput;
    bool reset = false;
    unsigned workWidth = 3840, workHeight = 2160;
} g_nr;
unsigned g_featureRebuilds = 0, creations = 0, evaluations = 0, parked = 0, skips = 0;
bool sealed = false, completed = false, releaseSucceeds = true;
void* retired = nullptr;
void ReportSkipOnce(const char*) { ++skips; }
void ParkNrFeature(void*& feature) {
    if (!feature) return;
    retired = feature; feature = nullptr; ++parked;
}
void ParkNrResource(void*& resource) { resource = nullptr; }
void ParkAllAdditionalLayerFeatures(const char*) {}
void ForgetCalibration() {}
#define LOG_INFO(...) ((void)0)
#include "primary-format.inc"
struct Scratch { template<class F> void Commit(F) {} };
struct Device { void Release() {} };
void PollRelease() {
    if (retired && sealed && completed && releaseSucceeds) {
        if (g_nr.resumeFeatureAwaitingRelease == retired)
            g_nr.resumeFeatureAwaitingRelease = nullptr;
        retired = nullptr;
    }
}
void Dispatch(bool resolutionChanged, bool tuningChanged, bool frameDimensionsChanged,
              unsigned workWidth, unsigned workHeight, bool formatChanged = false) {
    PollRelease();
#include "primary-gate.inc"
    Device instance;
    auto* device = &instance;
    void* target = reinterpret_cast<void*>(3);
    Scratch scratch;
    std::unique_ptr<Scratch> layer2Scratch;
    std::vector<std::unique_ptr<Scratch>> laterScratch;
    if (formatChanged) ReleaseSurfacesIfFormatChanged(2, nullptr);
#include "primary-rebuild.inc"
    // Fake vendor boundary: a creation frame never evaluates the new feature.
    if (!g_nr.feature) { ++creations; g_nr.feature = reinterpret_cast<void*>(2); return; }
    ++evaluations;
}
bool Require(bool value, const char* reason) {
    if (!value) std::fprintf(stderr, "FAIL: %s\n", reason);
    return value;
}
int main() {
    for (int change = 0; change != 4; ++change) {
        g_nr = {}; creations = evaluations = parked = skips = g_featureRebuilds = 0;
        sealed = completed = false; releaseSucceeds = true; retired = nullptr;
        Dispatch(change == 0 || change == 2, change == 1, change == 2, 960, 540, change == 3);
        if (!Require(parked == 1 && retired != nullptr &&
                     g_nr.resumeFeatureAwaitingRelease == retired && creations == 0 && evaluations == 0,
                     "resolution/tuning/format rebuild must retain the old generation gate before replacement")) return 1;
        if (change == 3) {
            ReleaseSurfacesIfFormatChanged(2, nullptr);
            if (!Require(g_nr.resumeFeatureAwaitingRelease == retired && parked == 1,
                         "format changes with no current feature must preserve the pending old handle")) return 1;
        }
        // Slider changes while old GPU work is pending cannot create a succession of models.
        for (int i = 0; i != 100; ++i) Dispatch(true, true, false, 2272 + i * 8, 1280);
        if (!Require(parked == 1 && creations == 0 && evaluations == 0,
                     "pending retirement must bypass further changes")) return 1;
        completed = true;
        Dispatch(true, false, false, 2272, 1280);
        if (!Require(creations == 0, "completed but replayable work must keep creation blocked")) return 1;
        completed = false; sealed = true;
        Dispatch(true, false, false, 2272, 1280);
        if (!Require(creations == 0, "sealed but incomplete GPU work must keep creation blocked")) return 1;
        completed = true; releaseSucceeds = false;
        Dispatch(true, false, false, 2272, 1280);
        if (!Require(creations == 0, "failed vendor release must keep creation blocked")) return 1;
        releaseSucceeds = true;
        Dispatch(true, false, false, 2272, 1280);
        if (!Require(creations == 1 && evaluations == 0 && g_nr.workWidth == 2272,
                     "successful release permits one replacement with the latest requested dimensions")) return 1;
        Dispatch(false, false, false, 2272, 1280);
        if (!Require(creations == 1 && evaluations == 1, "evaluation resumes after the creation frame")) return 1;
    }
    g_nr = {}; creations = evaluations = 0; retired = nullptr;
    ReleaseSurfacesIfFormatChanged(1, nullptr);
    Dispatch(false, false, false, 3840, 2160);
    if (!Require(creations == 0 && evaluations == 1, "unchanged signatures continue evaluating")) return 1;
    std::puts("PASS primary rebuild: resolution/tuning/frame size/format, pending changes, replay, completion, failed release, recovery");
}
