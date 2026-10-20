#include "nr/diagnostics/candidate/Observer.h"
#include <windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace DlssNr::CandidateObserver;

// CPU-only experiment: exercise periodic publication without any creation hook.
// The game failures contain only initial coverage, so immediate-close fixtures
// do not exercise this part of the worker's lifetime.
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto destination = std::filesystem::absolute(argv[1]);
    assert(!std::filesystem::exists(destination));
    std::filesystem::create_directories(destination);
    SetEnvironmentVariableA("NEUROTIC_CANDIDATE_OBSERVER", "1");
    SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION", "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    SetEnvironmentVariableW(L"NEUROTIC_DIAGNOSTIC_DIRECTORY", destination.c_str());
    StartCold(false);
    const auto start = GetTickCount64();
    Snapshot snapshot{};
    uint64_t lastElapsed = 0;
    uint32_t observedUpdates = 0;
    while (GetTickCount64() - start < 6000) {
        const auto result = TryRead(snapshot);
        if (result == ReadResult::Snapshot) {
            assert(snapshot.count == 0 && snapshot.coverage.complete);
            assert(snapshot.coverage.enqueued == 1 && snapshot.coverage.dropped == 0);
            assert(snapshot.elapsedMs >= lastElapsed);
            if (snapshot.elapsedMs > lastElapsed) ++observedUpdates;
            lastElapsed = snapshot.elapsedMs;
        } else {
            assert(result == ReadResult::Unavailable || result == ReadResult::Contended);
        }
        Sleep(10);
    }
    assert(Enabled() && observedUpdates >= 15 && lastElapsed >= 5000);
    RequestClose(Reason::Requested);
    const auto trace = destination / L"candidate-eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee" / L"trace.jsonl";
    bool footer = false;
    for (unsigned i = 0; i < 200 && !footer; ++i) {
        std::ifstream input(trace);
        std::string line;
        while (std::getline(input, line))
            if (line.find("\"type\":\"footer\"") != std::string::npos &&
                line.find("\"complete_transport\":true") != std::string::npos) footer = true;
        Sleep(10);
    }
    assert(footer && TryRead(snapshot) == ReadResult::Closed && snapshot.count == 0);
    std::cout << "PASS: optimized idle observer, " << observedUpdates
              << " periodic snapshots, complete footer; no game or GPU calls\n";
}
