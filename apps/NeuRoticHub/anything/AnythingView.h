#pragma once
#include <json.hpp>
#include <string>
namespace nh {
struct HubModel;struct AnythingUiState;struct AnythingSnapshot;
void RenderAnything(HubModel& model,float dpi);
float AnythingCountdownWidth(float dpi);
void RenderAnythingCountdown(HubModel& model,float dpi);
void RequestAnythingDiagnosticsDialog();
void RenderAnythingAction(HubModel& model,float dpi,float width,float height=0);
nlohmann::json AnythingStartRequest(const AnythingUiState& page);
std::string AnythingActionLabel(const AnythingUiState& page,const AnythingSnapshot& state);
void ToggleAnythingSelected(HubModel& model);
bool CanCaptureAnythingScreenshot(const AnythingSnapshot& state);
bool CaptureAnythingScreenshot(HubModel& model);
}
