#include "nr/diagnostics/candidate/Observer.h"
#include <windows.h>

// Match the shipped DLL's lack of a TryRead consumer under whole-program
// optimization. No game, rendering API, or resource hook is exercised.
extern "C" __declspec(dllexport) void BeginCapture() {
    DlssNr::CandidateObserver::StartCold(false);
}
extern "C" __declspec(dllexport) void EndCapture() {
    DlssNr::CandidateObserver::RequestClose(DlssNr::CandidateObserver::Reason::Requested);
}
