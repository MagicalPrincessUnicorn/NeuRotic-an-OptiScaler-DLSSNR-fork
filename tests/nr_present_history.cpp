#include "dlssnr/DlssNr_PresentHistory.h"
#include "dlssnr/DlssNr_PresentEffects.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

using DlssNr::PresentHistory::Continuity;

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
    Check(DlssNr::PresentEffects::FallbackPlacement(false) ==
              std::string_view("Original Present fallback"),
          "refusal before copyback leaves original output");
    Check(DlssNr::PresentEffects::FallbackPlacement(true) ==
              std::string_view("Present output may have changed"),
          "failure after copyback submission cannot claim untouched output");
    DlssNr::PresentHistory::HostReturnGate host;
    Check(host.Accept(11), "first host callback accepted");
    Check(!host.Accept(11) && !host.Accept(10), "duplicate and stale host callbacks refused");
    Check(host.Accept(12), "new host callback accepted after stale callback");
    Continuity history;
    Check(history.ResetForNextEvaluation(), "initial admitted frame resets Feature 18");
    Check(history.CompletedFrames() == 0, "initial history has no completed output");

    history.CompleteOutputPresent();
    Check(!history.ResetForNextEvaluation() && history.CompletedFrames() == 1,
          "first completed original Present enables continuity");
    history.CompleteOutputPresent();
    Check(!history.ResetForNextEvaluation() && history.CompletedFrames() == 2,
          "uninterrupted completed Presents stay reset-free");

    for (const auto reason : {
             "Present route or enable state changed",
             "NR resume generation changed",
             "Present target signature changed",
             "D3D11-to-D3D12 input synchronization failed",
             "D3D12-to-D3D11 output synchronization failed",
             "copyback completion signal failed",
             "original Present failed",
         })
    {
        history.Invalidate(reason);
        Check(history.ResetForNextEvaluation(), "every continuity interruption resets the next frame");
        Check(history.CompletedFrames() == 0, "interruption discards uninterrupted-frame count");
        Check(history.ResetReason() == reason && history.LastInvalidationReason() == reason,
              "diagnostics retain the precise continuity cause");
        history.CompleteOutputPresent();
        Check(!history.ResetForNextEvaluation(), "a successful completed Present recovers continuity");
    }

    std::cout << "PASS: Present history reset cadence and invalidation coverage\n";
}
