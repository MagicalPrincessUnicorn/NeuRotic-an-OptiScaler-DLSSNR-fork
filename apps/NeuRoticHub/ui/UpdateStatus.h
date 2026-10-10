#include <menu/Localization.h>
#pragma once
#include <string>
namespace nh {
enum class UpdateState {Checking, Current, Available, Unavailable};
struct UpdateStatus {UpdateState state=UpdateState::Unavailable;std::string tag,detail=Neurotic::UiMessage("desktop.updatestatus.update_status_unavailable_b7f21ad3", "Update status unavailable");};
UpdateStatus EvaluateRelease(const std::string& tag);
void StartUpdateCheck();
UpdateStatus GetUpdateStatus();
void StopUpdateCheck();
void RequestUpdateStop();bool UpdateStopReady();
inline constexpr wchar_t UpdatePage[]=L"https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases";
inline constexpr wchar_t ProjectPage[]=L"https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork";
inline constexpr wchar_t PatchNotesPage[]=L"https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases/latest";
}
