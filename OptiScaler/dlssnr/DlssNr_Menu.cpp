#include "pch.h"
#include "DlssNrFeature_Vk.h"

#include "DlssNr.h"
#include "DlssNr_ExposureScan.h"
#include "DlssNr_BridgeTelemetry.h"
#include "DlssNr_Present.h"
#include "DlssNr_PresentGuides.h"
#include "DlssNr_MenuStatus.h"
#include "DlssNr_StageControls.h"
#include "DlssNr_MenuControls.h"
#include "NrToggleBurst.h"
#include "NrToggleNotes.h"
#include "NrPendingEdit.h"
#include "NrScreenshotContract.h"


#include <Config.h>
#include <State.h>
#include <menu/menu_common.h>
#include <menu/Localization.h>

#include <imgui/imgui.h>
#include <imgui/ImGuiNotify.hpp>

#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <bit>
#include <array>
#include <optional>

namespace DlssNr
{

void NoteNrUserToggle()
{
    static ToggleBurstTracker tracker;
    const double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto message = tracker.Click(now, ToggleBurstMessages.size());
    if (!message)
        return;
    const auto translated = Neurotic::Translate(ToggleBurstMessages[*message]);
    ImGuiToast notification { ImGuiToastType::Info, 5000, translated.c_str() };
    notification.setTitle("NeuRotic");
    ImGui::InsertNotification(notification);
}

static bool IsVulkanInput()
{
    switch (State::Instance().currentInputApiName)
    {
    case ApiUpscalerInput::DLSS_VK:
    case ApiUpscalerInput::XeSS_VK:
    case ApiUpscalerInput::FFX_VK:
    case ApiUpscalerInput::FSR2X_VK:
        return true;
    default:
        return false;
    }
}

// Nested panels indent their contents on the left. Match that inset on the right so wrapped
// descriptions remain visually inside the panel instead of running to the parent column edge.
class ScopedNestedTextWrap
{
  public:
    explicit ScopedNestedTextWrap(float rightInset = 16.0f)
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - rightInset);
    }

    ~ScopedNestedTextWrap() { ImGui::PopTextWrapPos(); }
};

// The "(?)" marker every control carries, matching the rest of the menu.
static void HelpMarker(const char* tip)
{
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("(?)").x <= right)
        ImGui::SameLine();
    ImGui::TextDisabled("(?)");

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// A slider that only writes its value when the handle is released.
//
// Some controls -- intensity, the structure and tone strengths -- are read by the model once, when
// the feature is built, so changing one rebuilds the whole feature. Writing on every pixel of a drag
// meant a rebuild per frame, felt as the picture hitching while you scrub. The slider still tracks
// live under the cursor; only the commit that triggers the rebuild waits for release. Cheap controls
// that are just shader constants (detail, colour, paper white) do not use this -- they can afford to
// apply live.
static std::unordered_map<std::string, NrPendingEdit> pendingNrEdits;

static void CancelNrEdits()
{
    for (auto& [id, edit] : pendingNrEdits) edit.Cancel();
}

static bool DeferredNrSlider(const char* label, const std::vector<NrOptional<float>*>& targets,
                             float mn, float mx, float def, const char* fmt, bool percent = false,
                             ImVec2* sliderMin = nullptr, ImVec2* sliderMax = nullptr)
{
    auto& edit = pendingNrEdits[label];
    edit.Prepare(targets, ImGui::GetFrameCount());
    float value = edit.Value();
    bool changed = false;
    if (percent)
    {
        int percentage = (int) lroundf(value * 100.0f);
        if (ImGui::SliderInt(label, &percentage, (int) lroundf(mn * 100),
                             (int) lroundf(mx * 100), "%d%%", ImGuiSliderFlags_AlwaysClamp))
            edit.Preview(percentage / 100.0f);
    }
    else if (ImGui::SliderFloat(label, &value, mn, mx, fmt, ImGuiSliderFlags_AlwaysClamp))
        edit.Preview(value);
    if (sliderMin) *sliderMin = ImGui::GetItemRectMin();
    if (sliderMax) *sliderMax = ImGui::GetItemRectMax();
    if (ImGui::IsItemDeactivatedAfterEdit()) changed = edit.Commit(mn, mx);
    edit.Finish(ImGui::IsItemActive());
    ImGui::SameLine();
    const char* stableLabel = strstr(label, "###");
    const std::string resetId = stableLabel ?
        std::string("Reset###Reset##") + (stableLabel + 3) : std::string("Reset##") + label;
    if (ImGui::SmallButton(resetId.c_str()))
    {
        edit.Reset(def);
        CancelNrEdits();
        changed = true;
    }
    return changed;
}

static void DrawCumulativePassSegments(const ImVec2& sliderMin, const ImVec2& sliderMax,
                                       unsigned int maximumPasses, float menuResScale)
{
    static const ImVec4 palette[] = {
        { 0.95f, 0.30f, 0.28f, 0.95f }, { 0.26f, 0.82f, 0.38f, 0.95f },
        { 0.28f, 0.55f, 0.98f, 0.95f }, { 1.00f, 0.72f, 0.18f, 0.95f },
        { 0.80f, 0.36f, 0.92f, 0.95f }, { 0.18f, 0.78f, 0.86f, 0.95f },
        { 1.00f, 0.48f, 0.16f, 0.95f }, { 0.55f, 0.42f, 0.94f, 0.95f },
        { 0.60f, 0.86f, 0.22f, 0.95f }, { 0.18f, 0.68f, 0.60f, 0.95f }
    };
    const unsigned int count = (std::clamp)(maximumPasses, 1u, 10u);
    const float width = sliderMax.x - sliderMin.x;
    const float underline = (std::max)(2.0f, 2.0f * menuResScale);
    auto* draw = ImGui::GetWindowDrawList();
    for (unsigned int index = 0; index < count; ++index)
    {
        const float left = sliderMin.x + width * float(index) / float(count);
        const float right = sliderMin.x + width * float(index + 1) / float(count);
        draw->AddRectFilled({ left, sliderMax.y - underline }, { right, sliderMax.y },
                            ImGui::GetColorU32(palette[index]), 0.0f);
        if (index > 0)
            draw->AddLine({ left, sliderMin.y + underline }, { left, sliderMax.y },
                          ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.55f)), 1.0f);
    }
}

static bool DeferredSlider(const char* label, NrOptional<float>* opt, float mn, float mx,
                           float def, const char* fmt = "%.2f")
{
    return DeferredNrSlider(label, { opt }, mn, mx, def, fmt);
}

static unsigned int RenderPassCountSelector(Config* config)
{
    static const char* passCounts[] = { "Standard (1 pass)", "2 passes", "3 passes", "4 passes",
                                        "5 passes", "6 passes", "7 passes", "8 passes", "9 passes",
                                        "10 passes" };
    int passCountIndex = std::clamp((int) config->DlssNrPasses.value_or_default(), 1, 10) - 1;
    if (ImGui::Combo("Passes", &passCountIndex, passCounts, IM_ARRAYSIZE(passCounts)))
    {
        NrConfigSynchronization::Transaction transaction;
        config->DlssNrPasses = (uint32_t) (passCountIndex + 1);
        // Retain the old field as an in-memory compatibility hint. The persisted alias remains
        // derived from the Multipass switch, so choosing a count alone never activates it.
        config->DlssNrSecondLayer =
            config->DlssNrMultipassEnabled.value_or_default() && passCountIndex >= 1;
    }
    HelpMarker("Standard uses one pass. Additional passes require Enable NR Multipass on a compatible route. "
               "Their settings can be edited in Neural Rendering Multipass before enabling it.");
    return (unsigned int) (passCountIndex + 1);
}

static void RenderMultipassMenu(Config* config, float menuResScale);

namespace
{
enum class AdvisorPhase { Idle, Warmup, Sample };
enum class AdvisorResultLevel { Unknown, Analyzing, Available, Unavailable, Recommended };

struct AdvisorRouteResult
{
    AdvisorResultLevel level = AdvisorResultLevel::Unknown;
    bool succeeded = false;
    double fps = 0.0;
    double frameMs = 0.0;
    double modelMs = 0.0;
    unsigned int modelSamples = 0;
    std::string detail = "Not measured";
};

struct AdvisorOriginalSettings
{
    bool captured = false;
    std::optional<bool> enabled;
    std::optional<bool> applyModel;
    std::optional<bool> multipass;
    std::optional<bool> secondLayer;
    std::optional<uint32_t> passes;
    std::optional<uint32_t> route;
    std::optional<int32_t> renderingMode;
    std::optional<bool> runBefore;
    std::optional<bool> manualResolution;
    std::optional<float> workingScale;
    std::optional<uint32_t> presentResolution;
    std::optional<uint32_t> presentScale;
    std::optional<uint32_t> enhancedResolution;
    std::optional<uint32_t> enhancedScale;
};

struct AdvisorState
{
    AdvisorPhase phase = AdvisorPhase::Idle;
    bool running = false;
    bool analyzed = false;
    int appliedRoute = -1;
    int targetIndex = 2; // 60 FPS
    int goalIndex = 1;   // balanced
    int routeIndex = 0;
    int recommendation = -1;
    double phaseStarted = 0.0;
    unsigned int originalWidth = 0;
    unsigned int originalHeight = 0;
    unsigned long long startNativeFrames = 0;
    unsigned long long startPresentEvaluations = 0;
    unsigned long long startGuideEvaluations = 0;
    unsigned long long lastNativeGpuFrame = 0;
    unsigned long long lastPresentGpuSample = 0;
    unsigned long long lifecycleGeneration = 0;
    std::optional<NrConfigSnapshot<Config>> expectedSettings;
    double frameIntervalTotal = 0.0;
    unsigned int frameIntervalSamples = 0;
    double modelGpuTotal = 0.0;
    unsigned int modelGpuSamples = 0;
    std::array<AdvisorRouteResult, 3> routes;
    AdvisorOriginalSettings original;
    std::string status = "Choose a target and analyze the current game scene.";
    std::string reason = "No settings change until you choose an available route.";
    std::string gpuName = "Detecting graphics card...";
};

AdvisorState& Advisor()
{
    static AdvisorState state;
    return state;
}

double AdvisorNow()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void CaptureAdvisorSettings(Config& config, AdvisorOriginalSettings& out)
{
    NrConfigSynchronization::Transaction transaction;
    out.enabled = config.DlssNrEnabled.snapshot();
    out.applyModel = config.DlssNrApplyModel.snapshot();
    out.multipass = config.DlssNrMultipassEnabled.snapshot();
    out.secondLayer = config.DlssNrSecondLayer.snapshot();
    out.passes = config.DlssNrPasses.snapshot();
    out.route = config.DlssNrRoute.snapshot();
    out.renderingMode = config.DlssNrRenderingMode.snapshot();
    out.runBefore = config.DlssNrRunBeforeSr.snapshot();
    out.manualResolution = config.DlssNrUiManualResolution.snapshot();
    out.workingScale = config.DlssNrWorkingScale.snapshot();
    out.presentResolution = config.DlssNrPresentResolution.snapshot();
    out.presentScale = config.DlssNrPresentCustomScale.snapshot();
    out.enhancedResolution = config.DlssNrEnhancedResolution.snapshot();
    out.enhancedScale = config.DlssNrEnhancedCustomScale.snapshot();
    out.captured = true;
}

void RestoreAdvisorSettings(Config& config, AdvisorOriginalSettings& original)
{
    if (!original.captured) return;
    NrConfigSynchronization::Transaction transaction;
    const bool enabled = original.enabled.value_or(false);
    config.SetDlssNrEnabled(enabled);
    config.DlssNrEnabled = original.enabled;
    config.DlssNrApplyModel = original.applyModel;
    config.DlssNrMultipassEnabled = original.multipass;
    config.DlssNrSecondLayer = original.secondLayer;
    config.DlssNrPasses = original.passes;
    config.DlssNrRoute = original.route;
    config.DlssNrRenderingMode = original.renderingMode;
    config.DlssNrRunBeforeSr = original.runBefore;
    config.DlssNrUiManualResolution = original.manualResolution;
    config.DlssNrWorkingScale = original.workingScale;
    config.DlssNrPresentResolution = original.presentResolution;
    config.DlssNrPresentCustomScale = original.presentScale;
    config.DlssNrEnhancedResolution = original.enhancedResolution;
    config.DlssNrEnhancedCustomScale = original.enhancedScale;
    original.captured = false;
}

void ConfigureAdvisorRoute(Config& config, int route)
{
    NrConfigSynchronization::Transaction transaction;
    config.SetDlssNrEnabled(true);
    config.DlssNrApplyModel = false;
    config.DlssNrMultipassEnabled = false;
    config.DlssNrSecondLayer = false;
    config.DlssNrPasses = 1u;
    config.DlssNrRoute = uint32_t(std::clamp(route, 0, 2));
    if (route == 0)
    {
        config.DlssNrRenderingMode = 1;
        config.DlssNrRunBeforeSr = true;
        config.DlssNrUiManualResolution = false;
        config.DlssNrWorkingScale = 1.0f;
    }
    else
    {
        config.DlssNrRenderingMode = 0;
        config.DlssNrRunBeforeSr = false;
        if (route == 1)
        {
            config.DlssNrPresentResolution = PresentResolution::Automatic;
            config.DlssNrPresentCustomScale = 100u;
        }
        else
        {
            config.DlssNrEnhancedResolution = PresentResolution::Automatic;
            config.DlssNrEnhancedCustomScale = 100u;
        }
    }
}

void BeginAdvisorRoute(Config& config, int route)
{
    auto& advisor = Advisor();
    advisor.routeIndex = route;
    advisor.phase = AdvisorPhase::Warmup;
    advisor.phaseStarted = AdvisorNow();
    ConfigureAdvisorRoute(config, route);
    advisor.expectedSettings = TryNrConfigSnapshot(config);
    const auto native = DlssNr::Telemetry();
    advisor.lifecycleGeneration = native.lifecycleGeneration;
    const auto present = DlssNr::PresentTelemetry();
    const auto guides = DlssNr::PresentGuides::Instance().Inspect();
    advisor.startNativeFrames = native.completedPipelineEvaluations;
    advisor.startPresentEvaluations = present.modelEvaluations;
    advisor.startGuideEvaluations = guides.evaluated;
    advisor.lastNativeGpuFrame = native.completedPipelineEvaluations;
    advisor.lastPresentGpuSample = present.presentGpuSamples;
    advisor.frameIntervalTotal = 0.0;
    advisor.frameIntervalSamples = 0;
    advisor.modelGpuTotal = 0.0;
    advisor.modelGpuSamples = 0;
}

double AdvisorTargetFps(const AdvisorState& advisor)
{
    static constexpr double values[] = { 30.0, 45.0, 60.0, 90.0, 120.0, 144.0 };
    return values[std::clamp(advisor.targetIndex, 0, 5)];
}

void ChooseAdvisorRecommendation(AdvisorState& advisor)
{
    const double target = AdvisorTargetFps(advisor);
    const auto meets = [&](int route, double margin)
    {
        const auto& result = advisor.routes[route];
        return result.succeeded && (result.fps <= 0.0 || result.fps >= target * margin);
    };

    int selected = -1;
    if (advisor.goalIndex == 0) // quality
    {
        for (int route : { 2, 1, 0 }) if (selected < 0 && meets(route, 0.90)) selected = route;
    }
    else if (advisor.goalIndex == 1) // balanced
    {
        for (int route : { 2, 1, 0 }) if (selected < 0 && meets(route, 1.0)) selected = route;
    }
    else // performance
    {
        double fastest = -1.0;
        for (int route : { 0, 1, 2 })
            if (advisor.routes[route].succeeded && advisor.routes[route].fps > fastest)
                selected = route, fastest = advisor.routes[route].fps;
    }
    if (selected < 0)
    {
        double fastest = -1.0;
        for (int route : { 0, 1, 2 })
            if (advisor.routes[route].succeeded && advisor.routes[route].fps > fastest)
                selected = route, fastest = advisor.routes[route].fps;
    }

    advisor.recommendation = selected;
    for (int route = 0; route < 3; ++route)
        advisor.routes[route].level = !advisor.routes[route].succeeded ? AdvisorResultLevel::Unavailable :
            route == selected ? AdvisorResultLevel::Recommended : AdvisorResultLevel::Available;

    advisor.analyzed = true;
    advisor.appliedRoute = -1;
    if (selected < 0)
    {
        advisor.status = "No verified Neural Rendering route was available.";
        advisor.reason = "The original image was preserved. Review the route reasons below and try another scene.";
    }
    else
    {
        static constexpr const char* names[] = { "Native Temporal", "Present Compatibility", "Present Enhanced" };
        advisor.status = std::string("Recommended: ") + names[selected];
        const bool targetMet = advisor.routes[selected].fps <= 0.0 || advisor.routes[selected].fps >= target;
        advisor.reason = targetMet
            ? "Verified at 100% model resolution and one pass for the selected target."
            : "No verified 100% route met the target; the fastest verified route is recommended.";
    }
}

void FinishAdvisorRoute(Config& config)
{
    auto& advisor = Advisor();
    const int route = advisor.routeIndex;
    auto& result = advisor.routes[route];
    const auto native = DlssNr::Telemetry();
    const auto present = DlssNr::PresentTelemetry();
    const auto guides = DlssNr::PresentGuides::Instance().Inspect();
    if (advisor.frameIntervalSamples != 0 && advisor.frameIntervalTotal > 0.0)
    {
        result.frameMs = advisor.frameIntervalTotal / advisor.frameIntervalSamples;
        result.fps = 1000.0 / result.frameMs;
    }
    if (advisor.modelGpuSamples != 0)
    {
        result.modelMs = advisor.modelGpuTotal / advisor.modelGpuSamples;
        result.modelSamples = advisor.modelGpuSamples;
    }
    if (route == 0)
    {
        result.succeeded = native.running && !native.failed && !native.outputQuarantined &&
            native.completedPipelineEvaluations > advisor.startNativeFrames;
        result.detail = result.succeeded ? "Verified at 100% before upscaling" :
            (native.failureReason && native.failureReason[0] ? native.failureReason : "Native model output was not verified");
    }
    else
    {
        const bool expectedRoute = present.requestedPlacement ==
            (route == 2 ? "Present Enhanced" : "Present Image-Only");
        result.succeeded = expectedRoute && present.active && !present.failed &&
            present.modelEvaluations > advisor.startPresentEvaluations &&
            (route != 2 || guides.evaluated > advisor.startGuideEvaluations);
        if (result.succeeded)
            result.detail = route == 2 ? "Verified depth and motion guides" : "Verified final-image compatibility path";
        else if (!present.failure.empty()) result.detail = present.failure;
        else if (!present.fallbackReason.empty()) result.detail = present.fallbackReason;
        else if (route == 2 && !guides.status.empty()) result.detail = guides.status;
        else result.detail = "Present output was not verified";
    }

    if (route < 2)
        BeginAdvisorRoute(config, route + 1);
    else
    {
        RestoreAdvisorSettings(config, advisor.original);
        advisor.running = false;
        advisor.phase = AdvisorPhase::Idle;
        ChooseAdvisorRecommendation(advisor);
    }
}

void StartAdvisorAnalysis(Config& config)
{
    auto& advisor = Advisor();
    if (advisor.running) return;
    CancelComparisonScreenshot();
    advisor.routes = {};
    for (auto& route : advisor.routes)
    {
        route.level = AdvisorResultLevel::Analyzing;
        route.detail = "Analyzing...";
    }
    advisor.recommendation = -1;
    advisor.analyzed = false;
    advisor.appliedRoute = -1;
    advisor.status = "Testing available routes...";
    advisor.reason = "Model effect hidden; 100% resolution and one pass. Original settings will be restored.";
    CaptureAdvisorSettings(config, advisor.original);
    const auto present = DlssNr::PresentTelemetry();
    advisor.originalWidth = present.backbufferWidth;
    advisor.originalHeight = present.backbufferHeight;
    advisor.running = true;
    BeginAdvisorRoute(config, 0);
}

void ApplyAdvisorRoute(Config& config, int route)
{
    auto& advisor = Advisor();
    if (advisor.running || route < 0 || route >= static_cast<int>(advisor.routes.size()) ||
        !advisor.routes[route].succeeded)
        return;
    NrConfigSynchronization::Transaction transaction;
    config.DlssNrRoute = uint32_t(route);
    config.DlssNrUiAfterMethod = uint32_t(route == 0 ?
        config.DlssNrUiAfterMethod.value_or_default() : route);
    if (route == 0)
    {
        config.DlssNrRenderingMode = 1;
        config.DlssNrRunBeforeSr = true;
        config.DlssNrUiManualResolution = false;
        config.DlssNrWorkingScale = 1.0f;
    }
    else
    {
        config.DlssNrRenderingMode = 0;
        config.DlssNrRunBeforeSr = false;
        auto& mode = route == 2 ? config.DlssNrEnhancedResolution : config.DlssNrPresentResolution;
        auto& scale = route == 2 ? config.DlssNrEnhancedCustomScale : config.DlssNrPresentCustomScale;
        mode = PresentResolution::Automatic;
        scale = 100u;
    }
    static constexpr const char* names[] = { "Native Temporal", "Present Compatibility", "Present Enhanced" };
    advisor.appliedRoute = route;
    advisor.status = std::string("Applied: ") + names[route];
    advisor.reason = "Stage, method, and 100% resolution policy were applied atomically. Model tuning and Multipass were unchanged.";
}

ImVec4 AdvisorColor(AdvisorResultLevel level)
{
    switch (level)
    {
    case AdvisorResultLevel::Recommended: return { 0.25f, 0.90f, 0.38f, 1.0f };
    case AdvisorResultLevel::Available: return { 1.00f, 0.66f, 0.20f, 1.0f };
    case AdvisorResultLevel::Unavailable: return { 0.95f, 0.30f, 0.28f, 1.0f };
    case AdvisorResultLevel::Analyzing: return { 0.30f, 0.68f, 1.00f, 1.0f };
    default: return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }
}

const char* AdvisorLevelName(AdvisorResultLevel level)
{
    switch (level)
    {
    case AdvisorResultLevel::Recommended: return "Recommended";
    case AdvisorResultLevel::Available: return "Available";
    case AdvisorResultLevel::Unavailable: return "Unavailable";
    case AdvisorResultLevel::Analyzing: return "Analyzing...";
    default: return "Not measured";
    }
}

void AdvisorSignal(const char* label, const std::string& value, const ImVec4& color)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", value.c_str());
    ImGui::PopStyleColor();
}

const char* BackbufferFormatName(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "RGBA8 SDR";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "RGBA8 sRGB";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "RGB10A2 HDR/SDR";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "RGBA16F HDR";
    default: return "Waiting for output format";
    }
}

void RenderAdvisorRouteCard(Config& config, int route, float height)
{
    static constexpr const char* names[] = { "Native Temporal", "Present Compatibility", "Present Enhanced" };
    auto& advisor = Advisor();
    auto& result = advisor.routes[route];
    const ImVec4 color = AdvisorColor(result.level);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(color.x, color.y, color.z, 0.07f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(color.x, color.y, color.z, 0.70f));
    if (ImGui::BeginChild((std::string("##AdvisorRoute") + std::to_string(route)).c_str(),
                          ImVec2(0.0f, height), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::TextColored(color, "%s", names[route]);
        ImGui::SameLine();
        ImGui::TextColored(color, "- %s", AdvisorLevelName(result.level));
        ImGui::TextWrapped("%s", result.detail.c_str());
        if (result.fps > 0.0)
        {
            ImGui::TextDisabled("Measured %.0f FPS | Frame %.2f ms", result.fps, result.frameMs);
            if (result.modelMs > 0.0)
                ImGui::TextDisabled("NR route GPU %.2f ms | %u samples", result.modelMs, result.modelSamples);
            else if (result.succeeded)
                ImGui::TextDisabled("NR route GPU timing unavailable");
        }
        ImGui::SetCursorPosY((std::max)(ImGui::GetCursorPosY(), height - ImGui::GetFrameHeightWithSpacing() -
            ImGui::GetStyle().WindowPadding.y));
        const bool canApply = advisor.analyzed && result.succeeded && !advisor.running;
        ImGui::BeginDisabled(!canApply);
        if (canApply)
        {
            const ImVec4 button = color;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(button.x, button.y, button.z, 0.50f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(button.x, button.y, button.z, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(button.x, button.y, button.z, 0.95f));
        }
        const std::string buttonLabel = advisor.appliedRoute == route
            ? std::string("Applied##AdvisorApply") + std::to_string(route)
            : std::string("Use ") + names[route] + "##AdvisorApply" + std::to_string(route);
        if (ImGui::Button(buttonLabel.c_str(), ImVec2(-1.0f, 0.0f)))
            ApplyAdvisorRoute(config, route);
        if (canApply) ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void RenderAdvisor(Config* config, float menuResScale)
{
    auto& advisor = Advisor();
    const auto native = DlssNr::Telemetry();
    const auto present = DlssNr::PresentTelemetry();
    const auto guides = DlssNr::PresentGuides::Instance().Inspect();
    const auto& host = State::Instance();
    const bool fg = host.activeFgInput != FGInput::NoFG && host.activeFgOutput != FGOutput::NoFG;
    const ImVec4 green(0.40f, 0.90f, 0.50f, 1.0f);
    const ImVec4 orange(1.00f, 0.72f, 0.25f, 1.0f);
    const ImVec4 muted = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const float width = ImGui::GetContentRegionAvail().x;
    const bool wide = width >= 700.0f * menuResScale;

    ImGui::Spacing();
    auto header = ScopedCollapsingHeader("Neural Rendering Advisor", ImGuiTreeNodeFlags_DefaultOpen);
    if (!header.IsHeaderOpen()) return;
    ScopedIndent indent {};
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.12f, 0.18f, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.60f, 0.90f, 0.55f));
    if (ImGui::BeginChild("##AdvisorSummary", ImVec2(0.0f, 68.0f * menuResScale),
                          ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::TextColored(advisor.running ? AdvisorColor(AdvisorResultLevel::Analyzing) : green,
                           "%s", advisor.status.c_str());
        ImGui::TextWrapped("%s", advisor.reason.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    const auto renderSignals = [&]()
    {
        ImGui::TextDisabled("WHAT OPTISCALER SEES");
        if (ImGui::BeginTable("##AdvisorSignals", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthStretch, 0.44f);
            ImGui::TableSetupColumn("Reading", ImGuiTableColumnFlags_WidthStretch, 0.56f);
            AdvisorSignal("Graphics path", ApiUpscalerInputName(host.currentInputApiName), muted);
            AdvisorSignal("Graphics card", advisor.gpuName, muted);
            const unsigned int outputW = present.backbufferWidth ? present.backbufferWidth : native.frameWidth;
            const unsigned int outputH = present.backbufferHeight ? present.backbufferHeight : native.frameHeight;
            char outputText[128] = "Waiting for a rendered frame";
            if (outputW && outputH)
                std::snprintf(outputText, sizeof(outputText), "%u x %u | %s", outputW, outputH,
                              BackbufferFormatName(present.backbufferFormat));
            AdvisorSignal("Present output", outputText, outputW && outputH ? green : orange);
            const bool verifiedGuides = guides.evaluated > 0 || guides.matched > 0;
            AdvisorSignal("Depth guide", verifiedGuides ? "Captured and matched" : "Not yet verified",
                          verifiedGuides ? green : orange);
            AdvisorSignal("Motion guide", verifiedGuides ? "Captured and matched" : "Not yet verified",
                          verifiedGuides ? green : orange);
            AdvisorSignal("Guide matching", guides.status, verifiedGuides ? green : orange);
            AdvisorSignal("Frame generation", fg ? "Active - native FPS estimate limited" : "Off",
                          fg ? orange : muted);
            ImGui::EndTable();
        }
    };

    const auto renderRecommendation = [&]()
    {
        ImGui::TextDisabled("RECOMMENDED SETUP");
        if (advisor.recommendation >= 0)
        {
            static constexpr const char* routeNames[] = { "Native Temporal", "Present Compatibility", "Present Enhanced" };
            ImGui::TextColored(green, "%s", routeNames[advisor.recommendation]);
            ImGui::Text("Automatic | 100%% | 1 pass");
            ImGui::TextWrapped("%s", advisor.reason.c_str());
        }
        else
        {
            ImGui::TextColored(advisor.running ? AdvisorColor(AdvisorResultLevel::Analyzing) : muted,
                               "%s", advisor.running ? "Testing available routes..." : "Analysis required");
            ImGui::TextWrapped("OptiScaler will test each route without displaying the model effect, then restore your current setup.");
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Confidence: %s", fg ? "Limited while Frame Generation is active" :
            advisor.analyzed ? "Based on current-session measurements" : "Not measured");
    };

    if (wide && ImGui::BeginTable("##AdvisorOverview", 2,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableNextColumn(); renderSignals();
        ImGui::TableNextColumn(); renderRecommendation();
        ImGui::EndTable();
    }
    else
    {
        renderSignals(); ImGui::Separator(); renderRecommendation();
    }

    ImGui::Spacing();
    if (wide && ImGui::BeginTable("##AdvisorRoutes", 3, ImGuiTableFlags_SizingStretchSame))
    {
        for (int route = 0; route < 3; ++route) { ImGui::TableNextColumn(); RenderAdvisorRouteCard(*config, route, 126.0f * menuResScale); }
        ImGui::EndTable();
    }
    else
        for (int route = 0; route < 3; ++route) RenderAdvisorRouteCard(*config, route, 116.0f * menuResScale);

    ImGui::TextDisabled("Green recommended  |  Orange available  |  Red unavailable  |  Grey not measured");
    ImGui::Spacing();
    static constexpr const char* targets[] = { "30 FPS", "45 FPS", "60 FPS", "90 FPS", "120 FPS", "144 FPS" };
    static constexpr const char* goals[] = { "Prioritize quality", "Balance quality and performance", "Prioritize performance" };
    const float comboWidth = wide ? 230.0f * menuResScale : (std::max)(120.0f, width * 0.62f);
    const auto clearAnalysis = [&](const char* status)
    {
        advisor.analyzed = false; advisor.recommendation = -1; advisor.appliedRoute = -1;
        advisor.status = status;
        advisor.reason = "No settings change until you choose an available route.";
        advisor.routes = {};
    };
    const auto renderTarget = [&]()
    {
        ImGui::TextUnformatted("Target native framerate");
        HelpMarker("The target is real rendered-frame cadence. When Frame Generation is active, confidence remains limited until a verified native cadence is available.");
        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::Combo("##AdvisorTargetFps", &advisor.targetIndex, targets, IM_ARRAYSIZE(targets)))
            clearAnalysis("Target changed - analyze again.");
    };
    const auto renderGoal = [&]()
    {
        ImGui::TextUnformatted("Optimization goal");
        HelpMarker("Quality prefers verified Present Enhanced. Balanced requires the target at 100%. Performance always chooses the fastest verified route.");
        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::Combo("##AdvisorGoal", &advisor.goalIndex, goals, IM_ARRAYSIZE(goals)))
            clearAnalysis("Optimization goal changed - analyze again.");
    };
    ImGui::BeginDisabled(advisor.running);
    if (wide && ImGui::BeginTable("##AdvisorPreferences", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn(); renderTarget();
        ImGui::TableNextColumn(); renderGoal();
        ImGui::EndTable();
    }
    else
    {
        renderTarget();
        renderGoal();
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextColored(orange, "Analyze temporarily turns Neural Rendering on to test each route.");
    ImGui::TextWrapped("The model effect stays hidden, and your current settings are restored when analysis ends or is cancelled.");
    if (advisor.running)
    {
        if (ImGui::Button("Cancel Analysis")) CancelAdvisorAnalysis(config);
    }
    else if (ImGui::Button("Analyze This Game"))
        StartAdvisorAnalysis(*config);
    ImGui::TextDisabled("Analysis never tests below 100%% and never changes presets, strengths, Multipass, or Advanced settings.");
}
} // namespace

static void RenderLiveReadouts(Config* config, NrConfigSnapshot<Config> uiConfig, bool enabled,
                               bool basicOwnsMain,
                               const std::optional<MenuStatus::RuntimeStatus>& status)
{
    if (basicOwnsMain) BasicMultipass::Derive(uiConfig);
    const auto nrTelemetry = DlssNr::Telemetry();
    const auto presentTelemetry = DlssNr::PresentTelemetry();
    const bool vulkan = DlssNr::IsRunningVk() || IsVulkanInput();
    const int route = std::clamp((int) uiConfig.DlssNrRoute.value_or_default(), 0, 2);
    const int renderMode = std::clamp(uiConfig.DlssNrRenderingMode.value_or_default(), 0, 1);
    const bool presentRoute = route != 0;
    const ImVec4 green(0.4f, 0.9f, 0.5f, 1.0f);
    const ImVec4 yellow(1.0f, 0.72f, 0.25f, 1.0f);
    const ImVec4 red(1.0f, 0.4f, 0.35f, 1.0f);

    static MenuStatus::SelectionObservation observation;
    const auto selection = (PresentResolution::CaptureKey(uiConfig) << 1) | (enabled ? 1ull : 0ull);
    const bool fresh = observation.Fresh(selection,
        presentTelemetry.presentAttempts + presentTelemetry.skippedFrames);
    const auto policy = PresentResolution::Selected(uiConfig);
    const bool presentMatches = fresh && presentTelemetry.requested &&
        presentTelemetry.requestedPlacement == (route == 2 ? "Present Enhanced" : "Present Image-Only");
    const bool presentActive = presentMatches && presentTelemetry.active &&
        presentTelemetry.resolution == policy.mode && presentTelemetry.workload == policy.scale;
    static MenuStatus::SelectionObservation nativeObservation;
    const auto nativeSelection = selection ^ (uint64_t(renderMode) << 20) ^
        (uint64_t(std::bit_cast<uint32_t>(uiConfig.DlssNrWorkingScale.value_or_default())) << 24);
    const bool nativeFresh = nativeObservation.Fresh(nativeSelection, nrTelemetry.frames);
    const bool nativeOutput = enabled && !presentRoute && !vulkan && nativeFresh &&
        nrTelemetry.running && !nrTelemetry.outputQuarantined && !nrTelemetry.transitionPending &&
        (renderMode == 0 || nrTelemetry.nativeRayReconstructionActive || nrTelemetry.preSrDisplayReady);

    if (enabled && nativeOutput && nrTelemetry.totalGpuMs)
        ImGui::TextColored(green, "NR processing: %.2f ms per frame", *nrTelemetry.totalGpuMs);
    else if (enabled && presentActive && presentTelemetry.presentGpuValid &&
             presentTelemetry.presentGpuRoute == (route == 2
                 ? PresentPacing::Route::PresentEnhanced
                 : PresentPacing::Route::PresentImageOnly))
        ImGui::TextColored(green, "NR processing: %.2f ms per frame", presentTelemetry.presentGpuMs);

    if (!enabled)
        ImGui::TextColored(yellow, "Neural Rendering is off.");
    else if (basicOwnsMain && BasicMultipass::Count(uiConfig.DlssNrBasicMultipass.value_or_default()) == 0)
        ImGui::TextColored(yellow, "Basic Multipass totals are zero. Image unchanged; loaded resources retained.");
    else if (StageUi::RenderRuntimeStatus(status))
    {
        // A caller may supply verified telemetry; the UI does not produce frame identity.
    }
    else if (presentRoute)
    {
        if (presentActive)
            ImGui::TextColored(green, "%s is active.", StageUi::Methods[route]);
        else if (presentMatches && !presentTelemetry.failure.empty())
            ImGui::TextColored(red, "Image unchanged. %s", presentTelemetry.failure.c_str());
        else if (presentMatches && !presentTelemetry.fallbackReason.empty())
            ImGui::TextColored(yellow, "Image unchanged. %s", presentTelemetry.fallbackReason.c_str());
        else
            ImGui::TextColored(yellow, "Waiting for the selected route. Image unchanged.");
    }
    else
    {
        const char* vkReason = DlssNr::FailureReasonVk();
        const char* reason = vulkan ? vkReason : nrTelemetry.failureReason;
        const bool nativeActive = vulkan ? DlssNr::IsRunningVk() : nativeOutput;
        if (reason[0])
        {
            ImGui::TextColored(red, "Neural Rendering unavailable: %s", reason);
            if (nrTelemetry.retryAllowed && !vkReason[0] && ImGui::SmallButton("Retry"))
                DlssNr::RetryAfterFailure();
        }
        else if (nativeActive)
            ImGui::TextColored(green, "Native Temporal is active.");
        else
            ImGui::TextColored(yellow, "Waiting for Native Temporal. Image unchanged.");
    }

    if (!presentRoute && (nrTelemetry.nativeRayReconstructionActive || vulkan))
        ImGui::TextWrapped("Ray Reconstruction and native Vulkan keep NR after reconstruction. Before-stage placement is unavailable on these paths.");
    if (enabled && !config->DlssNrApplyModel.value_or_default())
        ImGui::TextColored(yellow, "Model effect hidden. Enable Apply the model to show it.");
    if (route == 2)
    {
        ImGui::TextColored(yellow, "Experimental: Frame Generation, Ray Reconstruction, NR Multipass and DX11.");
        HelpMarker("These combinations are unlocked. Processing requires fresh matching guides and compatible resources. Vulkan Present has no adapter yet. SDR output is required.");
    }
}

void TickAdvisor(Config* config)
{
    auto& advisor = Advisor();
    if (!advisor.running || config == nullptr) return;
    const auto currentSettings = TryNrConfigSnapshot(*config);
    const auto lifecycle = DlssNr::Telemetry();
    if (!currentSettings || !advisor.expectedSettings ||
        !advisor.expectedSettings->SameConfiguration(*currentSettings) ||
        !lifecycle.lifecycleOpen || lifecycle.lifecycleGeneration != advisor.lifecycleGeneration)
    {
        CancelAdvisorAnalysis(config, "Route, settings or rendering session changed; original settings were restored.");
        return;
    }
    const auto present = DlssNr::PresentTelemetry();
    if ((advisor.originalWidth && present.backbufferWidth && advisor.originalWidth != present.backbufferWidth) ||
        (advisor.originalHeight && present.backbufferHeight && advisor.originalHeight != present.backbufferHeight))
    {
        CancelAdvisorAnalysis(config, "Output size changed; analysis stopped and original settings were restored.");
        return;
    }
    if (State::Instance().isShuttingDown)
    {
        CancelAdvisorAnalysis(config, "Rendering device is shutting down; original settings were restored.");
        return;
    }
    const double elapsed = AdvisorNow() - advisor.phaseStarted;
    if (advisor.phase == AdvisorPhase::Warmup && elapsed >= 1.0)
    {
        const auto native = DlssNr::Telemetry();
        const auto guides = DlssNr::PresentGuides::Instance().Inspect();
        advisor.startNativeFrames = native.completedPipelineEvaluations;
        advisor.startPresentEvaluations = present.modelEvaluations;
        advisor.startGuideEvaluations = guides.evaluated;
        advisor.lastNativeGpuFrame = native.completedPipelineEvaluations;
        advisor.lastPresentGpuSample = present.presentGpuSamples;
        advisor.frameIntervalTotal = 0.0;
        advisor.frameIntervalSamples = 0;
        advisor.modelGpuTotal = 0.0;
        advisor.modelGpuSamples = 0;
        advisor.phase = AdvisorPhase::Sample;
        advisor.phaseStarted = AdvisorNow();
    }
    else if (advisor.phase == AdvisorPhase::Sample)
    {
        if (std::isfinite(present.frameIntervalMs) && present.frameIntervalMs > 0.0 &&
            present.frameIntervalMs < 1000.0)
        {
            advisor.frameIntervalTotal += present.frameIntervalMs;
            ++advisor.frameIntervalSamples;
        }

        if (advisor.routeIndex == 0)
        {
            const auto native = DlssNr::Telemetry();
            if (native.completedPipelineEvaluations > advisor.lastNativeGpuFrame)
            {
                advisor.lastNativeGpuFrame = native.completedPipelineEvaluations;
                if (native.totalGpuMs && std::isfinite(*native.totalGpuMs) && *native.totalGpuMs >= 0.0)
                {
                    advisor.modelGpuTotal += *native.totalGpuMs;
                    ++advisor.modelGpuSamples;
                }
            }
        }
        else if (present.presentGpuSamples > advisor.lastPresentGpuSample)
        {
            advisor.lastPresentGpuSample = present.presentGpuSamples;
            const auto expectedRoute = advisor.routeIndex == 2
                ? DlssNr::PresentPacing::Route::PresentEnhanced
                : DlssNr::PresentPacing::Route::PresentImageOnly;
            if (present.presentGpuValid && present.presentGpuRoute == expectedRoute && std::isfinite(present.presentGpuMs) &&
                present.presentGpuMs >= 0.0)
            {
                advisor.modelGpuTotal += present.presentGpuMs;
                ++advisor.modelGpuSamples;
            }
        }

        if (elapsed >= 2.0)
            FinishAdvisorRoute(*config);
    }
}

void CancelAdvisorAnalysis(Config* config, const char* reason)
{
    auto& advisor = Advisor();
    if (!advisor.running || config == nullptr) return;
    RestoreAdvisorSettings(*config, advisor.original);
    advisor.running = false;
    advisor.phase = AdvisorPhase::Idle;
    advisor.analyzed = false;
    advisor.recommendation = -1;
    advisor.appliedRoute = -1;
    advisor.routes = {};
    advisor.status = reason != nullptr ? reason : "Analysis cancelled; original settings restored.";
    advisor.reason = "No recommendation was applied.";
}

void RenderMenu(Config* config, float menuResScale, const std::optional<MenuStatus::RuntimeStatus>& status,
                const char* gpuName)
{
    Neurotic::EnglishPreview englishPreview;
    Advisor().gpuName = gpuName != nullptr && gpuName[0] != 0 ? gpuName : "Detecting graphics card...";
    RenderAdvisor(config, menuResScale);
    // DLSS Neural Rendering -----------------------------
    ImGui::Spacing();
    {
    bool enabled = config->GetDlssNrRuntimeSnapshot().enabled;
    auto ch = ScopedCollapsingHeader("DLSS Neural Rendering", ImGuiTreeNodeFlags_DefaultOpen);
    if (ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);

        if (MenuControls::EmphasizedCheckbox("Enable Neural Rendering", &enabled))
        {
            config->SetDlssNrEnabled(enabled);
            NoteNrUserToggle();
        }
        const bool applyInline = MenuControls::LastItemHasInlineRoom(
            MenuControls::CheckboxWithHelpWidth("Apply the model"));
        if (applyInline)
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x + ImGui::GetStyle().FramePadding.x);
        bool applyModel = config->DlssNrApplyModel.value_or_default();
        if (ImGui::Checkbox("Apply the model", &applyModel))
            config->DlssNrApplyModel = applyModel;
        HelpMarker("Shows the whole chain's effect. Turn off to compare with the original image while the models keep running.");

        const bool basicOwnsMain = BasicMultipass::Active(config->GetDlssNrConfigSnapshot()) && !IsVulkanInput();
        RenderLiveReadouts(config, config->GetDlssNrConfigSnapshot(), enabled, basicOwnsMain, status);
        if (StageUi::RenderControls(*config, basicOwnsMain)) CancelNrEdits();
        auto uiConfig = config->GetDlssNrConfigSnapshot();
        if (basicOwnsMain) BasicMultipass::Derive(uiConfig);
        const auto& routeNames = StageUi::Methods;
        const int route = std::clamp((int) uiConfig.DlssNrRoute.value_or_default(), 0, 2);
        const int stage = StageUi::Stage(uiConfig);
        const int renderMode = std::clamp(uiConfig.DlssNrRenderingMode.value_or_default(), 0, 1);
        static const char* renderModeNames[] = { "Quality", "Performance (Default)" };
        const bool presentRoute = route != 0;
        if (basicOwnsMain)
            ImGui::TextWrapped("Basic Multipass controls resolution, downscaler and strengths for every pass. These controls show Pass 1; edit them in Multipass below.");
        ImGui::BeginDisabled(basicOwnsMain);
        if (StageUi::ResolutionSelection(uiConfig) == 1)
        {
            static NrOptional<float> scalePreview { 1.0f };
            static uint64_t previousSelection = 0;
            const auto selection = PresentResolution::CaptureKey(uiConfig) * 4 + stage;
            if (selection != previousSelection) CancelNrEdits();
            previousSelection = selection;
            const auto editGeneration = NrConfigSynchronization::ProfileGeneration();
            const float originalScale = StageUi::ResolutionScale(uiConfig);
            scalePreview = originalScale;
            ImGui::TextUnformatted("Manual resolution");
            HelpMarker("Sets the Neural Rendering working resolution as a percentage of the selected "
                       "stage: the game's render input Before upscaling, or the final upscaled output "
                       "After. Lower values reduce model cost and fine detail. Values above 100% "
                       "supersample, increase cost roughly with image area, and reveal the downscaler. "
                       "Reset restores 100%.");
            ImGui::SetNextItemWidth((std::max)(40.0f, ImGui::GetContentRegionAvail().x -
                ImGui::CalcTextSize("Reset (?)").x - ImGui::GetStyle().ItemSpacing.x * 3));
            if (DeferredNrSlider("##NrManualScale", { &scalePreview }, 0.25f, 2.0f, 1.0f, "%d%%", true))
            {
                NrConfigSynchronization::Transaction transaction;
                const auto current = config->GetDlssNrConfigSnapshot();
                if (editGeneration == NrConfigSynchronization::ProfileGeneration() &&
                    selection == PresentResolution::CaptureKey(current) * 4 + StageUi::Stage(current) &&
                    originalScale == StageUi::ResolutionScale(current))
                    StageUi::SelectResolutionScale(*config, scalePreview.value_or_default());
                else CancelNrEdits();
            }
            uiConfig = config->GetDlssNrConfigSnapshot();
            if (basicOwnsMain) BasicMultipass::Derive(uiConfig);
        }
        ImGui::EndDisabled();
        const auto renderReadouts = [&](bool detailed)
        {
        // The setting requests Pre-SR. It is deliberately not described as active until the
        // replacement-resource, reset, seed, and display-ready checks have all passed.
        const auto nrTelemetry = DlssNr::Telemetry();
        const auto presentTelemetry = DlssNr::PresentTelemetry();
        const auto bridgeTelemetry = DlssNr::BridgeTelemetry().Snapshot();
        const bool vulkan = DlssNr::IsRunningVk() || IsVulkanInput();

        const ImVec4 green(0.4f, 0.9f, 0.5f, 1.0f);
        const ImVec4 yellow(1.0f, 0.72f, 0.25f, 1.0f);
        const ImVec4 red(1.0f, 0.4f, 0.35f, 1.0f);
        static MenuStatus::SelectionObservation observation;
        const auto selection = (PresentResolution::CaptureKey(uiConfig) << 1) | (enabled ? 1ull : 0ull);
        const bool fresh = observation.Fresh(selection, presentTelemetry.presentAttempts + presentTelemetry.skippedFrames);
        const auto policy = PresentResolution::Selected(uiConfig);
        const bool presentMatches = fresh && presentTelemetry.requested &&
            presentTelemetry.requestedPlacement == (route == 2 ? "Present Enhanced" : "Present Image-Only");
        const bool presentActive = presentMatches && presentTelemetry.active &&
            presentTelemetry.resolution == policy.mode &&
            presentTelemetry.workload == policy.scale;
        static MenuStatus::SelectionObservation nativeObservation;
        const auto nativeSelection = selection ^ (uint64_t(renderMode) << 20) ^
            (uint64_t(std::bit_cast<uint32_t>(uiConfig.DlssNrWorkingScale.value_or_default())) << 24);
        const bool nativeFresh = nativeObservation.Fresh(nativeSelection, nrTelemetry.frames);
        const bool nativeOutput = enabled && !presentRoute && !vulkan && nativeFresh &&
            nrTelemetry.running && !nrTelemetry.outputQuarantined && !nrTelemetry.transitionPending &&
            (renderMode == 0 || nrTelemetry.nativeRayReconstructionActive || nrTelemetry.preSrDisplayReady);
        if (!detailed)
        {
        if (presentRoute)
            ImGui::TextWrapped(StageUi::ResolutionSelection(uiConfig) == 2 ?
                "Legacy follows fresh game render dimensions. Selecting Automatic or Manual adopts the new resolution policy." :
                "Present runs after upscaling and includes the HUD. Automatic uses the final output; Manual scales it. Each method remembers its selection.");
        else if (stage == 0)
            ImGui::TextWrapped("Automatic uses 100% of the game render input; Manual scales that input.");
        else
            ImGui::TextWrapped("Automatic uses 100% of the final upscaled output; Manual scales that output.");
        if (!presentRoute && (nrTelemetry.nativeRayReconstructionActive || vulkan))
            ImGui::TextWrapped("Ray Reconstruction and native Vulkan keep NR after reconstruction. Before-stage placement is unavailable on these paths.");
        const auto stageLabel = Neurotic::Translate(StageUi::Stages[stage]);
        const auto methodLabel = Neurotic::Translate(routeNames[route]);
        auto resolutionLabel = Neurotic::Translate(StageUi::Resolutions[StageUi::ResolutionSelection(uiConfig)]);
        if (StageUi::ResolutionSelection(uiConfig) == 1)
            resolutionLabel += " (" + std::to_string(StageUi::DisplayPercent(StageUi::ResolutionScale(uiConfig))) + "%)";
        uint32_t workW = 0, workH = 0, outputW = 0, outputH = 0;
        if (presentRoute && enabled && presentMatches)
        {
            outputW = presentTelemetry.backbufferWidth; outputH = presentTelemetry.backbufferHeight;
            if (presentActive) { workW = presentTelemetry.workWidth; workH = presentTelemetry.workHeight; }
        }
        else if (nativeOutput)
        {
            workW = nrTelemetry.workWidth; workH = nrTelemetry.workHeight;
            // Frame is the NR stage raster. Before SR it is not the final upscaled output.
            const bool before = renderMode != 0 && !nrTelemetry.nativeRayReconstructionActive;
            const auto feature = State::Instance().currentFeature;
            outputW = before ? (feature ? feature->DisplayWidth() : 0u) : nrTelemetry.frameWidth;
            outputH = before ? (feature ? feature->DisplayHeight() : 0u) : nrTelemetry.frameHeight;
        }
        if (enabled && status)
        {
            outputW = status->outputWidth; outputH = status->outputHeight;
            const bool active = status->state == MenuStatus::State::Active;
            workW = active ? status->workWidth : 0u; workH = active ? status->workHeight : 0u;
        }
        const auto dimensions = StageUi::DimensionText(workW, workH, outputW, outputH);
        const auto summaryResolution = resolutionLabel + " -> " + dimensions;
        ImGui::TextWrapped("%s -> %s -> %s", stageLabel.c_str(), methodLabel.c_str(), summaryResolution.c_str());
        if (enabled && nativeOutput && nrTelemetry.totalGpuMs)
            ImGui::TextColored(green, "NR processing: %.2f ms per frame", *nrTelemetry.totalGpuMs);

        if (!enabled)
            ImGui::TextColored(yellow, "Neural Rendering is off.");
        else if (basicOwnsMain && BasicMultipass::Count(uiConfig.DlssNrBasicMultipass.value_or_default()) == 0)
            ImGui::TextColored(yellow, "Basic Multipass totals are zero. Image unchanged; loaded resources retained.");
        else if (StageUi::RenderRuntimeStatus(status))
        {
            // A caller may supply verified telemetry; the UI does not produce frame identity.
        }
        else if (presentRoute)
        {
            if (presentActive)
                ImGui::TextColored(green, "%s is active.", routeNames[route]);
            else if (presentMatches && !presentTelemetry.failure.empty())
                ImGui::TextColored(red, "Image unchanged. %s", presentTelemetry.failure.c_str());
            else if (presentMatches && !presentTelemetry.fallbackReason.empty())
                ImGui::TextColored(yellow, "Image unchanged. %s", presentTelemetry.fallbackReason.c_str());
            else
                ImGui::TextColored(yellow, "Waiting for the selected route. Image unchanged.");
        }
        else
        {
            const char* vkReason = DlssNr::FailureReasonVk();
            const char* reason = vulkan ? vkReason : nrTelemetry.failureReason;
            const bool nativeActive = vulkan ? DlssNr::IsRunningVk() : nativeOutput;
            if (reason[0])
            {
                ImGui::TextColored(red, "Neural Rendering unavailable: %s", reason);
                if (nrTelemetry.retryAllowed && !vkReason[0] && ImGui::SmallButton("Retry"))
                    DlssNr::RetryAfterFailure();
            }
            else if (nativeActive)
                ImGui::TextColored(green, "Native Temporal is active.");
            else
                ImGui::TextColored(yellow, "Waiting for Native Temporal. Image unchanged.");
        }
        if (enabled && !config->DlssNrApplyModel.value_or_default())
            ImGui::TextColored(yellow, "Model effect hidden. Enable Apply the model to show it.");
        if (route == 2)
        {
            ImGui::TextColored(yellow, "Experimental: Frame Generation, Ray Reconstruction, NR Multipass and DX11.");
            HelpMarker("These combinations are unlocked. Processing requires fresh matching guides and compatible resources. Vulkan Present has no adapter yet. SDR output is required.");
        }
        }
        if (detailed)
        {
        ScopedIndent diagnosticIndent {};
        if (presentRoute)
            ImGui::TextWrapped(StageUi::ResolutionSelection(uiConfig) == 2 ?
                "Legacy follows fresh game render dimensions. Selecting Automatic or Manual adopts the new resolution policy." :
                "Present runs after upscaling and includes the HUD. Automatic uses the final output; Manual scales it. Each method remembers its selection.");
        else if (stage == 0)
            ImGui::TextWrapped("Automatic uses 100% of the game render input; Manual scales that input.");
        else
            ImGui::TextWrapped("Automatic uses 100% of the final upscaled output; Manual scales that output.");

        const auto stageLabel = Neurotic::Translate(StageUi::Stages[stage]);
        const auto methodLabel = Neurotic::Translate(routeNames[route]);
        auto resolutionLabel = Neurotic::Translate(StageUi::Resolutions[StageUi::ResolutionSelection(uiConfig)]);
        if (StageUi::ResolutionSelection(uiConfig) == 1)
            resolutionLabel += " (" + std::to_string(StageUi::DisplayPercent(StageUi::ResolutionScale(uiConfig))) + "%)";
        uint32_t workW = 0, workH = 0, outputW = 0, outputH = 0;
        if (presentRoute && enabled && presentMatches)
        {
            outputW = presentTelemetry.backbufferWidth; outputH = presentTelemetry.backbufferHeight;
            if (presentActive) { workW = presentTelemetry.workWidth; workH = presentTelemetry.workHeight; }
        }
        else if (nativeOutput)
        {
            workW = nrTelemetry.workWidth; workH = nrTelemetry.workHeight;
            const bool before = renderMode != 0 && !nrTelemetry.nativeRayReconstructionActive;
            const auto feature = State::Instance().currentFeature;
            outputW = before ? (feature ? feature->DisplayWidth() : 0u) : nrTelemetry.frameWidth;
            outputH = before ? (feature ? feature->DisplayHeight() : 0u) : nrTelemetry.frameHeight;
        }
        if (enabled && status)
        {
            outputW = status->outputWidth; outputH = status->outputHeight;
            const bool active = status->state == MenuStatus::State::Active;
            workW = active ? status->workWidth : 0u; workH = active ? status->workHeight : 0u;
        }
        const auto dimensions = StageUi::DimensionText(workW, workH, outputW, outputH);
        const auto summaryResolution = resolutionLabel + " -> " + dimensions;
        ImGui::TextWrapped("%s -> %s -> %s", stageLabel.c_str(), methodLabel.c_str(), summaryResolution.c_str());

        const auto guides = PresentGuides::Instance().Inspect();
        if (presentRoute)
        {
            ImGui::TextWrapped("%s", guides.status.c_str());
            ImGui::Text("Native capture calls %llu", guides.captureAttempts);
            if (!guides.inputDescription.empty()) ImGui::TextWrapped("%s", guides.inputDescription.c_str());
            if (!guides.captureError.empty()) ImGui::TextWrapped("Capture failure: %s", guides.captureError.c_str());
            ImGui::Text("Guide copies %llu | matched %llu | evaluated %llu | rejected %llu",
                guides.captures, guides.matched, guides.evaluated, guides.rejected);
        }
        if (State::Instance().api == API::Vulkan)
        {
            const auto tuningStatus = DlssNr::TuningStatusVk();
            ImGui::TextWrapped("Vulkan model: %s", tuningStatus.c_str());
            ImGui::TextDisabled("Model sliders request settings on release; composition controls remain live.");
        }
        if (!enabled)
        {
            if (!presentRoute) ImGui::TextDisabled("Rendering mode selected: %s.", renderModeNames[renderMode]);
        }
        else if (presentRoute)
        {
            ImGui::TextDisabled("Requested placement: %s.", presentTelemetry.requestedPlacement.c_str());
            if (presentTelemetry.active)
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Actual placement: %s.",
                                   presentTelemetry.actualPlacement.c_str());
            else
                ImGui::TextDisabled("Actual placement: %s.", presentTelemetry.actualPlacement.c_str());
        }
        else if (nrTelemetry.failed)
        {
            ImGui::TextDisabled("Rendering mode requested: %s; NR is unavailable.", renderModeNames[renderMode]);
        }
        else if (vulkan)
        {
            ImGui::TextDisabled("Native Vulkan NR active after the upscaler.");
        }
        else if (nrTelemetry.nativeRayReconstructionActive)
        {
            ImGui::TextDisabled(nrTelemetry.running ? "Ray Reconstruction active: native RR upscale, then NR."
                                                  : "Ray Reconstruction active: waiting for NR after native RR.");
        }
        else if (renderMode != 0)
        {
            if (nrTelemetry.preSrDisplayReady)
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f),
                                   "%s active: Pre-SR, before native DLSS upscale.", renderModeNames[renderMode]);
            else if (nrTelemetry.outputQuarantined)
                ImGui::TextDisabled("%s requested: changing path; NR output is withheld.", renderModeNames[renderMode]);
            else
                ImGui::TextDisabled("%s requested: waiting for a safe Pre-SR evaluation.", renderModeNames[renderMode]);
        }
        else
        {
            ImGui::TextDisabled(nrTelemetry.running ? "Quality active: Post-SR after native DLSS upscale."
                                                  : "Quality requested: waiting for a successful Post-SR evaluation.");
        }

        // Report the same locked D3D12 observation used above. A loaded model can be retained
        // while NR is disabled; its previous frame's cost must not imply current activity.
        if (!enabled)
        {
            ImGui::TextDisabled("Off. Any loaded model is retained for the next enable request.");
        }
        else if (presentRoute)
        {
            const char* api = presentTelemetry.api == PresentApi::D3D12 ? "DX12"
                              : presentTelemetry.api == PresentApi::D3D11 ? "DX11"
                              : presentTelemetry.api == PresentApi::Vulkan ? "Vulkan" : "Unknown";
            ImGui::Text("API %s | target %ux%u | format %u | samples %u", api,
                        presentTelemetry.backbufferWidth, presentTelemetry.backbufferHeight,
                        (unsigned int) presentTelemetry.backbufferFormat,
                        presentTelemetry.backbufferSampleCount);
            ImGui::Text("Swap effect %u | color space %u",
                        (unsigned int) presentTelemetry.swapEffect,
                        (unsigned int) presentTelemetry.colorSpace);
            if (!presentTelemetry.compatibilityPath.empty())
                ImGui::Text("Compatibility path: %s", presentTelemetry.compatibilityPath.c_str());
            ImGui::Text("Model evaluations %llu | spatial upscale/composites %llu | skipped %llu",
                        presentTelemetry.modelEvaluations, presentTelemetry.compositeEvaluations,
                        presentTelemetry.skippedFrames);
            ImGui::Text("Present history: %s | uninterrupted output frames %llu",
                        presentTelemetry.historyResetPending ? "reset pending" : "continuous",
                        presentTelemetry.uninterruptedFrames);
            ImGui::TextDisabled("Reset reason: %s | last interruption: %s",
                                presentTelemetry.historyResetReason.c_str(),
                                presentTelemetry.historyInvalidationReason.empty()
                                    ? "none" : presentTelemetry.historyInvalidationReason.c_str());
            ImGui::Text("Command submissions: model %llu | composite %llu",
                        presentTelemetry.modelSubmissions, presentTelemetry.compositeSubmissions);
            ImGui::Text("Attempts %llu | fallback streak %llu | last fallback attempt %llu",
                        presentTelemetry.presentAttempts, presentTelemetry.consecutiveFallbacks,
                        presentTelemetry.lastFallbackAttempt);
            ImGui::Text("Fence submitted %llu | completed %llu | pending slots %u / 8",
                        presentTelemetry.lastSubmittedFence, presentTelemetry.lastCompletedFence,
                        presentTelemetry.pendingSlots);
            ImGui::Text("Adapter CPU %.2f ms (max %.2f; >=4 ms: %llu) | original Present CPU %.2f ms (max %.2f; >=33.3 ms: %llu)",
                        presentTelemetry.adapterCpuMs, presentTelemetry.adapterCpuMaxMs,
                        presentTelemetry.adapterCpuSlowCalls, presentTelemetry.originalPresentMs,
                        presentTelemetry.originalPresentMaxMs, presentTelemetry.originalPresentSlowCalls);
            if (presentTelemetry.hasPacingSummary)
            {
                const auto& summary = presentTelemetry.pacingSummary;
                const char* summaryRoute = summary.route == PresentPacing::Route::PresentEnhanced ? "Present Enhanced" :
                    summary.route == PresentPacing::Route::PresentImageOnly
                                               ? "Present Image Only" : "Native Temporal";
                ImGui::TextDisabled("Completed pacing window %llu: %s | %llu samples; warm-up discarded %llu",
                                    summary.serial, summaryRoute,
                                    static_cast<unsigned long long>(summary.frameInterval.samples),
                                    summary.warmupDiscarded);
                ImGui::Text("Frame ms avg %.2f | median %.2f | p95 %.2f | max %.2f",
                            summary.frameInterval.average, summary.frameInterval.median,
                            summary.frameInterval.p95, summary.frameInterval.maximum);
                ImGui::Text("CPU p95/max ms: adapter %.3f/%.3f | hook %.3f/%.3f | Present %.3f/%.3f",
                            summary.adapterCpu.p95, summary.adapterCpu.maximum, summary.hookCpu.p95,
                            summary.hookCpu.maximum, summary.originalPresentCpu.p95,
                            summary.originalPresentCpu.maximum);
                if (summary.route != PresentPacing::Route::NativeTemporal)
                {
                    ImGui::Text("Present GPU %llu/%llu: median %.2f | p95 %.2f | max %.2f ms",
                                static_cast<unsigned long long>(summary.presentGpu.samples),
                                summary.expectedGpuSamples, summary.presentGpu.median,
                                summary.presentGpu.p95, summary.presentGpu.maximum);
                    ImGui::Text("Completion-observed upper bound p95/max %.2f/%.2f ms | fence age p95/max %.0f/%.0f attempts",
                                summary.completionObservation.p95, summary.completionObservation.maximum,
                                summary.fenceAge.p95, summary.fenceAge.maximum);
                    ImGui::Text("Pending high-water %u / 8 | missing GPU %llu | unmatched late GPU %llu",
                                summary.pendingSlotsHighWater, summary.missingGpuSamples,
                                presentTelemetry.unmatchedGpuTimingSamples);
                }
            }
            ImGui::TextDisabled("CPU call timing and fence completion do not measure scanout or prove displayed frames.");
            if (!presentTelemetry.failure.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Failure: %s",
                                   presentTelemetry.failure.c_str());
            else if (!presentTelemetry.active)
                ImGui::TextDisabled("Compatibility detail: %s",
                                   presentTelemetry.fallbackReason.empty()
                                       ? "waiting for the first safe successful frame"
                                       : presentTelemetry.fallbackReason.c_str());
        }
        else if (!nrTelemetry.running && !vulkan)
        {
            const char* reason = nrTelemetry.failureReason;
            const char* vkReason = DlssNr::FailureReasonVk();
            if (reason[0] == 0)
                reason = vkReason;

            if (reason[0] != 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Off for this session: %s.", reason);
            }
            else if (enabled)
                ImGui::TextUnformatted("Waiting for a successful NR evaluation on the requested path.");
        }
        else
        {
            // The cost belongs here rather than only in the upscaler's breakdown: that tooltip needs
            // OptiScaler's own upscaler to have run, and with native DLSS passing through there is
            // nothing in it to hang this off.
            // Either backend's timer. They measure the same thing by different means, and only one
            // of them is running.
            const auto ms = vulkan ? DlssNr::LastGpuTimeVk() : nrTelemetry.totalGpuMs;

            // With "Apply the model" off the pass STILL RUNS (so Hold-frame A/B can toggle its edit on
            // a frozen frame) -- it only outputs the clean frame. So the cost is real, and saying so
            // stops the reading looking like a bug. Enable Neural Rendering off is what zeroes it.
            const char* runSuffix =
                !config->DlssNrApplyModel.value_or_default() ? "  (model running, edit hidden)" : "";

            if (ms.has_value())
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Running%s - %.2f ms per frame%s",
                                   vulkan ? " natively on Vulkan" : "", ms.value(), runSuffix);
            else if (vulkan)
                // Measured but not yet read: the first few frames are still in the query ring.
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Running natively on Vulkan - %llu frames%s",
                                   DlssNr::FramesVk(), runSuffix);
            else
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Running.%s", runSuffix);

            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("The whole pass: the staging copies and the resolve as well as the"
                                  "\nmodel. Timing only the model would flatter the number."
                                  "\n\nCompare it against the frame time at the bottom of this window to"
                                  "\nsee what it is costing you.");
        }

        if (!presentRoute && bridgeTelemetry.observed)
        {
            const auto translatedReason = Neurotic::Translate(bridgeTelemetry.reason);
            const ImVec4 bridgeColor = bridgeTelemetry.stage == BridgeStage::CopyBackComplete
                                           ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f)
                                           : ImVec4(1.0f, 0.72f, 0.25f, 1.0f);
            ImGui::TextColored(bridgeColor, "D3D11 native bridge: %s", translatedReason.c_str());
            ImGui::Text("Bridge handoffs %llu | model builds %llu | evaluations %llu | compositions %llu | copy-backs %llu",
                        bridgeTelemetry.handoffs, bridgeTelemetry.modelCreations,
                        bridgeTelemetry.modelEvaluations, bridgeTelemetry.compositions,
                        bridgeTelemetry.copyBacks);
        }

        }

        };
        ImGui::PushItemWidth(std::clamp(ImGui::GetContentRegionAvail().x - 240.0f * menuResScale,
                                      40.0f * menuResScale, 220.0f * menuResScale));

        const auto mainTuningSliderWidth = [&]
        {
            const auto& style = ImGui::GetStyle();
            const float resetWidth = ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2.0f;
            return StageUi::ResponsiveSliderWidth(ImGui::GetContentRegionAvail().x, menuResScale,
                                                   resetWidth, ImGui::CalcTextSize("(?)").x,
                                                   style.ItemSpacing.x);
        };

        const auto renderResampling = [&]
        {
        if (auto resampling = ScopedCollapsingHeader("Advanced resampling##NrResampling"); resampling.IsHeaderOpen())
        {
        ScopedIndent resamplingIndent {};
        ImGui::BeginDisabled(presentRoute);
        const int scalePercent = StageUi::DisplayPercent(StageUi::ResolutionScale(uiConfig));

        if (!presentRoute && scalePercent > 100)
            ImGui::TextDisabled("Supersampling %.2fx: the model runs ABOVE native, then\n"
                                "is sampled back down. Experimental, and costly -- time grows with the area.",
                                scalePercent / 100.0f);

        HelpMarker("What fraction of the frame the model works at. Cost falls with the square of"
                       "\nthis, so half resolution is roughly a quarter of the time."
                       "\n\nThe frame is never reduced. Only the model's contribution is computed small"
                       "\nand enlarged, so the picture underneath is untouched whatever this says."
                       "\n\nWhat it trades: the shading the model adds is broad and survives enlargement;"
                       "\nthe fine structure it synthesises does not, and softens. Worth having when the"
                       "\npass costs more than you want to pay for the detail it returns."
                       "\n\nThe frame itself stays at full detail whatever this says -- only the"
                       "\nmodel's own work is done small.");

        // Meaningful only when the model runs BELOW the frame's size. At 100% -- and above, where
        // supersampling composites its down-legged answer at native -- the residual collapses to the
        // model's own picture and the two modes are identical, so the control says so by going grey.
        {
            const bool reduced = StageUi::ResolutionScale(uiConfig) < 0.999f;

            if (!reduced)
                ImGui::BeginDisabled();

            static const char* enlargeNames[] = { "Classic", "Matched residual" };
            int enlarge = config->DlssNrTransfer.value_or_default() == 1 ? 1 : 0;

            if (ImGui::Combo("Enlargement", &enlarge, enlargeNames, IM_ARRAYSIZE(enlargeNames)))
                config->DlssNrTransfer = (uint32_t) enlarge;

            if (!reduced)
                ImGui::EndDisabled();

            HelpMarker("How the model's work is brought back up when it ran below the frame's size."
                       "\n\nClassic composes the model's small picture directly against the full-size"
                       "\nframe. Those two disagree by the shrink's blur as well as by the model's edit,"
                       "\nand the composition cannot tell them apart -- it reads the blur as brightness"
                       "\nthe frame has and the model never saw. The lower the model resolution the"
                       "\nlarger that error, and it is the colour shift that shows up at 50%."
                       "\n\nMatched residual carries up only the model's difference and lays it on the"
                       "\nframe's own proxy, so both pictures being compared are full size and the only"
                       "\nthing that came from the small raster is the edit itself."
                       "\n\nNo effect at 100% or above: there is no residual to carry and the two are"
                       "\nidentical (supersampling brings its answer down to frame size before this)."
                           "\n\nFrom hhkbble's multi-pass work on this fork.");
        }
        ImGui::EndDisabled();
        }
        };
        // The ordinary downscaler sits directly below manual resolution.
        if (StageUi::ResolutionScale(uiConfig) > 1.0f)
        {
            ImGui::BeginDisabled(basicOwnsMain);
            static const char* names[] = { "FSR1", "Bicubic", "Catmull-Rom", "Lanczos2", "Lanczos3", "Kaiser2", "Kaiser3", "MAGIC" };
            int downscaler = std::clamp(int(uiConfig.DlssNrScalingDownscaler.value_or_default()), 0, 7);
            if (ImGui::Combo("Downscaler##NrDownscaler", &downscaler, names, 8))
                config->DlssNrScalingDownscaler = (Scaler) downscaler;
            ImGui::EndDisabled();
        }
        static const char* nrPresetNames[] = { "Default", "Preset 1", "Preset 2", "Preset 3" };
        int preset = (int) config->DlssNrPreset.value_or_default();
        const auto renderModelPreset = [&]()
        {
            ImGui::SetNextItemWidth((std::min)(180.0f * menuResScale,
                (std::max)(110.0f * menuResScale, ImGui::GetContentRegionAvail().x -
                    ImGui::CalcTextSize("Model preset (?)").x - ImGui::GetStyle().ItemSpacing.x * 2.0f)));
            if (ImGui::Combo("Model preset", &preset, nrPresetNames, IM_ARRAYSIZE(nrPresetNames)))
                config->DlssNrPreset = (uint32_t) preset;

        HelpMarker("Default leaves the choice to the model."
                       "\n\nNot the same scale as the super resolution or ray reconstruction presets --"
                       "\nthe same number means something different here."
                       "\n\nI have no idea what this does. Seems like nothing.");
        };

        static const char* nrStyleNames[] = { "Standard", "Natural", "Cinematic" };
        int style = (int) config->DlssNrStyle.value_or_default();

        if (style > 2)
            style = 2;

        const auto renderStyle = [&]()
        {
            ImGui::SetNextItemWidth((std::min)(180.0f * menuResScale,
                (std::max)(110.0f * menuResScale, ImGui::GetContentRegionAvail().x -
                    ImGui::CalcTextSize("Style (?)").x - ImGui::GetStyle().ItemSpacing.x * 2.0f)));
            if (ImGui::Combo("Style", &style, nrStyleNames, IM_ARRAYSIZE(nrStyleNames)))
                config->DlssNrStyle = (uint32_t) style;

        HelpMarker("The model's own processing profiles."
                   "\n\nDefault (standard): the strongest. Boosts local contrast and deepens"
                   "\nlighting, and can oversaturate or look stylised -- most of what reads as"
                   "\n'the model changed my game's look' is this profile."
                   "\n\nNatural: the same detail work with a gentler hand. Keeps skin tones and"
                   "\ntonal balance closer to what the game rendered."
                   "\n\nCinematic: tones down the shine and over-processing for a film-like look."
                   "\n\nRead when the model is built, so a change rebuilds it after a moment. The"
                   "\nnames come from community testing; NVIDIA ships no names in the binaries.");
        };

        if (ImGui::GetContentRegionAvail().x >= 520.0f * menuResScale &&
            ImGui::BeginTable("##NrStylePresetRow", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn(); renderStyle();
            ImGui::TableNextColumn(); renderModelPreset();
            ImGui::EndTable();
        }
        else
        {
            renderStyle();
            renderModelPreset();
        }

        ImGui::SetNextItemWidth(mainTuningSliderWidth());
        if (basicOwnsMain)
        {
            ImGui::BeginDisabled();
            float strength = uiConfig.DlssNrIntensity.value_or_default();
            ImGui::SliderFloat("Model Strength", &strength, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();
        }
        else
            DeferredSlider("Model Strength", &config->DlssNrIntensity, 0.0f, 2.0f, 1.0f);

        ImGui::BeginDisabled(basicOwnsMain);
        float transfer = uiConfig.DlssNrTransferStrength.value_or_default();
        ImGui::SetNextItemWidth(mainTuningSliderWidth());
        if (ImGui::SliderFloat("Detail Strength###Detail strength", &transfer, 0.0f, 2.0f, "%.2f"))
            config->DlssNrTransferStrength = transfer;

        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##detail"))
            config->DlssNrTransferStrength = 1.0f;
        ImGui::EndDisabled();

        HelpMarker("How far the frame moves toward the model's picture."
                       "\n\nThe model's answer is not added to the frame -- it is a complete picture of its"
                       "\nown, rescaled so its luminance sits where the original says it should. This"
                       "\nblends between the two, so both ends are real pictures and everything between"
                       "\nthem is one too."
                       "\n\n0 gives back exactly what the upscaler produced. 1 is the model's picture."
                       "\n\nAbove 1 carries on past it in the same direction, which is not something the"
                       "\nmodel asked for -- use it to see what it is doing, then come back down. This"
                       "\nis the control to push if you want more effect: Model Strength belongs to the model"
                       "\nand it decides what to do with it.");

        float colour = config->DlssNrColourStrength.value_or_default();
        ImGui::SetNextItemWidth(mainTuningSliderWidth());
        if (ImGui::SliderFloat("Colour Strength###Colour strength", &colour, 0.0f, 4.0f, "%.2f"))
            config->DlssNrColourStrength = colour;

        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##colour"))
            config->DlssNrColourStrength = 1.0f;

        HelpMarker("Whether the model's colour arrives with its light."
                       "\n\n0 keeps the game's own hue exactly -- every pixel is the original colour with"
                       "\nonly its brightness carrying the model's verdict. Game-accurate colour, with"
                       "\nthe detail. 1 brings the model's colour as well, in its own hue, clamped into"
                       "\nAP1 so nothing unreachable is asked for."
                       "\n\nThis cannot shift hue on its own: it interpolates between two finished"
                       "\npictures rather than adding a colour difference to one, which is what used to"
                       "\nlet a warm subject come back green."
                       "\n\nAbove 1 it OVER-SATURATES: the colour keeps its hue but grows more vivid,"
                       "\nand rolls off at the edge of what the display can show rather than clipping"
                       "\ninto a flat blown patch. 1 is the model's own colour; push past it for punch.");

        const auto renderProxy = [&]
        {
        // Experimental. 0 off (soft knee), 1 Neutwo + our composition, 2 Neutwo + pure-inverse replace,
        // 3 hybrid+composed, 4 hybrid+replace (identity midtones + unclipped highlights). Always shown.
        static const char* reversibleNames[] = { "Off (soft knee)", "Neutwo proxy + composed",
                                                 "Neutwo proxy + replace", "Hybrid proxy + composed",
                                                 "Hybrid proxy + replace" };
        int reversible = (int) config->DlssNrReversibleMode.value_or_default();
        if (reversible < 0 || reversible > 4)
            reversible = 0;
        if (ImGui::Combo("Reversible proxy", &reversible, reversibleNames,
                         IM_ARRAYSIZE(reversibleNames)))
            config->DlssNrReversibleMode = (uint32_t) reversible;

        HelpMarker("What the model is shown, and how its answer comes back."
                       "\n\nOff (soft knee): the default. It rolls highlights off so hard the model"
                       "\ncannot resolve detail in them -- fine in soft-lit scenes, weak in bright ones."
                       "\n\nNeutwo composed: an unclipped curve so the model sees highlight detail, then"
                       "\neverything above (Detail/Colour strength, highlight guard, palette). It wins in"
                       "\nbright scenes, but the curve compresses MIDTONES too, so in soft-lit content it"
                       "\ncan be worse than Off. It also shifts paper white -- re-check it when you switch."
                       "\n\nHybrid composed: the best of both, and the one to use. Identity in the"
                       "\nmidtones -- as good as Off there -- and the unclipped roll only in the"
                       "\nhighlights, so it recovers the detail Off crushes without giving up the"
                       "\nmidtones Neutwo does. It barely shifts paper white."
                       "\n\nReplace: the raw model straight back through the exact inverse, none of the"
                       "\ncomposition -- no guard, no palette, no strengths. Gorgeous where there are no"
                       "\nbright lights, but they FLASH in motion. A reference, not a daily setting."
                       "\n\nHybrid replace: the raw model like Replace, but on the hybrid curve -- the"
                       "\ndecode is identity in the midtones, so the flashing is confined to genuine"
                       "\nbright highlights instead of everywhere. Most of Replace's detail, far more"
                       "\nstable. If you love the Replace look but the flicker bothers you, use this."
                       "\n\nOff is byte-identical to before.");

        };

        ImGui::SetNextItemWidth(mainTuningSliderWidth());
        DeferredSlider("Local Structure###Local structure", &config->DlssNrLocalStructure, 0.0f, 2.0f, 1.0f);

        ImGui::SetNextItemWidth(mainTuningSliderWidth());
        DeferredSlider("Local Tone###Local tone", &config->DlssNrLocalTone, 0.0f, 2.0f, 1.0f);


        ImGui::SetNextItemWidth(mainTuningSliderWidth());
        DeferredSlider("Skin Structure###Skin structure", &config->DlssNrSkinStructure, -1.0f, 2.0f, -1.0f);

        HelpMarker("-1 means follow local structure, and is the model's own default -- it is not a"
                       "\nstrength of zero. 0 and above set skin independently of the rest of the frame.");

        bool autoMask = config->DlssNrAutoMask.value_or_default();
        if (ImGui::Checkbox("Auto skin mask", &autoMask))
            config->DlssNrAutoMask = autoMask;

        HelpMarker("Lets the model find skin itself rather than treating the frame uniformly.");

        if (auto advanced = ScopedCollapsingHeader("Advanced Settings / Diagnostics##NrAdvanced"); advanced.IsHeaderOpen())
        {
        renderReadouts(true);
        renderResampling();
        if (auto proxy = ScopedCollapsingHeader("Reversible proxy##NrProxy"); proxy.IsHeaderOpen()) renderProxy();

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("Colour##DlssNrColourSection"); ch.IsHeaderOpen())
        {
        ScopedIndent indent {};
        ImGui::Spacing();
        ScopedNestedTextWrap nestedWrap {};

        ImGui::TextDisabled("The model was trained on finished, sRGB-encoded frames. The upscaler's\n"
                            "output is not one: it is linear and open-ended. These decide how it is\n"
                            "mapped into something the model recognises. A frame the game reports as\n"
                            "already tone-mapped is passed over untouched and none of this applies.");

        {
        // Logarithmic, because the useful range is not linear. A quarter to 240: the low end because
        // a frame the game already tone mapped wants roughly 1, the high end because there is no
        // principled ceiling -- this is a divisor on an open-ended linear buffer, and how far up a
        // given game needs to go is a property of that game's exposure, not of anything we can bound.
        // One tester was still improving at 100. A linear slider over that span would spend nine
        // tenths of its travel on values nobody needs and never reach the ones they do.
        // One dropdown, because there is one answer.
        //
        // This was two checkboxes that could both be on, and every attempt to stop that was a patch
        // on a shape that should not have existed. Greying deadlocked -- each disabled the other, so
        // once both were set the only way out was a button the notice never mentioned. Clearing
        // worked but silently undid a setting somebody had made. Both were ways to stop an illegal
        // state being REACHED; a single choice cannot reach it, because there is only one value to
        // be in.
        //
        // Each option also says whether it can actually do anything in THIS game, in colour, so the
        // choice is made on what is available rather than on what sounds best.
        {
            const auto ex = DlssNr::GameExposureStatus();
            const bool vk = DlssNr::IsRunningVk();
            const bool haveExposure = vk ? DlssNr::ExposureOfferedVk() : ex.everOffered;

            const float anchorNow = DlssNr::ExposureScan::BestValue();
            const bool haveAnchor = !DlssNr::ExposureScan::Anchors().empty();

            static const char* sourceNames[] = { "Paper white only", "The game's own exposure",
                                                 "A buffer the scan found" };

            int source = (int) config->DlssNrWhitePointSource.value_or_default();

            if (source < 0 || source > 2)
                source = 0;

            if (ImGui::Combo("White point from", &source, sourceNames, IM_ARRAYSIZE(sourceNames)))
            {
                config->DlssNrWhitePointSource = (uint32_t) source;

                // Nothing else to set. The scan asks the source whether it is wanted, so choosing
                // it here is the whole of switching it on -- there is no second flag to keep in
                // step, and so no way for the two to disagree.
            }

            HelpMarker("Where the number that divides the frame comes from."
                           "\n\nPaper white only -- the slider below and nothing else. Right for a"
                           "\ngame whose exposure never moves, wrong the moment it does: one"
                           "\nconstant cannot serve a cave and a field."
                           "\n\nThe game's own exposure -- read from the texture the game hands"
                           "\nthe upscaler. The best source there is, because it is decided"
                           "\nupstream and nothing this pass does can move it. Not every game"
                           "\nsupplies one."
                           "\n\nA buffer the scan found -- for games that compute an exposure and"
                           "\nnever pass it on. A GUESS: candidates are matched by shape, and in"
                           "\nGTA V the best one tracks the real exposure but at its own scale,"
                           "\nwhich the anchor's ratio cancels. Needs anchoring once, and checking"
                           "\nafterwards.");

            // Availability, in colour, for the option currently chosen.
            if (source == 1)
            {
                if (!vk && ex.seenFrames == 0)
                    ImGui::TextDisabled("Waiting for a frame...");
                else if (!haveExposure)
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                       "This game supplies no exposure -- paper white is in use. Try "
                                       "the scan instead.");
                else if (vk)
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       "This game supplies an exposure and it is being read.");
                else if (ex.exposure > 1e-6f)
                {
                    const float trim =
                        std::clamp(config->DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       "Game exposure %.4f  ->  white point %.2f%s", ex.exposure,
                                       ex.preExposure / ex.exposure * trim,
                                       ex.offeredNow ? "" : "  (held: absent this frame)");
                }
                else
                    ImGui::TextDisabled("Reading the exposure...");
            }
            else if (source == 2)
            {
                // "Nothing found" and "found several, none of them moving" are different states,
                // and this said the first for both. In GTA V the log carried eight candidates while
                // the panel claimed there were none, which reads as the scan being broken when what
                // it actually needs is for the light to change.
                if (anchorNow <= 0.0f)
                {
                    const unsigned int watching = (unsigned int) DlssNr::ExposureScan::Report().size();

                    if (watching == 0)
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                           "Nothing in this game is shaped like an exposure.");
                    else
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                           "Watching %u, none moving yet -- go between light and shade.",
                                           watching);
                }
                else if (!haveAnchor)
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                       "Found one. Set paper white below until the picture looks "
                                       "right, then press Anchor here.");
                // Once anchored, the scan -> white point readout sits above the sliders below; it is
                // not repeated up here.
            }
            else if (haveExposure)
            {
                ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                   "This game supplies an exposure -- the option above would use it.");
            }
        }






        // A measured suggestion for paper white used to sit here and has been withdrawn.
        //
        // It took the 90th percentile of per-tile peak luminance from the untouched frame, which is a
        // statement about scene content rather than about the buffer's scale. In Nioh 3, where the
        // right answer is about 240, it offered 8 -- because most tiles are shadow and the percentile
        // sits wherever most tiles are. The guard meant to catch that compared each tile against the
        // frame's own brightest, which is scale-free and therefore passes on a black screen: the same
        // relative-threshold mistake the white point meter was removed for, made a second time.
        //
        // A wrong number offered confidently is worse than no number, so nothing is offered. What
        // replaces it has to be a measurement of the game's own exposure rather than of its scenery:
        // the exposure texture where a game supplies one, and otherwise the ratio between the
        // scene-referred buffer and the finished frame, which is that exposure by definition.

        // Two controls, not one control with two meanings.
        //
        // These are different quantities. The manual path wants an absolute divisor on an open-ended
        // linear buffer -- Nioh 3 needs about 240 -- and the exposure path wants a multiplier on a
        // number the game already supplied, where 1 is correct and anything far from it says the read
        // is wrong rather than that somebody prefers it.
        //
        // They used to share one stored value, narrowed to 0.25..4 when the toggle was on. That kept
        // a ruinous value unreachable but left two worse problems: moving the slider in one mode
        // silently destroyed the number found in the other, and there was no way back to "just take
        // the game's answer" short of knowing that the number for it was 1. Separate values fix both.
        // Switching modes is now non-destructive in both directions.
        // The trim belongs to both automatic sources, since both end in "the game's number times a
        // little". Only the manual source gets the absolute slider.
        // One slider per source, each remembering its own number.
        //
        // A trim on the game's exposure and a trim on a buffer the scan found are trims on different
        // things, and a value found against one means nothing against the other. Sharing them meant
        // changing source silently carried a number across, so a picture that had been tuned came
        // back wrong for a reason nothing on screen explained.
        //
        // The scan before it is anchored is the exception, and it has to be: anchoring captures an
        // absolute white point, so there must be an absolute slider to set. Showing a trim there
        // asked people to "set paper white below" next to a control that was not paper white.
        const int wpSource = (int) config->DlssNrWhitePointSource.value_or_default();

        // Which anchor row the paper-white slider edits, or -1 for the live unanchored point. Menu-
        // local and not persisted; the anchor block below sets it when a row is clicked. Declared
        // here because both the slider (this block) and the table (below) read it in the same frame.
        static int selectedAnchor = -1;
        auto anchors = DlssNr::ExposureScan::Anchors();
        if (selectedAnchor >= (int) anchors.size())
            selectedAnchor = -1;

        if (wpSource == 2)
        {
            const bool editingRow = selectedAnchor >= 0 && selectedAnchor < (int) anchors.size();

            // The single scan -> white point readout, above the sliders it explains.
            if (!anchors.empty())
            {
                const float liveScan = DlssNr::ExposureScan::BestValue();

                if (liveScan > 0.0f)
                {
                    const float w = DlssNr::ExposureScan::AnchoredWhitePoint(
                        liveScan, config->DlssNrScanInverted.value_or_default(),
                        config->DlssNrScanTrim.value_or_default());

                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       "Scan %.5f  ->  white point %.2f   (%u point%s)", liveScan, w,
                                       (unsigned) anchors.size(), anchors.size() == 1 ? "" : "s");
                }
            }

            // Paper white shows only when there is a point to set: before the first anchor, or when a
            // row is selected to edit. Once points exist and none is selected, the white point is fixed
            // by the anchors and only the trim adjusts the live picture -- so the trim takes the
            // slider's place, the same shape as the game-exposure source.
            const bool showPaperWhite = anchors.empty() || editingRow;

            if (showPaperWhite)
            {
                float pw = editingRow ? anchors[selectedAnchor].white
                                      : config->DlssNrWhitePointScale.value_or_default();

                char lbl[48];
                if (editingRow)
                    snprintf(lbl, sizeof(lbl), "Paper white (editing point %d)", selectedAnchor + 1);
                else
                    snprintf(lbl, sizeof(lbl), "Paper white");

                if (ImGui::SliderFloat(lbl, &pw, 0.25f, 2000.0f, "%.2fx", ImGuiSliderFlags_Logarithmic))
                {
                    if (editingRow)
                    {
                        DlssNr::ExposureScan::AnchorSetWhite(selectedAnchor, pw);
                        config->DlssNrScanAnchors = DlssNr::ExposureScan::SerializeAnchors();
                    }
                    else
                        config->DlssNrWhitePointScale = pw;
                }

                HelpMarker("The white point for the selected calibration point, or -- with no row"
                               "\nselected -- the value the next Anchor press captures."
                               "\n\nSet it until the picture looks right here, then Anchor. Move to very"
                               "\ndifferent light and do it again: two points fix the buffer's real"
                               "\nrelationship and the white point holds between them. Click a row below"
                               "\nto come back and adjust that point; click it again to let go.");
            }

            // The trim multiplies the interpolated result, and in the steady state it is the control
            // that stands in for paper white: adjust it until the picture looks right in the current
            // light, then Anchor bakes that trimmed value into a new point and resets the trim to 1.
            if (!anchors.empty())
            {
                float trim = config->DlssNrScanTrim.value_or_default();

                if (ImGui::SliderFloat("Trim (x the scan)", &trim, 0.25f, 4.0f, "%.2fx",
                                       ImGuiSliderFlags_Logarithmic))
                    config->DlssNrScanTrim = std::clamp(trim, 0.25f, 4.0f);

                ImGui::SameLine();

                if (ImGui::SmallButton("Reset##scantrim"))
                    config->DlssNrScanTrim = 1.0f;

                HelpMarker("A multiplier on the scan's white point, and the control you adjust between"
                               "\nanchor points: dial it until the picture looks right in the current"
                               "\nlight, then press Anchor here -- it captures the trimmed value as a new"
                               "\npoint and resets the trim to 1.");
            }
        }
        else if (wpSource == 1)
        {
            const bool ofScan = false;

            float trim = ofScan ? config->DlssNrScanTrim.value_or_default()
                                : config->DlssNrWhitePointTrim.value_or_default();

            if (ImGui::SliderFloat(ofScan ? "Trim (x the scan)" : "Trim (x the game's exposure)", &trim,
                                   0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic))
            {
                if (ofScan)
                    config->DlssNrScanTrim = std::clamp(trim, 0.25f, 4.0f);
                else
                    config->DlssNrWhitePointTrim = std::clamp(trim, 0.25f, 4.0f);
            }

            ImGui::SameLine();

            // Deliberately always present rather than greyed at 1. The point of it is that the safe
            // value is one click away without having to know what the safe value is.
            if (ImGui::SmallButton("Reset##wptrim"))
            {
                if (ofScan)
                    config->DlssNrScanTrim = 1.0f;
                else
                    config->DlssNrWhitePointTrim = 1.0f;
            }

            HelpMarker("A multiplier on the exposure the game supplied. 1.00x takes its number"
                           "\nexactly, and that is the right answer here."
                           "\n\nThis is not a fudge factor. If a game needs the trim far from 1 to look"
                           "\nright, that is evidence the exposure being read is wrong for that game,"
                           "\nnot that the game wants trimming. Somewhere around 0.8 to 1.25 is honest"
                           "\ntuning; reaching for 4 means something upstream is broken and the trim is"
                           "\nhiding it."
                           "\n\nYour manual paper white is kept separately and comes back untouched if"
                           "\nyou switch the option above off.");
        }
        else
        {
            // Logarithmic, because the useful range is not linear. A quarter to 2000: the low end
            // because a frame the game already tone mapped wants roughly 1, the high end because
            // there is no principled ceiling -- this is a divisor on an open-ended linear buffer, and
            // how far up a given game needs to go is a property of that game's exposure rather than
            // of anything that can be bounded here. One tester was still improving at 100.
            float wpScale = config->DlssNrWhitePointScale.value_or_default();

            if (ImGui::SliderFloat("Paper white", &wpScale, 0.25f, 2000.0f, "%.2fx",
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrWhitePointScale = wpScale;

        HelpMarker("What the frame is divided by before the model sees it. There is no other white"
                       "\npoint; this is the whole of it."
                       "\n\nThe model was trained on finished frames where white sits at 1. The"
                       "\nupscaler's output is linear and open-ended, so something has to say where"
                       "\nwhite is -- and where the game's DLSS buffer is linear HDR, that number is"
                       "\nrarely anywhere near 1. Measured in Monster Hunter Wilds it takes 16 or more"
                       "\nbefore the model's detail reaches the frame at all, and the value that suits"
                       "\na shaded camp is still too small for the same game out in daylight."
                       "\n\nToo low and almost every pixel trips the soft knee: the model is shown a"
                       "\nflat near-white picture, its answer is scaled away, and only its hue"
                       "\nsurvives -- which reads as a colour cast rather than as lost detail. Too"
                       "\nhigh and it is shown an underexposed one, its answer degrades, and this same"
                       "\nnumber multiplies that error on the way out."
                       "\n\nRaise it until the picture stops improving. Past that point it does not"
                       "\nplateau, it gets worse in the other direction."
                       "\n\nThis was once a multiplier on a measured white point. The measurement is"
                       "\ngone: it read scene brightness rather than where white belongs, handed the"
                       "\nmodel a picture three times too dark, and left the highlight path nothing to"
                       "\ngive back."
                       "\n\nAt strength zero the frame is still bit-identical whatever this says.");
        }

        // Highlight guard, directly under the white point / trim -- it bounds the model's edit and
        // belongs with the exposure controls it works alongside.
        float maxRatio = config->DlssNrMaxRatio.value_or_default();
        if (ImGui::SliderFloat("Highlight guard", &maxRatio, 1.0f, 8.0f, "%.1fx"))
            config->DlssNrMaxRatio = maxRatio;

        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##guard"))
            config->DlssNrMaxRatio = 2.0f;

        HelpMarker("The most the pass may move any pixel, as a multiple of what it already was, in"
                       "\nboth directions -- a pixel may not be brightened past this nor darkened past"
                       "\nits reciprocal. Lights are where the model has least to say and rescaling its"
                       "\nanswer does the most damage; 2x leaves detail intact while stopping a strip"
                       "\nlight turning into a string of coloured cells. Raise it only if bright areas"
                       "\nlook clipped.");

        // Directly under the white point, because that is the number it moves and the number the
        // anchor captures. It used to sit under Inspect, a whole section away from the slider it
        // reads, which left "Anchor here" looking like a control for something else entirely.
        {
            // No checkbox here any more.
            //
            // The dropdown above says whether the scan is the white point's source, and that is
            // the only reason anybody using this would want it running. A second control could
            // only agree with the dropdown or contradict it, and both were on offer: it began as
            // a redundant question and became a way to switch off the thing the chosen source
            // depended on.
            //
            // The ini key survives as a developer override for the one case a user has no reason
            // to want -- running the scan in a game that supplies a REAL exposure, so the log can
            // compare the two. That is validation, and validation does not need a widget.
            //
            // Worth keeping written down, since the panel no longer says it: the scan matches
            // buffers by SHAPE, and shape is a weak filter. In GTA V -- a game that supplies a
            // real exposure, so the right answer sat visible beside it -- the best candidate was
            // a 1x1 R32_FLOAT that climbed in a straight line for seventeen minutes while the
            // true exposure held still. Their ratio moved 14x. That is an accumulator, not an
            // eye adaptation.

                // Only where it means something. The lamp reads the scan, so offering it beside a
                // white point that comes from the game's own exposure is offering a control that
                // cannot light up.
                bool meter = config->DlssNrScanMeter.value_or_default();

                if (config->DlssNrWhitePointSource.value_or_default() == 2 &&
                    ImGui::Checkbox("Show the light meter on screen", &meter))
                    config->DlssNrScanMeter = meter;

                HelpMarker("A lamp in the corner: red for dark, green for full light, and the"
                               "\nshades between, with the reading beside it."
                               "\n\nIt is how you see at a glance that the scan is TRACKING rather"
                               "\nthan merely running. Walk into shade and it should slide toward"
                               "\nred; step out and it should go green. If it moves the wrong way,"
                               "\nthat is what the setting above is for."
                               "\n\nPurely a readout. It changes nothing.");

            // Shown when the scan is actually running, whichever way it got switched on.
            if (DlssNr::ExposureScan::Scanning())
            {
                // Anchoring: one press, then it never needs touching again.
                //
                // The absolute white point cannot come out of a buffer whose units are unknown.
                // Every value AFTER the first can: only the ratio against the anchor is used, so
                // whatever the number means, it cancels. That is why this is a button and not a
                // measurement -- the one thing a person can supply that no amount of cleverness
                // can is "this looks right to me".
                int which = 0;
                float low = 0.0f, high = 0.0f;
                const float live = DlssNr::ExposureScan::BestValue(&which, &low, &high);

                const bool isSource = config->DlssNrWhitePointSource.value_or_default() == 2;

                // Anchor captures (currentScan, currentPaperWhite) and ADDS a row -- it does not
                // replace. One row is the old single-anchor ratio law; add a second in different
                // light and the white point is interpolated between the points, so it holds across
                // the whole range instead of only near one anchor. Greyed unless the scan is the
                // chosen source and it currently has a value to capture.
                ImGui::BeginDisabled(live <= 0.0f || !isSource);

                if (ImGui::Button("Anchor here"))
                {
                    // What to capture. Before the first point, the paper white above (an absolute value
                    // with the wide range a fresh game needs). After that, the EFFECTIVE white point the
                    // picture is showing right now -- the interpolated value times the Trim the user just
                    // dialed in -- so a second point in different light captures the trimmed look, not a
                    // frozen paper white (which would make two equal whites and a flat, non-tracking
                    // curve). The trim is reset afterwards: the new point, which the picture now passes
                    // through exactly, must not be multiplied by it a second time.
                    const float captureWhite =
                        anchors.empty()
                            ? std::max(0.01f, config->DlssNrWhitePointScale.value_or_default())
                            : std::max(0.01f, DlssNr::ExposureScan::AnchoredWhitePoint(
                                                  live, config->DlssNrScanInverted.value_or_default(),
                                                  config->DlssNrScanTrim.value_or_default()));

                    if (DlssNr::ExposureScan::AnchorAdd(live, captureWhite))
                    {
                        config->DlssNrScanAnchors = DlssNr::ExposureScan::SerializeAnchors();
                        config->DlssNrScanTrim = 1.0f;
                        selectedAnchor = -1;
                    }
                }

                ImGui::EndDisabled();

                HelpMarker("Make the picture look right, then press this -- it captures the current look"
                               "\nas a point. For the first point use the Paper white slider above; for"
                               "\nevery point after, move to different light and use the Trim, which the"
                               "\nAnchor then bakes into a new point."
                               "\n\nThe first press calibrates one point -- the white point then"
                               "\nfollows the scan by ratio from there, as before. Walk into very"
                               "\ndifferent light, set paper white again, and press it again: the"
                               "\nsecond point pins down the buffer's real curve and everything"
                               "\nbetween the two is right, not just near one anchor. Up to eight."
                               "\n\nThe table is per game and shareable: one person calibrates a game"
                               "\nand the numbers are the same for everyone who takes the profile.");

                if (!isSource)
                    ImGui::TextDisabled("(the scan is only watching -- the white point above comes "
                                        "from somewhere else)");

                if (!anchors.empty())
                {
                    // The row nearest the live scan value (in log space) is the one driving the
                    // picture right now; mark it so the user can see which calibration is in effect.
                    int active = 0;
                    float bestDist = 1e30f;
                    const float liveLog = std::log(std::max(live, 1e-6f));

                    for (size_t i = 0; i < anchors.size(); ++i)
                    {
                        const float d =
                            std::fabs(std::log(std::max(anchors[i].scan, 1e-6f)) - liveLog);
                        if (d < bestDist)
                        {
                            bestDist = d;
                            active = (int) i;
                        }
                    }

                    for (size_t i = 0; i < anchors.size(); ++i)
                    {
                        ImGui::PushID((int) i);

                        // Delete first, so its click is never swallowed by the row-wide Selectable.
                        if (ImGui::SmallButton("x"))
                        {
                            DlssNr::ExposureScan::AnchorRemove((int) i);
                            config->DlssNrScanAnchors = DlssNr::ExposureScan::SerializeAnchors();
                            if (selectedAnchor == (int) i)
                                selectedAnchor = -1;
                            else if (selectedAnchor > (int) i)
                                --selectedAnchor;
                            ImGui::PopID();
                            continue;
                        }

                        ImGui::SameLine();

                        const bool sel = (int) i == selectedAnchor;
                        char row[96];
                        snprintf(row, sizeof(row), "%s scan %.4f  ->  white %.2f%s",
                                 ((int) i == active && isSource) ? ">" : "  ", anchors[i].scan,
                                 anchors[i].white, sel ? "   [editing]" : "");

                        // Click selects the row (slider edits it); click again deselects (slider
                        // returns to the live unanchored point).
                        if (ImGui::Selectable(row, sel))
                            selectedAnchor = sel ? -1 : (int) i;

                        ImGui::PopID();
                    }

                    ImGui::TextDisabled("Click a row to edit it with the slider above; click it again"
                                        " to control the live point. > is the point in use now.");
                }

                // The direction flag only means anything with a single point; with two or more the
                // direction the white point moves is already fixed by the data.
                if (anchors.size() == 1)
                {
                    bool inverted = config->DlssNrScanInverted.value_or_default();
                    if (ImGui::Checkbox("The number runs the other way", &inverted))
                        config->DlssNrScanInverted = inverted;

                    HelpMarker("Flip this if the picture gets worse in the direction it should be"
                                   "\ngetting better. Most engines store an exposure that falls as"
                                   "\nthe scene brightens; some store its reciprocal, and a buffer"
                                   "\nfound by shape does not say which. Add a second anchor point in"
                                   "\ndifferent light and this is decided for you, so it disappears.");
                }

                // The scan -> white point readout is shown above the sliders now, not here.

                // Everything below is read-out rather than control: what the scan is looking at and
                // how to tell whether it found the right thing. Folded away because the two decisions
                // that matter -- anchor, and which way the number runs -- are above it.
                if (ImGui::TreeNode("Advanced"))
                {

                    const auto found = DlssNr::ExposureScan::Report();
                    const char* why = DlssNr::ExposureScan::Status();

                    if (found.empty())
                    {
                        ImGui::TextDisabled("%s", why != nullptr && why[0] != 0
                                                      ? why
                                                      : "nothing matched yet.");
                    }
                    else
                    {
                        for (size_t i = 0; i < found.size(); ++i)
                        {
                            const auto& c = found[i];

                            if (c.reads == 0)
                            {
                                ImGui::TextDisabled("%zu. %s -- not read yet", i + 1, c.shape.c_str());
                                continue;
                            }

                            // Moving is the whole signal, so it is the thing that is coloured.
                            ImGui::TextColored(c.moves ? ImVec4(0.45f, 0.8f, 0.45f, 1.0f)
                                                       : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                               "%zu. %s = %.5f  (seen %.5f..%.5f) %s", i + 1,
                                               c.shape.c_str(), c.latest, c.lowest, c.highest,
                                               c.moves ? "MOVES" : "flat so far");
                        }

                        ImGui::TextDisabled("Walk from shade into daylight. A real exposure moves.");
                        ImGui::TextDisabled("One that only ever climbs is a counter, not an exposure.");
                    }

                    ImGui::TreePop();
                }
            }
        }


        }
        }

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("Compare##DlssNrCompareSection"); ch.IsHeaderOpen())
        {
        ScopedIndent indent {};
        ImGui::Spacing();
        ScopedNestedTextWrap nestedWrap {};

        // Freeze the frame the model works on, so a setting change re-renders it in place -- the only
        // clean way to A/B our own settings (a moving scene confounds every other comparison). See
        // design/frame-hold.md.
        bool held = config->DlssNrHoldFrame.value_or_default();
        const bool unsupportedHold = State::Instance().api == API::Vulkan;
        ImGui::BeginDisabled(unsupportedHold);
        if (unsupportedHold) held = false;
        if (ImGui::Checkbox("Hold frame", &held))
            config->DlssNrHoldFrame = held;
        ImGui::EndDisabled();

        HelpMarker(unsupportedHold ? "Hold frame is not implemented on native Vulkan. It has no effect here."
                                  : "Freezes the frame the model works on. While held, change paper white, the"
                       "\nstrengths, the reversible mode, the model preset -- anything below the"
                       "\nupscaler -- and only that setting moves; the scene does not."
                       "\n\nWhat it CANNOT show: DLSS/FSR/XeSS upscaler presets or anything upstream"
                       "\n(the upscaler is not re-run on a held frame), and the game's own HUD and"
                       "\npost-processing, which run after this pass and keep updating. The white"
                       "\npoint stops being measured and holds its value while frozen, so it cannot"
                       "\ndrift and confound the comparison."
                       "\n\nHide the menu and it stays held. Untoggle to resume.");

        static const char* compareNames[] = { "Off", "Side by side", "Wipe" };
        int compare = (int) config->DlssNrCompare.value_or_default();
        if (ImGui::Combo("Compare", &compare, compareNames, IM_ARRAYSIZE(compareNames)))
            config->DlssNrCompare = (uint32_t) compare;

        HelpMarker("Shows the pass against itself, so the two can be seen at once rather than"
                       "\ntoggled and remembered."
                       "\n\nSide by side puts the whole frame in each half, untouched on the left and"
                       "\nedited on the right. Both halves are squeezed horizontally to fit, so it is"
                       "\nfor looking at rather than playing in."
                       "\n\nWipe cuts a single frame at the split and resamples nothing, so the picture"
                       "\nis the right shape and can be played normally. Drag the split below; it is a"
                       "\nstored setting and stays put once the menu is closed."
                       "\n\nNeither needs the menu open to keep working. A hairline marks the join.");

        if (compare != 0)
        {
            bool swap = config->DlssNrCompareSwap.value_or_default();
            if (ImGui::Checkbox("Swap sides", &swap))
                config->DlssNrCompareSwap = swap;

            bool tags = config->DlssNrCompareTags.value_or_default();
            if (ImGui::Checkbox("Label the sides", &tags))
                config->DlssNrCompareTags = tags;

            HelpMarker("Writes which side is which onto the frame itself, so a screenshot still"
                           "\nsays so after it has left this machine. Drawn into the picture's own"
                           "\nplane: in the wipe the split reveals and hides the label exactly as it"
                           "\ndoes the images, and there is nothing to drag. Swap sides moves the"
                           "\nlabels with their pictures.");

            if (tags)
            {
                float tagScale = config->DlssNrTagScale.value_or_default();
                if (ImGui::SliderFloat("Label size", &tagScale, 0.5f, 5.0f, "%.1fx"))
                    config->DlssNrTagScale = std::clamp(tagScale, 0.5f, 5.0f);
            }

            HelpMarker("Puts the edited frame on the other side."
                           "\n\nWorth doing once you have decided which you prefer: the eye is not"
                           "\neven-handed about left and right, and a difference can read as an"
                           "\nimprovement purely from where it sits. If the same side still wins after"
                           "\nswapping, it is the pass you are seeing and not the placement.");
        }

        if (compare == 1)
        {
            float zoom = config->DlssNrCompareZoom.value_or_default();
            if (ImGui::SliderFloat("Zoom", &zoom, 1.0f, 2.0f, "%.2f"))
                config->DlssNrCompareZoom = std::clamp(zoom, 1.0f, 2.0f);

            HelpMarker("How much of the frame each half shows."
                           "\n\nA half is half as wide as the frame and just as tall, so the frame"
                           "\ncannot fill it and keep its shape."
                           "\n\nAt 1 the whole frame is there at its right proportions, with bars above"
                           "\nand below. At 2 the half is filled and the sides are cropped away"
                           "\ninstead. Anything between trades one for the other.");
        }

        if (compare == 2)
        {
            float split = config->DlssNrCompareSplit.value_or_default();
            if (ImGui::SliderFloat("Split", &split, 0.0f, 1.0f, "%.2f"))
                config->DlssNrCompareSplit = std::clamp(split, 0.0f, 1.0f);

            HelpMarker("Where the wipe cuts. Left of it is the frame as the upscaler produced it,"
                           "\nright of it is the frame the model edited.");
        }

        static const char* debugNames[] = { "Off", "Proxy (what the model sees)", "Model output (raw)",
                                            "Difference (amplified)" };
        int debugView = (int) config->DlssNrDebugView.value_or_default();
        if (ImGui::Combo("Debug view", &debugView, debugNames, IM_ARRAYSIZE(debugNames)))
            config->DlssNrDebugView = (uint32_t) debugView;

        HelpMarker("Proxy is the picture handed to the model -- if that looks wrong, the white point"
                       "\nis wrong and nothing downstream can be judged."
                       "\n\nDifference shows what the model actually changed, amplified twenty times and"
                       "\ncentred on grey. A flat grey frame there means it is doing nothing.");
        }

        }
        ImGui::PopItemWidth();
        ImGui::PopTextWrapPos();
    }
    }

    // Multipass belongs to the same Neural Rendering page, but stays independently collapsible so
    // the primary first-pass controls remain easy to scan.
    RenderMultipassMenu(config, menuResScale);
}

struct PassOptionRefs
{
    NrOptional<float>* workingScale;
    NrOptional<Scaler>* scalingDownscaler;
    NrOptional<uint32_t>* transfer;
    NrOptional<uint32_t>* preset;
    NrOptional<float>* intensity;
    NrOptional<uint32_t>* style;
    NrOptional<float>* localStructure;
    NrOptional<float>* localTone;
    NrOptional<float>* skinStructure;
    NrOptional<bool>* autoMask;
    NrOptional<float>* transferStrength;
    NrOptional<float>* colourStrength;
    NrOptional<float>* maxRatio;
    NrOptional<uint32_t>* reversibleMode;
    NrOptional<bool>* applyModel;
};

static PassOptionRefs PassOptions(Config* config, unsigned int pass)
{
    if (pass == 0)
        return { &config->DlssNrWorkingScale, &config->DlssNrScalingDownscaler,
                 &config->DlssNrTransfer, &config->DlssNrPreset, &config->DlssNrIntensity,
                 &config->DlssNrStyle, &config->DlssNrLocalStructure, &config->DlssNrLocalTone,
                 &config->DlssNrSkinStructure, &config->DlssNrAutoMask,
                 &config->DlssNrTransferStrength, &config->DlssNrColourStrength,
                 &config->DlssNrMaxRatio, &config->DlssNrReversibleMode,
                 &config->DlssNrApplyModel };
    if (pass == 1)
        return { &config->DlssNrSecondLayerWorkingScale,
                 &config->DlssNrSecondLayerScalingDownscaler,
                 &config->DlssNrSecondLayerTransfer, &config->DlssNrSecondLayerPreset,
                 &config->DlssNrSecondLayerIntensity, &config->DlssNrSecondLayerStyle,
                 &config->DlssNrSecondLayerLocalStructure, &config->DlssNrSecondLayerLocalTone,
                 &config->DlssNrSecondLayerSkinStructure, &config->DlssNrSecondLayerAutoMask,
                 &config->DlssNrSecondLayerTransferStrength,
                 &config->DlssNrSecondLayerColourStrength, &config->DlssNrSecondLayerMaxRatio,
                 &config->DlssNrSecondLayerReversibleMode,
                 &config->DlssNrSecondLayerApplyModel };

    auto& layer = config->DlssNrExtraLayers.values[pass - 2];
    return { &layer.workingScale, &layer.scalingDownscaler, &layer.transfer, &layer.preset,
             &layer.intensity, &layer.style, &layer.localStructure, &layer.localTone,
             &layer.skinStructure, &layer.autoMask, &layer.transferStrength,
             &layer.colourStrength, &layer.maxRatio, &layer.reversibleMode, &layer.applyModel };
}

static void ResetPassOptions(const PassOptionRefs& pass)
{
    NrConfigSynchronization::Transaction transaction;
    *pass.workingScale = 1.0f;
    *pass.scalingDownscaler = Scaler::Lanczos3;
    *pass.transfer = 1u;
    *pass.preset = 0u;
    *pass.intensity = 1.0f;
    *pass.style = 0u;
    *pass.localStructure = 1.0f;
    *pass.localTone = 1.0f;
    *pass.skinStructure = -1.0f;
    *pass.autoMask = true;
    *pass.transferStrength = 1.0f;
    *pass.colourStrength = 1.0f;
    *pass.maxRatio = 2.0f;
    *pass.reversibleMode = 0u;
    *pass.applyModel = true;
}

static void CopyPassOptions(const PassOptionRefs& source, const PassOptionRefs& destination)
{
    NrConfigSynchronization::Transaction transaction;
    *destination.workingScale = source.workingScale->value_or_default();
    *destination.scalingDownscaler = source.scalingDownscaler->value_or_default();
    *destination.transfer = source.transfer->value_or_default();
    *destination.preset = source.preset->value_or_default();
    *destination.intensity = source.intensity->value_or_default();
    *destination.style = source.style->value_or_default();
    *destination.localStructure = source.localStructure->value_or_default();
    *destination.localTone = source.localTone->value_or_default();
    *destination.skinStructure = source.skinStructure->value_or_default();
    *destination.autoMask = source.autoMask->value_or_default();
    *destination.transferStrength = source.transferStrength->value_or_default();
    *destination.colourStrength = source.colourStrength->value_or_default();
    *destination.maxRatio = source.maxRatio->value_or_default();
    *destination.reversibleMode = source.reversibleMode->value_or_default();
    *destination.applyModel = source.applyModel->value_or_default();
}

static void ResetButton(const char* id, const std::function<void()>& reset)
{
    ImGui::SameLine();
    if (ImGui::SmallButton(id)) reset();
}

static void RenderMultipassMenu(Config* config, float menuResScale)
{
    ImGui::Spacing();
    if (auto panel = ScopedCollapsingHeader("Neural Rendering Multipass##DlssNrMultipassSection");
        panel.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ScopedNestedTextWrap wrap {};
        const bool d3d12 = !IsVulkanInput() && State::Instance().api == API::DX12;
        const bool presentRoute = config->DlssNrRoute.value_or_default() != 0;
        static unsigned int previousPassCount = 0;

        auto basic = BasicMultipass::Normalize(config->DlssNrBasicMultipass.value_or_default());
        int editor = basic.advanced ? 1 : 0;
        static const char* editors[] = { "Basic", "Advanced" };
        if (StageUi::SentenceCombo("##NrMultipassEditor", "", "Neural Rendering Settings", &editor, editors, 2))
        {
            BasicMultipass::Update(*config, [&](auto& p) { p.advanced = editor == 1; });
            CancelNrEdits();
            basic = config->DlssNrBasicMultipass.value_or_default();
        }

        bool enabled = config->DlssNrMultipassEnabled.value_or_default();
        if (!d3d12 && !presentRoute) ImGui::BeginDisabled();
        if (MenuControls::EmphasizedCheckbox("Enable NR Multipass", &enabled))
        {
            NrConfigSynchronization::Transaction transaction;
            config->DlssNrMultipassEnabled = enabled;
            config->DlssNrSecondLayer = enabled && config->DlssNrPasses.value_or_default() > 1;
            CancelNrEdits();
        }
        if (!d3d12 && !presentRoute) ImGui::EndDisabled();
        HelpMarker("Enables a bounded chain of one to ten Neural Rendering passes on D3D12. Each later pass consumes the fully composed image from the preceding pass and owns an independent model session and temporal history. Cost increases approximately linearly with the selected pass count.");

        if (!basic.advanced)
        {
            const auto slider = [&](const char* title, const char* id, NrOptional<float>& preview,
                                    float BasicMultipass::Profile::* member, float minimum, float maximum,
                                    const char* hint = nullptr, float preferredWidth = 0.0f,
                                    unsigned int cumulativePasses = 0)
            {
                BasicMultipass::Profile original;
                uint64_t generation;
                {
                    NrConfigSynchronization::Transaction transaction;
                    original = config->DlssNrBasicMultipass.value_or_default();
                    generation = NrConfigSynchronization::ProfileGeneration();
                }
                preview = original.*member;
                ImGui::TextUnformatted(title);
                if (hint) HelpMarker(hint);
                const auto& style = ImGui::GetStyle();
                const float resetWidth = ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2.0f;
                const float available = ImGui::GetContentRegionAvail().x;
                const float sliderWidth = cumulativePasses > 0 ?
                    MenuControls::ResponsiveCumulativeStrengthWidth(available, resetWidth,
                        style.ItemSpacing.x, cumulativePasses) :
                    (preferredWidth > 0.0f ?
                        MenuControls::ResponsiveBasicResolutionWidth(available, menuResScale,
                            resetWidth, style.ItemSpacing.x, preferredWidth) :
                        (std::max)(1.0f, available - resetWidth - style.ItemSpacing.x));
                ImGui::SetNextItemWidth(sliderWidth);
                ImVec2 sliderMin {}, sliderMax {};
                const bool changed = DeferredNrSlider(id, { &preview }, minimum, maximum, 1.0f,
                    "%d%%", true, cumulativePasses > 0 ? &sliderMin : nullptr,
                    cumulativePasses > 0 ? &sliderMax : nullptr);
                if (cumulativePasses > 0)
                    DrawCumulativePassSegments(sliderMin, sliderMax, cumulativePasses, menuResScale);
                if (changed)
                    if (!BasicMultipass::CommitEdit(*config, original, generation, member, preview.value_or_default()))
                        CancelNrEdits();
            };
            static NrOptional<float> resolution { 1.0f }, model { 1.0f }, detail { 1.0f };
            slider("Shared Model Resolution (All Passes)", "##NrBasicResolution", resolution,
                   &BasicMultipass::Profile::resolution, 0.25f, 2.0f,
                   "Sets one model-raster percentage for Pass 1 and every active additional pass "
                   "in Basic mode. The composed frame remains full resolution. Releasing commits "
                   "the shared value and rebuilds changed models; disabling Multipass restores the "
                   "saved main settings.", 320.0f);
            basic = config->DlssNrBasicMultipass.value_or_default();
            if (basic.resolution > 1.0f)
            {
                static const char* downscalers[] = { "FSR1", "Bicubic", "Catmull-Rom", "Lanczos2", "Lanczos3", "Kaiser2", "Kaiser3", "MAGIC" };
                int selected = int(basic.downscaler);
                if (ImGui::Combo("Downscaler##NrBasicDownscaler", &selected, downscalers, 8))
                    BasicMultipass::Update(*config, [&](auto& p) { p.downscaler = uint32_t(selected); });
            }
            static const char* maximums[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };
            int maximum = int(basic.maximum) - 1;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
            if (ImGui::Combo("Maximum passes##NrBasicMaximum", &maximum, maximums, 10))
            {
                BasicMultipass::Update(*config, [&](auto& p) { p.maximum = uint32_t(maximum + 1); });
                CancelNrEdits();
            }
            basic = config->DlssNrBasicMultipass.value_or_default();
            const char* cumulativeHint =
                "Each coloured segment represents one pass. The slider grows from one-quarter width "
                "at one maximum pass to full width at four. Five to ten passes keep the full width "
                "and add denser pass segments. Cumulative totals fill passes from left to right.";
            slider("Model Strength", "##NrBasicModel", model, &BasicMultipass::Profile::model,
                   0.0f, float(basic.maximum), cumulativeHint, 0.0f, basic.maximum);
            slider("Detail Strength", "##NrBasicDetail", detail, &BasicMultipass::Profile::detail,
                   0.0f, float(basic.maximum), cumulativeHint, 0.0f, basic.maximum);
            basic = config->DlssNrBasicMultipass.value_or_default();
            const auto requested = BasicMultipass::Count(basic);
            const auto telemetry = DlssNr::Telemetry();
            static MenuStatus::SelectionObservation completedObservation;
            const uint64_t key = uint64_t(std::lround(basic.model * 100)) |
                (uint64_t(std::lround(basic.detail * 100)) << 12) |
                (uint64_t(StageUi::DisplayPercent(basic.resolution)) << 24) |
                (uint64_t(enabled) << 36) | (uint64_t(config->DlssNrRoute.value_or_default()) << 37);
            const bool fresh = completedObservation.Fresh(key, telemetry.completedPipelineEvaluations);
            if (enabled && requested == 0)
                ImGui::TextWrapped("0 passes requested. Effect bypassed; loaded resources retained.");
            else if (enabled && fresh && telemetry.running)
                ImGui::Text("%u requested | %u completed on the last frame", requested, telemetry.layerCount);
            else
                ImGui::Text("%u requested | completed: unavailable", requested);
            ImGui::TextWrapped("Totals include Pass 1. 230%% means 100%% + 100%% + 30%%. Remaining settings inherit the main section.");
            if (!d3d12 && !presentRoute)
                ImGui::TextWrapped("Multipass requires D3D12; native Vulkan keeps its saved single-pass settings.");
            return;
        }

        const unsigned int passCount = RenderPassCountSelector(config);
        if (previousPassCount != passCount) CancelNrEdits();
        previousPassCount = passCount;

        if (ImGui::Button("Reset All")) ImGui::OpenPopup("Reset all multipass settings?");
        HelpMarker("Restores the Multipass switch and every setting in all nine saved additional pass profiles to "
                   "their shipped defaults. Baseline Pass 1 and the shared pass count above remain unchanged. "
                   "A confirmation is required.");
        if (ImGui::BeginPopupModal("Reset all multipass settings?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Reset every additional Neural Rendering pass profile to default?");
            if (ImGui::Button("Confirm"))
            {
                {
                    NrConfigSynchronization::Transaction transaction;
                    enabled = false;
                    config->DlssNrMultipassEnabled = false;
                    config->DlssNrSecondLayer = false;
                    for (unsigned int pass = 1; pass < 10; ++pass)
                        ResetPassOptions(PassOptions(config, pass));
                }
                CancelNrEdits();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (!d3d12 && !presentRoute)
            ImGui::TextDisabled("Neural Rendering Multipass requires D3D12; Vulkan remains single-pass.");

        const auto telemetry = DlssNr::Telemetry();
        if (enabled && d3d12)
        {
            if (telemetry.running)
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%u of %u requested passes completed on the last frame.",
                                   telemetry.layerCount, passCount);
            else
                ImGui::TextDisabled("The pass chain becomes active when DLSS Neural Rendering is enabled and ready.");
            if (passCount >= 4)
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                                   "High pass counts are experimental and may exhaust GPU memory or frame time.");
        }

        if (passCount > 1)
        {
            const auto sharedSlider = [&](const char* label, NrOptional<float>* PassOptionRefs::* member,
                                          float minimum, float maximum, const char* hint, const char* mixedHint)
            {
                std::vector<NrOptional<float>*> targets;
                for (unsigned int index = 1; index < passCount; ++index)
                    targets.push_back(PassOptions(config, index).*member);
                DeferredNrSlider(label, targets, minimum, maximum, 1.0f, "%d%%", true);
                HelpMarker(hint);
                if (pendingNrEdits[label].Mixed()) ImGui::TextDisabled("%s", mixedHint);
            };
            sharedSlider("Global Pass Resolution (Passes 2–N)###Model Resolution##AdditionalPassModelResolution", &PassOptionRefs::workingScale, 0.25f, 2.0f,
                "Changes the Model resolution for every additional pass at once: Pass 2 through the selected final pass. It never changes Pass 1. Dragging previews the shared percentage; releasing commits that percentage to all additional passes and rebuilds them once.",
                "Passes 2–N have mixed model resolutions; adjusting this slider applies one value to all of them.");
            sharedSlider("Global Pass Model Strength (Passes 2–N)###Model Strength##AdditionalPassModelStrength", &PassOptionRefs::intensity, 0.0f, 2.0f,
                "Sets internal model intensity for Pass 2 through the selected final pass. Release to apply and rebuild only changed child models. 100% is default; 0% does not disable model execution. Pass 1 is unchanged.",
                "Passes 2–N have mixed model strengths; adjusting this slider applies one value to all of them.");
            sharedSlider("Global Pass Detail Strength (Passes 2–N)###Detail Strength##AdditionalPassDetailStrength", &PassOptionRefs::transferStrength, 0.0f, 2.0f,
                "Sets detail blending for Pass 2 through the selected final pass. Release to apply without rebuilding models. 100% is default; 0% hides the detail edit. Pass 1 and colour strength are unchanged.",
                "Passes 2–N have mixed detail strengths; adjusting this slider applies one value to all of them.");
        }
        else
        {
            ImGui::TextDisabled("Select two or more passes to adjust shared Model Resolution, Model Strength and Detail Strength.");
        }

        if (passCount == 1)
            ImGui::TextDisabled("Pass 1 is configured in the main Neural Rendering section. Select two or more passes above to configure additional passes here.");
        else if (ImGui::BeginTabBar("NrMultipassLayers", ImGuiTabBarFlags_FittingPolicyScroll))
        {
            static unsigned int selectedPass = 1;
            if (selectedPass < 1 || selectedPass >= passCount) selectedPass = 1;
            for (unsigned int index = 1; index < passCount; ++index)
            {
                char label[24] {};
                snprintf(label, sizeof(label), "Pass %u", index + 1);
                if (!ImGui::BeginTabItem(label)) continue;
                selectedPass = index;
                const auto pass = PassOptions(config, index);
                char scaleLabel[96] {};
                snprintf(scaleLabel, sizeof(scaleLabel), "Model resolution##pass%u", index + 1);

                char resetPopup[64] {};
                snprintf(resetPopup, sizeof(resetPopup), "Reset Pass %u profile?##pass%u", index + 1, index + 1);
                if (ImGui::Button("Reset this pass")) ImGui::OpenPopup(resetPopup);
                HelpMarker("Restores only this additional pass profile to its shipped defaults. Baseline Pass 1, "
                           "other additional pass profiles, and the shared pass count remain unchanged.");
                if (ImGui::BeginPopupModal(resetPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Text("Reset every setting for Pass %u to default?", index + 1);
                    if (ImGui::Button("Confirm"))
                    {
                        ResetPassOptions(pass);
                        CancelNrEdits();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }

                if (index > 0)
                {
                    ImGui::SameLine();
                    char copyLabel[48] {};
                    snprintf(copyLabel, sizeof(copyLabel), "Copy Pass %u settings", index);
                    if (ImGui::SmallButton(copyLabel))
                    {
                        CopyPassOptions(PassOptions(config, index - 1), pass);
                        CancelNrEdits();
                    }
                    HelpMarker("Copies every saved setting from the preceding pass into this pass. It does not change the shared pass count or enable Multipass.");
                }

                ImGui::PushItemWidth(220.0f * menuResScale);
                char id[96] {};
                DeferredNrSlider(scaleLabel, { pass.workingScale }, 0.25f, 2.0f, 1.0f, "%d%%", true);
                const int scale = (int) lroundf(pendingNrEdits[scaleLabel].Value() * 100.0f);
                HelpMarker("Sets this pass's model raster from 25 to 200 percent. The composed frame remains full resolution; cost changes roughly with the square of this value.");

                static const char* downscalers[] = { "FSR1", "Bicubic", "Catmull-Rom", "Lanczos2",
                                                     "Lanczos3", "Kaiser2", "Kaiser3", "MAGIC" };
                int downscaler = std::clamp((int) pass.scalingDownscaler->value_or_default(), 0, 7);
                if (scale <= 100) ImGui::BeginDisabled();
                snprintf(id, sizeof(id), "Downscaler##pass%u", index + 1);
                if (ImGui::Combo(id, &downscaler, downscalers, IM_ARRAYSIZE(downscalers)))
                    *pass.scalingDownscaler = (Scaler) downscaler;
                snprintf(id, sizeof(id), "Reset##pass%u-downscaler", index + 1);
                ResetButton(id, [&] { *pass.scalingDownscaler = Scaler::Lanczos3; });
                if (scale <= 100) ImGui::EndDisabled();
                HelpMarker("Chooses the filter that averages this pass's above-native model answer back to the full-resolution frame. It applies only above 100 percent.");

                static const char* presets[] = { "Default", "Preset 1", "Preset 2", "Preset 3" };
                int preset = std::clamp((int) pass.preset->value_or_default(), 0, 3);
                snprintf(id, sizeof(id), "Model preset##pass%u", index + 1);
                if (ImGui::Combo(id, &preset, presets, IM_ARRAYSIZE(presets))) *pass.preset = (uint32_t) preset;
                snprintf(id, sizeof(id), "Reset##pass%u-preset", index + 1);
                ResetButton(id, [&] { *pass.preset = 0u; });
                HelpMarker("Selects the model preset for this pass's independent Feature 18 session. Changing it rebuilds only this pass.");

                static const char* styles[] = { "Default (standard)", "Natural", "Cinematic" };
                int style = std::clamp((int) pass.style->value_or_default(), 0, 2);
                snprintf(id, sizeof(id), "Style##pass%u", index + 1);
                if (ImGui::Combo(id, &style, styles, IM_ARRAYSIZE(styles))) *pass.style = (uint32_t) style;
                snprintf(id, sizeof(id), "Reset##pass%u-style", index + 1);
                ResetButton(id, [&] { *pass.style = 0u; });
                HelpMarker("Selects this pass's model processing profile. Default is strongest; Natural and Cinematic are progressively gentler alternatives.");

                static const char* transfers[] = { "Classic", "Matched residual" };
                int transfer = pass.transfer->value_or_default() == 1 ? 1 : 0;
                if (scale >= 100) ImGui::BeginDisabled();
                snprintf(id, sizeof(id), "Enlargement##pass%u", index + 1);
                if (ImGui::Combo(id, &transfer, transfers, IM_ARRAYSIZE(transfers))) *pass.transfer = (uint32_t) transfer;
                snprintf(id, sizeof(id), "Reset##pass%u-enlargement", index + 1);
                ResetButton(id, [&] { *pass.transfer = 1u; });
                if (scale >= 100) ImGui::EndDisabled();
                HelpMarker("Chooses how a sub-native model edit returns to full size. Matched residual enlarges only the model's difference and generally preserves colour better.");

                snprintf(id, sizeof(id), "Detail strength##pass%u", index + 1);
                DeferredSlider(id, pass.transferStrength, 0.0f, 2.0f, 1.0f);
                HelpMarker("Controls how much of this pass's detail edit reaches the composed frame. Zero hides this pass's edit; values above one exaggerate it.");
                snprintf(id, sizeof(id), "Colour strength##pass%u", index + 1);
                DeferredSlider(id, pass.colourStrength, 0.0f, 4.0f, 1.0f);
                HelpMarker("Controls how much of this pass's colour change accompanies its lighting edit. Zero preserves the preceding pass's hue.");
                snprintf(id, sizeof(id), "Highlight guard##pass%u", index + 1);
                DeferredSlider(id, pass.maxRatio, 1.0f, 8.0f, 2.0f, "%.1fx");
                HelpMarker("Limits the brightness multiplication or division this pass may apply. The 2x default protects moving highlights.");
                snprintf(id, sizeof(id), "Model Strength##pass%u", index + 1);
                DeferredSlider(id, pass.intensity, 0.0f, 2.0f, 1.0f);
                HelpMarker("Sets this pass's internal model strength. It commits when the slider is released and rebuilds only this pass's feature. Zero does not disable model execution.");
                snprintf(id, sizeof(id), "Local structure##pass%u", index + 1);
                DeferredSlider(id, pass.localStructure, 0.0f, 2.0f, 1.0f);
                HelpMarker("Sets the local-structure strength for this pass's independent model session.");
                snprintf(id, sizeof(id), "Local tone##pass%u", index + 1);
                DeferredSlider(id, pass.localTone, 0.0f, 2.0f, 1.0f);
                HelpMarker("Sets the local-tone strength for this pass's independent model session.");
                snprintf(id, sizeof(id), "Skin structure##pass%u", index + 1);
                DeferredSlider(id, pass.skinStructure, -1.0f, 2.0f, -1.0f);
                HelpMarker("Sets skin-structure strength for this pass. Minus one follows Local structure; zero and above tune it independently.");

                bool autoMask = pass.autoMask->value_or_default();
                snprintf(id, sizeof(id), "Auto skin mask##pass%u", index + 1);
                if (ImGui::Checkbox(id, &autoMask)) *pass.autoMask = autoMask;
                snprintf(id, sizeof(id), "Reset##pass%u-auto-mask", index + 1);
                ResetButton(id, [&] { *pass.autoMask = true; });
                HelpMarker("Lets this pass's model identify skin rather than treating every region uniformly.");

                static const char* reversibleModes[] = { "Soft-knee composition", "Neutwo composition",
                                                          "Neutwo pure inverse" };
                int reversible = std::clamp((int) pass.reversibleMode->value_or_default(), 0, 2);
                snprintf(id, sizeof(id), "Proxy composition##pass%u", index + 1);
                if (ImGui::Combo(id, &reversible, reversibleModes, IM_ARRAYSIZE(reversibleModes)))
                    *pass.reversibleMode = (uint32_t) reversible;
                snprintf(id, sizeof(id), "Reset##pass%u-proxy", index + 1);
                ResetButton(id, [&] { *pass.reversibleMode = 0u; });
                HelpMarker("Chooses this pass's reversible proxy/composition path. Soft-knee is the shipped default; the Neutwo modes remain experimental.");

                bool apply = pass.applyModel->value_or_default();
                snprintf(id, sizeof(id), "Apply the model##pass%u", index + 1);
                if (ImGui::Checkbox(id, &apply)) *pass.applyModel = apply;
                snprintf(id, sizeof(id), "Reset##pass%u-apply", index + 1);
                ResetButton(id, [&] { *pass.applyModel = true; });
                HelpMarker("Off keeps this pass evaluating and preserving its history but hides only its edit, leaving the preceding completed image visible.");

                ImGui::PopItemWidth();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
}

void RenderScreenshotMenu(Config* config)
{
    if (auto section = ScopedCollapsingHeader("Comparison screenshots", ImGuiTreeNodeFlags_DefaultOpen); section.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ScopedNestedTextWrap wrap {};
        const unsigned int route = config->DlssNrRoute.value_or_default();
        const bool present = route == 1 || route == 2;
        const bool enabled = config->GetDlssNrRuntimeSnapshot().enabled;
        const bool busy = ComparisonScreenshotBusy();
        const bool nativePair = NativeComparisonScreenshotAvailable();
        const char* backendRefusal = Screenshots::BackendRefusal(route, enabled, State::Instance().api == API::DX12);
        ImGui::TextWrapped("Save full-resolution PNG comparisons and matching JSON manifests in NeuroticScreenshots beside the game.");
        ImGui::TextWrapped("Compatibility: DX11 games can compare active Present Image Only and Present Enhanced routes. Native Temporal and NR-off comparisons require DX12.");
        const bool analysis = Advisor().running;
        ImGui::BeginDisabled(busy || analysis || backendRefusal != nullptr);
        bool before = config->ScreenshotNrOff.value_or_default();
        ImGui::BeginDisabled(enabled && !present && !nativePair);
        if (ImGui::Checkbox("NR off", &before)) config->ScreenshotNrOff = before;
        ImGui::EndDisabled();
        bool native = config->ScreenshotNativeNr.value_or_default();
        ImGui::BeginDisabled(present || !enabled);
        if (ImGui::Checkbox(nativePair ? "Native NR on###ScreenshotNative" : "Current full output###ScreenshotNative", &native)) config->ScreenshotNativeNr = native;
        ImGui::EndDisabled();
        bool imageOnly = config->ScreenshotPresentNr.value_or_default();
        ImGui::BeginDisabled(!present || !enabled);
        if (ImGui::Checkbox("Present NR on", &imageOnly)) config->ScreenshotPresentNr = imageOnly;
        ImGui::EndDisabled();
        if (route == 0)
            ImGui::TextWrapped("Experimental Native Temporal limitation: the original/reference image can come out darker than it should because display conversion is currently incorrect. No brightness workaround is applied.");
        if (backendRefusal)
            ImGui::TextWrapped("Comparison unavailable for the current route: %s", backendRefusal);
        else if (nativePair && config->DlssNrRunBeforeSr.value_or_default() && !Telemetry().nativeRayReconstructionActive)
        {
            ImGui::TextWrapped("Performance compares one frame using two temporary DLSS evaluations with fresh history, then stops. Live history is unchanged. Capture can briefly pause rendering and use extra memory.");
            ImGui::TextWrapped("These fresh-history images do not reproduce accumulated live-image history.");
        }
        else if (nativePair)
            ImGui::TextWrapped("Native pairs capture the same scene before and after NR, ahead of later game effects and HUD.");
        else if (enabled && !present)
            ImGui::TextWrapped("Performance comparisons currently require native DX12 DLSS at full display output. This upscaler can save its current full output; unavailable comparisons are disabled.");
        else if (enabled)
            ImGui::TextWrapped("Present NR saves the selected before/after images from the same full-resolution frame.");
        else
            ImGui::TextWrapped("NR is off: its current full output is available. Unavailable comparisons are disabled.");
        if (route == 2)
            ImGui::TextWrapped("Present Enhanced comparisons are experimental; runtime image quality is not yet validated.");
        if (analysis)
            ImGui::TextWrapped("Finish or cancel analysis before taking comparisons.");
        ImGui::TextWrapped("Capture starts on the next ready frame. You can leave this menu open; it is excluded automatically. In-game HUD and other overlays already in the image remain.");
        if (ImGui::Button(backendRefusal ? "Unavailable for this route###TakeComparisonScreenshots"
                                         : "Take comparison screenshots###TakeComparisonScreenshots"))
            RequestComparisonScreenshot();
        ImGui::EndDisabled();
        if (busy && ImGui::Button("Cancel screenshots")) CancelComparisonScreenshot();
        ImGui::TextWrapped("%s", ComparisonScreenshotStatus().c_str());
        ImGui::TextWrapped("NR-on comparisons need Apply Model on and Debug view / Compare off.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Screenshot keybind");
        MenuCommon::RenderScreenshotKeybind(config);
        ImGui::TextDisabled("Escape cancels; Backspace clears the binding. Also shown in Keybinds.");
    }
}

} // namespace DlssNr
