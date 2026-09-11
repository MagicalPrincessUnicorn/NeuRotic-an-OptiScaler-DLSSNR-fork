#include "../OptiScaler/dlssnr/NrPendingEdit.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x << '\n'; std::abort(); } } while (false)
using DlssNr::NrPendingEdit;
int main()
{
    std::array<NrOptional<float>, 10> layers { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
    for (const unsigned count : {2u, 4u, 10u})
    {
        for (auto& layer : layers) layer = 1.f;
        std::vector<NrOptional<float>*> targets;
        for (unsigned i = 1; i < count; ++i) targets.push_back(&layers[i]);
        NrPendingEdit edit;
        edit.Prepare(targets, 1);
        CHECK(!edit.Mixed());
        edit.Preview(.4f);
        for (auto& layer : layers) CHECK(layer.value_or_default() == 1.f);
        edit.Prepare(targets, 2);
        CHECK(edit.Value() == .4f);
        CHECK(edit.Commit(0.f, 2.f));
        for (unsigned i = 0; i < 10; ++i) CHECK(layers[i].value_or_default() == ((i > 0 && i < count) ? .4f : 1.f));
        for (unsigned i = 0; i < 10; ++i) {
            NrOptional<float> reloaded { 1.f };
            reloaded.set_from_config(layers[i].value_for_config());
            CHECK(reloaded.value_or_default() == layers[i].value_or_default());
        }
        edit.Finish(false);
        if (count > 2) { layers[2] = .7f; CHECK(edit.Mixed()); }
        edit.Prepare(targets, 3); edit.Preview(3.f); CHECK(edit.Commit(0.f, 2.f));
        CHECK(!edit.Mixed()); CHECK(layers[1].value_or_default() == 2.f);
        edit.Prepare(targets, 4); edit.Preview(-1.f); CHECK(edit.Commit(0.f, 2.f));
        CHECK(layers[1].value_or_default() == 0.f);
        edit.Prepare(targets, 5); edit.Preview(.8f); edit.Reset(1.f);
        CHECK(!edit.Commit(0.f, 2.f)); CHECK(layers[1].value_or_default() == 1.f);
        edit.Finish(false);
        // Panel/tab/window omitted for a frame cancels the old gesture.
        edit.Prepare(targets, 6); edit.Preview(.6f); edit.Prepare(targets, 8);
        CHECK(!edit.Commit(0.f, 2.f)); edit.Finish(false);
        // External value replacement/copy cannot be overwritten by a pending preview.
        edit.Prepare(targets, 9); edit.Preview(.6f); layers[1] = .9f;
        CHECK(!edit.Commit(0.f, 2.f)); CHECK(layers[1].value_or_default() == .9f); edit.Finish(false);
        // Same-valued INI reload also invalidates the edit.
        edit.Prepare(targets, 10); edit.Preview(.6f); NrConfigSynchronization::InvalidateProfileEdits();
        CHECK(!edit.Commit(0.f, 2.f)); edit.Finish(false);
        edit.Prepare(targets, 11); edit.Preview(.6f);
        edit.Prepare({&layers[0]}, 12); CHECK(!edit.Commit(0.f, 2.f)); CHECK(layers[0].value_or_default() == 1.f);
        edit.Finish(false);
        // A cancelled control can accept a new gesture; unchanged commits do not dirty options.
        edit.Prepare(targets, 13); edit.Preview(1.f); edit.Commit(0.f, 2.f); edit.Finish(false);
        edit.Prepare(targets, 14); edit.Preview(1.f); CHECK(!edit.Commit(0.f, 2.f));
    }
    // Rendering snapshots cannot see partially updated child sets.
    std::vector<NrOptional<float>*> targets;
    for (unsigned i = 1; i < 10; ++i) targets.push_back(&layers[i]);
    for (auto* target : targets) *target = 1.f;
    std::atomic<bool> stop = false;
    std::thread reader([&] {
        while (!stop.load()) {
            NrConfigSynchronization::Transaction transaction;
            float first = targets.front()->value_or_default();
            for (auto* target : targets) CHECK(target->value_or_default() == first);
        }
    });
    NrPendingEdit edit;
    for (int i = 0; i < 2000; ++i) {
        edit.Prepare(targets, i); edit.Preview(i % 2 ? .5f : 1.f); edit.Commit(0.f, 2.f); edit.Finish(false);
    }
    stop = true; reader.join();
    std::cout << "PASS shared preview/commit, 2/4/10 scope, mixed values, reset/copy/reload/closure cancellation, bounds, unchanged values and atomic snapshots\n";
}
