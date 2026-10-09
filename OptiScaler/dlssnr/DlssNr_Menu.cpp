#include "pch.h"
#include "NativeFgVulkan.h"
#include "RenderingOutputStatus.h"
#include "RenderingOutputStartupTrace.h"
#include "NrAnythingModeControls.h"
#include "NativeFgPresentation.h"
#include "NativeGuideRoute.h"
#include "connections/ConnectionPolicy.h"
#include "VulkanNrFlightRecorder.h"
#include <Util.h>
#include "../nr/diagnostics/capability/CapabilityStatusCollector.h"
#include "../nr/diagnostics/capability/CapabilityQuery.h"
#include "../nr/diagnostics/capability/CapabilityRegistry.h"
#include "../nr/diagnostics/capability/CapabilityExport.h"
#include "../nr/diagnostics/capability/CapabilityRefresh.h"
#include "DlssNrFeature_Vk.h"
#include "VulkanPresentStatus.h"
#include "NativeVulkanGuides.h"

#include "DlssNr.h"
#include "DlssNr_ExposureScan.h"
#include "DlssNr_BridgeTelemetry.h"
#include "DlssNr_Present.h"
#include "DlssNr_PresentGuides.h"
#include "DlssNr_MenuStatus.h"
#include "DlssNr_StageControls.h"
#include "DlssNr_MenuControls.h"
#include "NrPendingEdit.h"
#include "NrScreenshotContract.h"
#include "NrExperimentalPolicy.h"
#include "NrPreflightCell.h"
#include "NrDiagnosticsUi.h"
#include "NrPreflightState.h"
#include "NrOutputColorState.h"
#include "NrIntakeCheckState.h"
#include "NrStatusPanel.h"
#include "NrJitterDisplay.h"
#include "NrAdvisorPolicy.h"
#include "ObservationRefreshButton.h"
#include "PreparedGuideStatus.h"
#include "PreparedGuideStatusV2.h"
#include <nr/diagnostics/capability/ObservationRefresh.h>


#include <Config.h>
#include <State.h>
#include <menu/menu_common.h>
#include <menu/SleekContentCard.h>
#include <menu/BoundedPopup.h>
#include <menu/Localization.h>
#include <magic_enum.hpp>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
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
#include <cfloat>

namespace DlssNr
{

static const char* ConnectionSourceLabel(Connections::Source source){switch(source){
case Connections::Source::Native:return Neurotic::UiLiteral("ingame.dlssnr-menu.native_bf0ec6da", "Native");case Connections::Source::BuiltIn:return Neurotic::UiLiteral("ingame.dlssnr-menu.built_in_capture_8d435da4", "Built-in capture");case Connections::Source::ReShade:return "ReShade";case Connections::Source::External:return Neurotic::UiLiteral("ingame.dlssnr-menu.external_ab876246", "External");default:return Neurotic::UiLiteral("ingame.dlssnr-menu.automatic_33f5300c", "Automatic");}}
static const char* ConnectionTransportLabel(Connections::Transport transport){switch(transport){case Connections::Transport::GPUOnly:return "GPU";case Connections::Transport::CPU:return Neurotic::UiLiteral("ingame.connection.0034536325d1", "CPU (readback cost)");default:return Neurotic::UiLiteral("ingame.dlssnr-menu.automatic_33f5300c", "Automatic");}}

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

static void RenderInputSelectionStatus()
{
    const auto selection=Connections::QueryInputSelection();
    using Phase=Connections::InputPhase;
    const char* label=selection.phase==Phase::Waiting||selection.phase==Phase::Switching?
        Neurotic::UiLiteral("ingame.nr-input.changing", "Changing input..."):
        selection.phase==Phase::RestartRequired?Neurotic::UiLiteral("ingame.nr-input.restart", "Restart required"):
        selection.phase==Phase::Blocked?Neurotic::UiLiteral("ingame.nr-input.unchanged", "Input unchanged"):
        Neurotic::UiLiteral("ingame.nr-input.applied", "Input preference applied");
    // Stable row; applied preference is not evidence of active rendering.
    const auto color=selection.phase==Phase::Applied?ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled):
        ImVec4(1.f,.72f,.25f,1.f);
    ImGui::TextColored(color,"%s",label);
    if(!selection.reason.empty()&&(ImGui::IsItemHovered()||ImGui::IsItemFocused())){
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*28.f);
        ImGui::TextUnformatted(Neurotic::Translate(selection.reason).c_str());
        ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
}

static bool UsesVulkanNrRoute(const Config& config)
{
    return config.DlssNrRoute.value_or_default() != 0
        ? PresentTelemetry().api == PresentApi::Vulkan
        : IsVulkanInput();
}


// All native readouts use the owner of the selected graphics API.
static TelemetrySnapshot SelectedNativeTelemetry()
{
    return IsVulkanInput() ? NativeTelemetryVk() : Telemetry();
}

bool MenuIsActive(Config* config)
{
    const auto settings=config->GetDlssNrConfigSnapshot();
    const bool enabled=settings.DlssNrEnabled.value_or_default();
    const bool presentRoute=settings.DlssNrRoute.value_or_default()!=0;
    const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    static std::optional<NrConfigSnapshot<Config>> observed;
    static API observedApi=State::Instance().swapchainApi;
    static bool observedVulkan=IsVulkanInput();
    static uint64_t selection=0;
    if(!observed || !observed->SameConfiguration(settings) || observedApi!=State::Instance().swapchainApi || observedVulkan!=IsVulkanInput()) {
        observed=settings; observedApi=State::Instance().swapchainApi; observedVulkan=IsVulkanInput(); ++selection;
    }
    static MenuStatus::ActivityObservation nativeObservation, presentObservation;
    if(presentRoute) {
        const auto connection=PreparedGuides::QueryStatusV2(GetTickCount64());
        if(connection.available&&connection.fresh&&connection.status.sourceApi==0x20000&&
           connection.status.selectedSource==Connections::Source::BuiltIn){
            static MenuStatus::ActivityObservation builtInObservation;
            const auto& input=connection.status;
            return enabled&&input.outputValid&&input.guideReady&&!input.restartRequired&&
                builtInObservation.Fresh(selection,input.copybackCompletions,now,input.generation);
        }
        const auto telemetry=PresentTelemetry();
        const auto policy=PresentResolution::Selected(settings);
        if(telemetry.api==PresentApi::Vulkan) {
            const auto vk=GetVulkanPresentStatus().Snapshot();
            const bool fresh=presentObservation.Fresh(selection,vk.completed,now,vk.generation);
            return enabled && fresh && vk.requested && vk.active && !vk.failed && !vk.needsRecreate &&
                vk.workload.policy.mode==policy.mode && vk.workload.policy.scale==policy.scale;
        }
        const bool fresh=presentObservation.Fresh(selection,telemetry.lastCompletedFence,now,telemetry.resourceGeneration);
        return enabled && fresh && telemetry.requested && telemetry.active && !telemetry.failed &&
            telemetry.requestedPlacement=="Present" && telemetry.resolution==policy.mode && telemetry.workload==policy.scale;
    }
    const auto telemetry=SelectedNativeTelemetry();
    const bool fresh=nativeObservation.Fresh(selection,
        telemetry.gpuCompletedOutputEvaluations,now,telemetry.lifecycleGeneration);
    return enabled && fresh && telemetry.enabled && telemetry.running && !telemetry.failed &&
        !telemetry.outputQuarantined && !telemetry.transitionPending && !telemetry.resetPending &&
        (settings.DlssNrRenderingMode.value_or_default()==0 ||
         telemetry.nativeRayReconstructionActive || telemetry.preSrDisplayReady);
}

MenuReadiness MenuPreflightState(Config* config)
{
    const auto settings = config->GetDlssNrConfigSnapshot();
    const bool enabled = settings.DlssNrEnabled.value_or_default();
    const bool presentRoute = settings.DlssNrRoute.value_or_default() != 0;
    const bool vulkan = IsVulkanInput();
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    static std::optional<NrConfigSnapshot<Config>> observed;
    static API observedApi = State::Instance().swapchainApi;
    static bool observedVulkan = vulkan;
    static uint64_t selection = 0;
    if (!observed || !observed->SameConfiguration(settings) ||
        observedApi != State::Instance().swapchainApi || observedVulkan != vulkan)
    {
        observed = settings;
        observedApi = State::Instance().swapchainApi;
        observedVulkan = vulkan;
        ++selection;
    }
    static MenuStatus::ActivityObservation nativeObservation, presentObservation;
    if (presentRoute)
    {
        const auto connection=PreparedGuides::QueryStatusV2(GetTickCount64());
        if(connection.available&&connection.fresh&&connection.status.sourceApi==0x20000&&
           connection.status.selectedSource==Connections::Source::BuiltIn){
            const auto& input=connection.status;
            if(enabled&&input.restartRequired)return MenuReadiness::Blocked;
            return input.outputValid&&input.guideReady?MenuReadiness::Ready:MenuReadiness::Waiting;
        }
        const auto telemetry = PresentTelemetry();
        const auto policy = PresentResolution::Selected(settings);
        if (telemetry.api == PresentApi::Vulkan)
        {
            const auto vk = GetVulkanPresentStatus().Snapshot();
            const bool fresh = presentObservation.Fresh(selection, vk.attempts, now, vk.generation);
            if (!fresh || !vk.requested) return MenuReadiness::Waiting;
            if (enabled && vk.failed) return MenuReadiness::Blocked;
            const bool ready = vk.active && !vk.needsRecreate && vk.extent.width && vk.extent.height &&
                vk.workload.policy.mode == policy.mode && vk.workload.policy.scale == policy.scale;
            return ready ? MenuReadiness::Ready : MenuReadiness::Waiting;
        }
        const bool fresh = presentObservation.Fresh(selection,
            telemetry.presentAttempts + telemetry.skippedFrames, now, telemetry.resourceGeneration);
        // A failure from a prior selection or retired owner cannot turn this tab red.
        if (!fresh || !telemetry.requested || telemetry.requestedPlacement != "Present")
            return MenuReadiness::Waiting;
        if (enabled && telemetry.failed) return MenuReadiness::Blocked;
        const bool ready = telemetry.active && telemetry.backbufferWidth && telemetry.backbufferHeight &&
            telemetry.resolution == policy.mode && telemetry.workload == policy.scale;
        return ready ? MenuReadiness::Ready : MenuReadiness::Waiting;
    }
    const auto telemetry = SelectedNativeTelemetry();
    const bool fresh = nativeObservation.Fresh(selection,
        telemetry.nativeInputs.observations + telemetry.frames, now, telemetry.lifecycleGeneration);
    if (!fresh || !telemetry.lifecycleOpen) return MenuReadiness::Waiting;
    if (enabled && telemetry.enabled && telemetry.failed) return MenuReadiness::Blocked;
    const bool ready = telemetry.nativeInputs.observations && telemetry.frameWidth && telemetry.frameHeight &&
        !telemetry.outputQuarantined && !telemetry.transitionPending && !telemetry.resetPending &&
        (!enabled || (telemetry.enabled && telemetry.running));
    // Optional guides, jitter, exposure and masks do not block this readiness projection.
    return ready ? MenuReadiness::Ready : MenuReadiness::Waiting;
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

// Existing Basic/Advanced call sites retain their identities; help is not rendered.
static void HelpMarker(const char*) {}

static std::optional<MenuPage> requestedMenuPage;
std::optional<MenuPage> ConsumeMenuPageRequest()
{
    const auto page = requestedMenuPage;
    requestedMenuPage.reset();
    return page;
}
static void BeginNrCard(const char* id, const char* title)
{ Neurotic::Sleek::BeginContentCard(id, title, true); }
static void EndNrCard() { Neurotic::Sleek::EndContentCard(); }

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
                             ImVec2* sliderMin = nullptr, ImVec2* sliderMax = nullptr,
                             const char* tableLabel = nullptr, const char* tableHelp = nullptr)
{
    auto& edit = pendingNrEdits[label];
    edit.Prepare(targets, ImGui::GetFrameCount());
    float value = edit.Value();
    bool changed = false;
    const char* sliderId = label;
    if (tableLabel)
    {
        const char* stableId = strstr(label, "###");
        sliderId = stableId ? stableId : "##NrMainModelStrength";
        Neurotic::Sleek::ControlLabel(tableLabel);
        const float resetWidth = ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - resetWidth - ImGui::GetStyle().ItemSpacing.x));
    }
    if (!tableLabel) ImGui::SetNextItemWidth(MenuControls::InlineSliderWidth(label));
    const float trackWidth = ImGui::CalcItemWidth();
    if (percent)
    {
        int percentage = (int) lroundf(value * 100.0f);
        if (ImGui::SliderInt(sliderId, &percentage, (int) lroundf(mn * 100),
                             (int) lroundf(mx * 100), "%d%%", ImGuiSliderFlags_AlwaysClamp))
            edit.Preview(percentage / 100.0f);
    }
    else if (tableLabel ? Neurotic::Sleek::CardSliderFloat(sliderId, &value, mn, mx, fmt, ImGuiSliderFlags_AlwaysClamp) :
             ImGui::SliderFloat(sliderId, &value, mn, mx, fmt, ImGuiSliderFlags_AlwaysClamp))
        edit.Preview(value);
    if (sliderMin) *sliderMin = ImGui::GetItemRectMin();
    if (sliderMax) *sliderMax = ImGui::GetItemRectMin() + ImVec2(trackWidth, ImGui::GetFrameHeight());
    if (ImGui::IsItemDeactivatedAfterEdit()) changed = edit.Commit(mn, mx);
    edit.Finish(ImGui::IsItemActive());
    ImGui::SameLine();
    const char* stableLabel = strstr(label, "###");
    const std::string resetId = stableLabel ?
        std::string(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset###Reset##")) + (stableLabel + 3) : std::string(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##")) + label;
    if (ImGui::SmallButton(resetId.c_str()))
    {
        edit.Reset(def);
        CancelNrEdits();
        changed = true;
    }
    if (tableHelp) HelpMarker(tableHelp);
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
                           float def, const char* fmt = "%.2f",
                           const char* tableLabel = nullptr, const char* tableHelp = nullptr)
{
    return DeferredNrSlider(label, { opt }, mn, mx, def, fmt, false, nullptr, nullptr,
                            tableLabel, tableHelp);
}

static unsigned int RenderPassCountSelector(Config* config)
{
    static const char* passCounts[] = { Neurotic::UiLiteral("ingame.provider.ab3a55fbb9c0", "Standard (1 pass)"), Neurotic::UiLiteral("ingame.option.db9da2845016", "2 passes"), Neurotic::UiLiteral("ingame.option.ae48058c0334", "3 passes"), Neurotic::UiLiteral("ingame.option.3edd3d211d77", "4 passes"),
                                        Neurotic::UiLiteral("ingame.option.f6e921322e1c", "5 passes"), Neurotic::UiLiteral("ingame.option.06cc7a9d10f8", "6 passes"), Neurotic::UiLiteral("ingame.option.bb60fd8589ad", "7 passes"), Neurotic::UiLiteral("ingame.option.71dad9e1f092", "8 passes"), Neurotic::UiLiteral("ingame.option.e96350d22443", "9 passes"),
                                        Neurotic::UiLiteral("ingame.option.98314f2be3b0", "10 passes") };
    int passCountIndex = std::clamp((int) config->DlssNrPasses.value_or_default(), 1, 10) - 1;
    if (ImGui::Combo(Neurotic::UiLiteral("ingame.dlssnr-menu.passes_e154f1e3", "Passes"), &passCountIndex, passCounts, IM_ARRAYSIZE(passCounts)))
    {
        NrConfigSynchronization::Transaction transaction;
        config->DlssNrPasses = (uint32_t) (passCountIndex + 1);
        // Retain the old field as an in-memory compatibility hint. The persisted alias remains
        // derived from the Multipass switch, so choosing a count alone never activates it.
        config->DlssNrSecondLayer =
            config->DlssNrMultipassEnabled.value_or_default() && passCountIndex >= 1;
    }
    HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.standard_uses_one_pass_additional_passes_require_6db17b33", "Standard uses one pass. Additional passes require Enable NR Multipass on a compatible route. "
               "Their settings can be edited in Neural Rendering Multipass before enabling it."));
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
    int resolutionPreference = 1;
    std::optional<NrConfigSnapshot<Config>> testedSettings;
    std::string detail = Neurotic::UiMessage("ingame.dlssnr-menu.not_measured_3c3329c5", "Not measured");
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
    std::optional<float> manualScale, presentManualScale, enhancedManualScale;
    std::optional<uint32_t> nativePreset, presentPreset, enhancedPreset;
};

struct AdvisorState
{
    AdvisorPhase phase = AdvisorPhase::Idle;
    bool running = false;
    bool analyzed = false;
    bool analyzeAll = false;
    int nextRoute = 0;
    int appliedRoute = -1;
    int targetIndex = 2; // 60 FPS
    int goalIndex = 1;   // balanced
    int resolutionPreference = 1;
    int stage = AdvisorPolicy::After; // Advisor scope only; never changes live settings on draw.
    int routeIndex = 0;
    int recommendation = -1;
    double phaseStarted = 0.0;
    double testStarted = 0.0, lastTick = 0.0, transitionSeconds = 0.0;
    AdvisorSampling::Window sampling;
    uint64_t providerGeneration = 0;
    uint64_t configurationGeneration = 0, resourceGeneration = 0;
    FGInput fgInput = FGInput::NoFG;
    FGOutput fgOutput = FGOutput::NoFG;
    sl::DLSSGMode fgMode = sl::DLSSGMode::eOff;
    bool rayReconstruction = false;
    int fgRatio = 0, xeRatio = 0;
    bool ready = false;
    bool receivedOutput = false;
    std::vector<double> baselineIntervals;
    unsigned int originalWidth = 0;
    unsigned int originalHeight = 0;
    unsigned long long startNativeFrames = 0;
    unsigned long long startPresentEvaluations = 0;
    unsigned long long startPresentAttempts = 0;
    unsigned long long startGuideEvaluations = 0;
    unsigned long long lastNativeGpuFrame = 0;
    unsigned long long lastPresentGpuSample = 0;
    unsigned long long lastCompletedObservation = 0;
    unsigned long long lifecycleGeneration = 0;
    std::optional<NrConfigSnapshot<Config>> expectedSettings;
    std::optional<NrConfigSnapshot<Config>> coverageSettings;
    double frameIntervalTotal = 0.0;
    unsigned int frameIntervalSamples = 0;
    double modelGpuTotal = 0.0;
    unsigned int modelGpuSamples = 0;
    std::array<AdvisorRouteResult, 3> routes;
    AdvisorOriginalSettings original;
    std::string status = Neurotic::UiMessage("ingame.dlssnr-menu.choose_a_target_and_analyze_the_current_game_sce_941febc9", "Choose a target and analyze the current game scene.");
    std::string reason = Neurotic::UiMessage("ingame.dlssnr-menu.no_settings_change_until_you_choose_an_available_f4dd1958", "No settings change until you choose an available route.");
    std::string gpuName = Neurotic::UiMessage("ingame.dlssnr-menu.detecting_graphics_card_5066a0c7", "Detecting graphics card...");
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

uint64_t AdvisorProviderGeneration(const PresentTelemetrySnapshot& present)
{
    const auto provider = PreFg::Provider();
    return provider.known ? provider.generation : present.cadence.providerGeneration;
}

void CaptureAdvisorContext(const Config& config, const PresentTelemetrySnapshot& present)
{
    auto& advisor = Advisor();
    const auto native = SelectedNativeTelemetry();
    advisor.providerGeneration = AdvisorProviderGeneration(present);
    advisor.lifecycleGeneration = native.lifecycleGeneration;
    advisor.rayReconstruction = native.nativeRayReconstructionActive;
    advisor.fgInput = State::Instance().activeFgInput;
    advisor.fgOutput = State::Instance().activeFgOutput;
    advisor.fgMode = State::Instance().dlssgLastSetMode.load();
    advisor.fgRatio = config.FGDLSSGInterpolationCount.value_or_default();
    advisor.xeRatio = config.FGXeFGInterpolationCount.value_or_default();
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
    out.manualScale = config.DlssNrUiManualScale.snapshot();
    out.presentManualScale = config.DlssNrUiPresentManualScale.snapshot();
    out.enhancedManualScale = config.DlssNrUiEnhancedManualScale.snapshot();
    out.nativePreset = config.DlssNrUiResolutionPreset.snapshot();
    out.presentPreset = config.DlssNrUiPresentResolutionPreset.snapshot();
    out.enhancedPreset = config.DlssNrUiEnhancedResolutionPreset.snapshot();
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
    config.DlssNrUiManualScale = original.manualScale;
    config.DlssNrUiPresentManualScale = original.presentManualScale;
    config.DlssNrUiEnhancedManualScale = original.enhancedManualScale;
    config.DlssNrUiResolutionPreset = original.nativePreset;
    config.DlssNrUiPresentResolutionPreset = original.presentPreset;
    config.DlssNrUiEnhancedResolutionPreset = original.enhancedPreset;
    original.captured = false;
    ++AdvisorSampling::ConfigurationGeneration;
    AdvisorSampling::TemporarySettings.store(false);
}

void ConfigureAdvisorRoute(Config& config, int route)
{
    NrConfigSynchronization::Transaction transaction;
    config.SetDlssNrEnabled(true);
    config.DlssNrApplyModel = false;
    config.DlssNrMultipassEnabled = false;
    config.DlssNrSecondLayer = false;
    config.DlssNrPasses = 1u;
    AdvisorPolicy::SelectPlacement(config, Advisor().stage, route);
    StageUi::SelectResolutionChoice(config, Advisor().resolutionPreference);
}

void BeginAdvisorRoute(Config& config, int route)
{
    auto& advisor = Advisor();
    advisor.routeIndex = route;
    advisor.phase = AdvisorPhase::Warmup;
    advisor.phaseStarted = AdvisorNow();
    advisor.testStarted = advisor.lastTick = advisor.phaseStarted;
    advisor.ready = false;
    advisor.receivedOutput = false;
    advisor.sampling = {};
    if (!advisor.baselineIntervals.empty())
    {
        auto sorted = advisor.baselineIntervals;
        std::sort(sorted.begin(), sorted.end());
        advisor.sampling.baselineMedianMs = sorted[sorted.size() / 2];
    }
    ConfigureAdvisorRoute(config, route);
    advisor.configurationGeneration = ++AdvisorSampling::ConfigurationGeneration;
    advisor.expectedSettings = TryNrConfigSnapshot(config);
    const auto native = SelectedNativeTelemetry();
    const auto present = DlssNr::PresentTelemetry();
    CaptureAdvisorContext(config, present);
    advisor.sampling.lastSequence = present.cadence.sequence;
    const auto guides = DlssNr::PresentGuides::Instance().Inspect();
    advisor.startNativeFrames = native.gpuCompletedOutputEvaluations;
    advisor.startPresentEvaluations = present.modelEvaluations;
    advisor.startPresentAttempts = present.presentAttempts;
    advisor.startGuideEvaluations = guides.evaluated;
    advisor.lastNativeGpuFrame = native.gpuCompletedOutputEvaluations;
    advisor.lastPresentGpuSample = present.presentGpuSamples;
    advisor.lastCompletedObservation = route == 0 ? native.gpuCompletedOutputEvaluations : present.presentGpuSamples;
    advisor.frameIntervalTotal = 0.0;
    advisor.frameIntervalSamples = 0;
    advisor.modelGpuTotal = 0.0;
    advisor.modelGpuSamples = 0;
}

const char* AdvisorRouteRefusal(const Config& config, int route);
void ChooseAdvisorRecommendation(AdvisorState& advisor);

bool BeginNextAdvisorRoute(Config& config)
{
    auto& advisor = Advisor();
    while (advisor.nextRoute < AdvisorPolicy::RouteCount(advisor.stage))
    {
        const int route = advisor.nextRoute++;
        // Preflight is pure and independent of Multipass. A skipped route must not
        // bounce live NR settings or trigger a restore/lifecycle transition.
        if (const auto* refusal = AdvisorRouteRefusal(config, route))
        {
            advisor.routes[route] = {};
            advisor.routes[route].level = AdvisorResultLevel::Unavailable;
            advisor.routes[route].detail = std::string("Skipped: ") + refusal;
            continue;
        }
        CaptureAdvisorSettings(config, advisor.original);
        AdvisorSampling::TemporarySettings.store(true);
        advisor.routes[route] = {};
        advisor.routes[route].level = AdvisorResultLevel::Analyzing;
        advisor.routes[route].detail = Neurotic::UiMessage("ingame.dlssnr-menu.waiting_for_matching_completed_evaluations_3d9cdcd0", "Waiting for matching completed evaluations...");
        advisor.running = true;
        advisor.status = Neurotic::UiMessage("ingame.dlssnr-menu.testing_available_routes_c80f2f4b", "Testing available routes...");
        BeginAdvisorRoute(config, route);
        return true;
    }
    advisor.running = false;
    advisor.phase = AdvisorPhase::Idle;
    advisor.coverageSettings = TryNrConfigSnapshot(config);
    ChooseAdvisorRecommendation(advisor);
    return false;
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
        return result.succeeded && result.fps > 0.0 && result.fps >= target * margin;
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
        if (advisor.routes[route].succeeded)
            advisor.routes[route].level = route == selected ? AdvisorResultLevel::Recommended : AdvisorResultLevel::Available;

    advisor.analyzed = true;
    advisor.appliedRoute = -1;
    if (selected < 0)
    {
        advisor.status = Neurotic::UiMessage("ingame.dlssnr-menu.no_measured_recommendation_check_the_requirement_da413d00", "No measured recommendation. Check the requirements and feedback on each route card.");
        advisor.reason = Neurotic::UiMessage("ingame.dlssnr-menu.the_original_image_was_preserved_review_the_rout_fef0975a", "The original image was preserved. Review the route reasons below and try another scene.");
    }
    else
    {
        const char* names[] = { Neurotic::UiLiteral("ingame.dlssnr-menu.native_temporal_682e0cdf", "Native Temporal"), Neurotic::UiLiteral("ingame.dlssnr-menu.present_compatibility_75ae6b8e", "Present Compatibility"), Neurotic::UiLiteral("ingame.dlssnr-menu.present_enhanced_eb02b993", "Present Enhanced") };
        advisor.status = std::string(Neurotic::UiLiteral("ingame.dlssnr-menu.best_of_tested_routes_a60d8aee", "Best of tested routes: ")) + names[selected];
        const bool targetMet = advisor.routes[selected].fps >= target;
        advisor.reason = targetMet
            ? Neurotic::UiLiteral("ingame.dlssnr-menu.the_measured_native_cadence_met_the_target_at_th_73a88cec", "The measured native cadence met the target at the selected resolution preference. Untested routes are unmeasured.")
            : Neurotic::UiLiteral("ingame.dlssnr-menu.the_selected_route_is_below_the_native_target_co_36091308", "The selected route is below the native target. Compare the measured alternatives; untested routes are unmeasured.");
    }
}

void FinishAdvisorRoute(Config& config)
{
    auto& advisor = Advisor();
    const int route = advisor.routeIndex;
    auto& result = advisor.routes[route];
    const auto native = SelectedNativeTelemetry();
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
            native.gpuCompletedOutputEvaluations > advisor.startNativeFrames;
        result.detail = result.succeeded ? Neurotic::UiLiteral("ingame.dlssnr-menu.completed_native_evaluations_at_the_selected_res_0dd1714e", "Completed Native evaluations at the selected resolution") :
            (native.failureReason && native.failureReason[0] ? native.failureReason : Neurotic::UiLiteral("ingame.dlssnr-menu.native_model_output_was_not_verified_62bd9cfe", "Native model output was not verified"));
    }
    else
    {
        const bool expectedRoute = present.requestedPlacement ==
            (route == 2 ? Neurotic::UiLiteral("ingame.dlssnr-menu.present_enhanced_eb02b993", "Present Enhanced") : Neurotic::UiLiteral("ingame.dlssnr-menu.present_image_only_09934172", "Present Image-Only"));
        result.succeeded = expectedRoute && present.active && !present.failed &&
            present.modelEvaluations > advisor.startPresentEvaluations &&
            (route != 2 || guides.evaluated > advisor.startGuideEvaluations);
        if (result.succeeded)
            result.detail = route == 2 ? Neurotic::UiLiteral("ingame.dlssnr-menu.verified_depth_and_motion_guides_62aef47c", "Verified depth and motion guides") : Neurotic::UiLiteral("ingame.dlssnr-menu.verified_final_image_compatibility_path_663639d6", "Verified final-image compatibility path");
        else if (!present.failure.empty()) result.detail = present.failure;
        else if (!present.fallbackReason.empty()) result.detail = present.fallbackReason;
        else if (route == 2 && !guides.status.empty()) result.detail = guides.status;
        else result.detail = Neurotic::UiMessage("ingame.dlssnr-menu.present_output_was_not_verified_2af8359a", "Present output was not verified");
    }

    result.succeeded = result.succeeded && result.fps > 0 && advisor.sampling.Complete(AdvisorNow() - advisor.phaseStarted);
    result.level = result.succeeded ? AdvisorResultLevel::Available : AdvisorResultLevel::Unavailable;
    result.resolutionPreference = advisor.resolutionPreference;
    if (advisor.expectedSettings) result.testedSettings = *advisor.expectedSettings;
    LOG_INFO("Advisor route={} transition={:.3f}s samples={} nativeFPS={:.2f} valid={}", route,
        advisor.transitionSeconds, advisor.sampling.samples, result.fps, result.succeeded);
    RestoreAdvisorSettings(config, advisor.original);
    if (advisor.analyzeAll && BeginNextAdvisorRoute(config)) return;
    advisor.coverageSettings = TryNrConfigSnapshot(config);
    advisor.running = false;
    advisor.phase = AdvisorPhase::Idle;
    ChooseAdvisorRecommendation(advisor);
}

void FailAdvisorRoute(Config& config, const char* reason)
{
    auto& advisor = Advisor();
    const auto present = DlssNr::PresentTelemetry();
    LOG_WARN("Advisor stage={} route={} resolution={} reason={} fallback={} nativeFailure={}",
        advisor.stage, advisor.routeIndex, advisor.resolutionPreference, reason ? reason : "interrupted",
        present.fallbackReason, SelectedNativeTelemetry().failureReason);
    auto& result = advisor.routes[advisor.routeIndex];
    result = {};
    result.detail = reason != nullptr ? reason : Neurotic::UiLiteral("ingame.dlssnr-menu.interrupted_not_measured_bb776677", "Interrupted - not measured");
    RestoreAdvisorSettings(config, advisor.original);
    if (advisor.analyzeAll && BeginNextAdvisorRoute(config)) return;
    advisor.running = false;
    advisor.phase = AdvisorPhase::Idle;
    if (advisor.analyzeAll)
    {
        advisor.coverageSettings = TryNrConfigSnapshot(config);
        ChooseAdvisorRecommendation(advisor);
    }
    else
    {
        advisor.analyzed = false;
        advisor.recommendation = -1;
        advisor.status = result.detail;
        advisor.reason = Neurotic::UiMessage("ingame.dlssnr-menu.no_recommendation_was_applied_95c6c94d", "No recommendation was applied.");
    }
}

const char* AdvisorRouteRefusal(const Config& config, int route)
{
    // Both individual and all-route tests own the same reversible single-pass setup.
    const auto settings = config.GetDlssNrConfigSnapshot();
    if (const auto* refusal = AdvisorPolicy::Refusal(settings, Advisor().stage, route,
        Advisor().resolutionPreference, IsVulkanInput(), SelectedNativeTelemetry().nativeRayReconstructionActive))
        return refusal;
    return nullptr;
}

std::string AdvisorFeedbackFailure(const PresentTelemetrySnapshot& present)
{
    const auto& advisor = Advisor();
    if (AdvisorPolicy::CurrentFailure(advisor.routeIndex, advisor.configurationGeneration,
        advisor.startPresentAttempts, present.cadence, present.lastFallbackAttempt))
    {
        // The route, trial epoch and attempt must match before using a renderer refusal.
        if (!present.failure.empty()) return present.failure;
        if (!present.fallbackReason.empty()) return present.fallbackReason;
    }
    return AdvisorPolicy::MissingFeedback(advisor.receivedOutput);
}

void StartAdvisorAnalysis(Config& config, int selectedRoute)
{
    auto& advisor = Advisor();
    if (advisor.running) return;
    if (const auto* reason = AdvisorRouteRefusal(config, selectedRoute))
    { advisor.status = reason; return; }
    CancelComparisonScreenshot();
    advisor.analyzeAll = false;
    advisor.routes[selectedRoute] = {};
    advisor.routes[selectedRoute].level = AdvisorResultLevel::Analyzing;
    advisor.routes[selectedRoute].detail = Neurotic::UiMessage("ingame.dlssnr-menu.waiting_for_matching_completed_evaluations_3d9cdcd0", "Waiting for matching completed evaluations...");
    advisor.recommendation = -1;
    advisor.analyzed = false;
    advisor.appliedRoute = -1;
    advisor.status = Neurotic::UiMessage("ingame.dlssnr-menu.testing_one_route_41bf32bb", "Testing one route...");
    advisor.reason = Neurotic::UiMessage("ingame.dlssnr-menu.model_effect_hidden_selected_resolution_only_ori_5721a506", "Model effect hidden; selected resolution only. Original settings will be restored.");
    CaptureAdvisorSettings(config, advisor.original);
    AdvisorSampling::TemporarySettings.store(true);
    const auto present = DlssNr::PresentTelemetry();
    advisor.providerGeneration = AdvisorProviderGeneration(present);
    advisor.lifecycleGeneration = SelectedNativeTelemetry().lifecycleGeneration;
    advisor.originalWidth = present.backbufferWidth;
    advisor.originalHeight = present.backbufferHeight;
    advisor.running = true;
    BeginAdvisorRoute(config, selectedRoute);
}

void StartAdvisorAllRoutes(Config& config)
{
    auto& advisor = Advisor();
    if (advisor.running) return;
    CancelComparisonScreenshot();
    advisor.routes = {};
    advisor.recommendation = -1;
    advisor.analyzed = false;
    advisor.appliedRoute = -1;
    advisor.analyzeAll = true;
    advisor.nextRoute = 0;
    advisor.reason = Neurotic::UiMessage("ingame.dlssnr-menu.multipass_is_temporarily_disabled_each_supported_1430ace8", "Multipass is temporarily disabled. Each supported route uses the selected resolution preference, and original settings are restored between trials and at the end.");
    const auto present = DlssNr::PresentTelemetry();
    // Keep refusal-only results tied to this session even when no trial begins.
    CaptureAdvisorContext(config, present);
    advisor.originalWidth = present.backbufferWidth;
    advisor.originalHeight = present.backbufferHeight;
    if (!BeginNextAdvisorRoute(config))
        advisor.status = Neurotic::UiMessage("ingame.dlssnr-menu.no_routes_tested_review_the_requirements_shown_o_42bf19e4", "No routes tested. Review the requirements shown on each card.");
}

void ApplyAdvisorRoute(Config& config, int route)
{
    auto& advisor = Advisor();
    if (advisor.running || !AdvisorPolicy::Contains(advisor.stage, route) ||
        !advisor.routes[route].succeeded || !advisor.routes[route].testedSettings)
        return;
    NrConfigSynchronization::Transaction transaction;
    config.DlssNrRoute = uint32_t(route);
    config.DlssNrUiAfterMethod = uint32_t(route == 0 ?
        config.DlssNrUiAfterMethod.value_or_default() : route);
    if (route == 0)
    {
        config.DlssNrRenderingMode = advisor.routes[route].testedSettings->DlssNrRenderingMode.value_or_default();
        config.DlssNrRunBeforeSr = advisor.routes[route].testedSettings->DlssNrRunBeforeSr.value_or_default();
    }
    else
    {
        config.DlssNrRenderingMode = 0;
        config.DlssNrRunBeforeSr = false;
    }
    StageUi::SelectResolutionChoice(config, advisor.routes[route].resolutionPreference);
    if (advisor.routes[route].resolutionPreference == StageUi::ManualChoice)
        StageUi::SelectResolutionScale(config, StageUi::ResolutionScale(*advisor.routes[route].testedSettings));
    const char* names[] = { Neurotic::UiLiteral("ingame.dlssnr-menu.native_temporal_682e0cdf", "Native Temporal"), Neurotic::UiLiteral("ingame.dlssnr-menu.present_compatibility_75ae6b8e", "Present Compatibility"), Neurotic::UiLiteral("ingame.dlssnr-menu.present_enhanced_eb02b993", "Present Enhanced") };
    advisor.appliedRoute = route;
    advisor.status = std::string("Applied: ") + names[route];
    advisor.reason = Neurotic::UiMessage("ingame.dlssnr-menu.applied_the_tested_route_and_resolution_preferen_04165809", "Applied the tested route and resolution preference.");
    advisor.coverageSettings = TryNrConfigSnapshot(config);
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
    default: return Neurotic::UiLiteral("ingame.dlssnr-menu.not_measured_3c3329c5", "Not measured");
    }
}

void ObservationSignal(const char* label, const std::string& value, const ImVec4& color)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    NrPreflightCell::Draw(label);
    ImGui::PopStyleColor();
    ImGui::TableSetColumnIndex(1);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    NrPreflightCell::Draw(value);
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
    default: return Neurotic::UiLiteral("ingame.dlssnr-menu.waiting_for_output_format_a7f4feb5", "Waiting for output format");
    }
}

// Read-only projection of existing owner snapshots. A non-zero historical counter or
// resource dimension is never promoted to proof of current-frame model consumption.
enum class PreflightView { Signals, Details, Footer };

void RenderPreflight(Config* config, PreflightView view, bool showBody = true, bool compact = false)
{
    const auto settings = config->GetDlssNrConfigSnapshot();
    const auto present = DlssNr::PresentTelemetry();
    const bool observationVulkan = Capability::ObservationUsesVulkan(settings.DlssNrRoute.value_or_default(),
        IsVulkanInput(), present.api != PresentApi::Unknown, present.api == PresentApi::Vulkan);
    const auto native = observationVulkan ? NativeTelemetryVk() : Telemetry();
    const auto guides = DlssNr::PresentGuides::Instance().Inspect();
    const auto exposure = DlssNr::GameExposureStatus();
    const auto& input = native.nativeInputs;
    const auto& host = State::Instance();
    const auto fgProvider = PreFg::Provider();
    const bool fg = fgProvider.known && fgProvider.enabled;
    const bool configuredFg = host.activeFgInput != FGInput::NoFG && host.activeFgOutput != FGOutput::NoFG;
    const ImVec4 muted = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const auto background=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const ImVec4 good=background.x+background.y+background.z>1.5f?
        ImVec4(.12f,.46f,.25f,1.f):ImVec4(.45f,.86f,.61f,1.f);
    const ImVec4 detected=background.x+background.y+background.z>1.5f?
        ImVec4(.16f,.42f,.68f,1.f):ImVec4(.42f,.75f,1.f,1.f);
    const ImVec4 caution(0.99f, 0.77f, 0.37f, 1.0f);
    const ImVec4 bad(0.95f, 0.30f, 0.28f, 1.0f);
    const bool enabled = settings.DlssNrEnabled.value_or_default();
    const bool nativeRoute = settings.DlssNrRoute.value_or_default() == 0;
    const bool nativeExpected = enabled && nativeRoute;
    const bool presentExpected = enabled && !nativeRoute;
    const auto effectiveConnection=PreparedGuides::QueryStatusV2(GetTickCount64());
    const auto outputSnapshot=RenderingOutput::Query();
    const bool outputActive=RenderingOutput::Active(outputSnapshot,GetTickCount64());
    // All prepared APIs publish the same facts. A previous capture must not
    // paint the board after game-supplied input has taken over.
    const bool preparedInput=presentExpected&&effectiveConnection.available&&effectiveConnection.fresh&&
        effectiveConnection.status.selectedSource>=Connections::Source::BuiltIn&&
        (NativeGuides::SelectedSource()==0||NativeGuides::SelectedSource()==int(effectiveConnection.status.selectedSource))&&
        (!outputActive||outputSnapshot.producer==RenderingOutput::Producer::BuiltIn);
    static NrPreflightSignals::FreshObservation nativeProgress[2], presentProgress[2];
    const auto now=GetTickCount64();
    const bool nativeObserved = nativeProgress[observationVulkan?1:0].Update(
        native.lifecycleGeneration,input.observations,now,native.lifecycleOpen);
    const bool presentObserved = presentProgress[observationVulkan?1:0].Update(
        present.resourceGeneration,present.presentAttempts,now,present.api!=PresentApi::Unknown);
    const bool nativeOperational = nativeExpected && native.enabled && native.running && nativeObserved;
    const bool presentOperational = presentExpected && present.requested && present.active && presentObserved;
    const auto fgProgress=NativeFg::Progress();
    static NrPreflightSignals::FreshObservation fgInputProgress, fgOutputProgress;
    const bool fgOwner=NativeFg::Selected()&&fgProgress.owners&&fgProgress.physicalAvailable;
    const bool fgDetected=fgInputProgress.Update(fgProgress.identity,fgProgress.real,now,fgOwner);
    const bool fgInUse=fgOutputProgress.Update(fgProgress.identity,fgProgress.physical.generatedRetired,now,fgOwner);
    // Share this display snapshot between the clipped cell and its complete hover value.
    // Updating it before a collapsed/alternate view returns also clears changed owners.
    static JitterDisplay::Snapshot jitterDisplay;
    const JitterDisplay::Context jitterContext {reinterpret_cast<uintptr_t>(host.currentFeature),
        static_cast<int>(host.currentInputApiName), settings.DlssNrRoute.value_or_default(),
        NrConfigSynchronization::ProfileGeneration(), native.lifecycleGeneration,
        present.resourceGeneration, native.gameResets, native.featureBuilds, native.featureRebuilds};
    const auto& jitterText = jitterDisplay.Update(jitterContext, input,
        nativeObserved && host.currentFeature != nullptr, ImGui::GetTime());
    using NrPreflightState::State;
    using NrPreflightState::Signal;
    using NrPreflightState::Delivery;
    const auto color = [&](State state) -> ImVec4 {
        return state == State::Ready ? good : state == State::Detected ? detected : state == State::Fault ? bad : muted;
    };
    auto refreshState=Capability::MakeRefreshState(enabled,settings.DlssNrRoute.value_or_default(),
        NrConfigSynchronization::ProfileGeneration(),native,present,fgProvider,settings.ObservationRevision());
    refreshState.hdrChangeRevision=Capability::HdrChangeRevision();
    refreshState.menuVisibilityGeneration=MenuCommon::VisibilityGeneration();
    // Observe changed owners before drawing their lamps. When the board is visible,
    // defer starting work until its buttons have been handled so Copy retains
    // its existing suppression of an automatic refresh on the same frame.
    Capability::ObservationRefresh::Update(refreshState,observationVulkan,false,ObservationActivationHeld(),
        view != PreflightView::Signals || !showBody || compact);
    const auto completed=Capability::ObservationRefresh::Completed();
    static const Capability::CollectionResult noObservation;
    const auto& capabilityObservation=completed?completed->observation:noObservation;
    const auto refreshedOwner=Capability::ObservationRefresh::CurrentOwner(
        nativeOperational && input.jitterSupplied && input.jitter.has_value(),
        nativeOperational && input.preExposureSupplied && input.preExposure.has_value());

    // Keep servicing the existing observation refresh even when the board is hidden.
    // Collapsing this presentation must not change renderer or observation state.
    if (!showBody) return;

    if (view == PreflightView::Footer)
    {
    ImGui::PushStyleColor(ImGuiCol_Text,muted);
    NrDiagnosticsUi::Cell("##NrObservationProgress", Capability::ObservationRefresh::Busy() ?
        Neurotic::UiLiteral("ingame.dlssnr-menu.updating_observations_949e66d0", "Updating observations...") : completed ? Neurotic::UiLiteral("ingame.dlssnr-menu.observation_check_complete_ecda59fc", "Observation check complete") : Neurotic::UiLiteral("ingame.dlssnr-menu.observations_unavailable_c2c30065", "Observations unavailable"), 1);
    if(capabilityObservation.capture.snapshot)
    {
        Capability::QueryRequest request; request.capability=Capability::CapabilityId::ConfigRequestedEnabled;
        request.subject=Neurotic::UiLiteral("ingame.objectruleeditor.nr_739a8e19", "nr"); request.session=capabilityObservation.capture.snapshot->session;
        request.bindings=capabilityObservation.configurationContext;
        const auto currentRevision=NrConfigSynchronization::ObservationRevision();
        for(auto& binding:request.bindings) if(binding.dimension==Capability::Dimension::Configuration) {
            binding.status=currentRevision?Capability::StampStatus::Known:Capability::StampStatus::Unknown;
            binding.generation=currentRevision.value_or(0);
        }
        const auto fact=Capability::Query(*capabilityObservation.capture.snapshot,request);
        const auto settingsObservation=Neurotic::Translate(!Capability::ObservationRefresh::Busy() &&
                            currentRevision && capabilityObservation.configRevision==currentRevision && fact.resolution==Capability::Resolution::Resolved?
                            Neurotic::UiLiteral("ingame.dlssnr-menu.current_owner_revision_captured_2b2d6b5e", "current owner revision captured"):Neurotic::UiLiteral("ingame.dlssnr-menu.unknown_or_changed_during_collection_001a6120", "unknown or changed during collection"));
        NrDiagnosticsUi::Cell("##NrSettingsObservation", StatusPanel::Format(Neurotic::UiLiteral("ingame.dlssnr-menu.settings_observation_s_at_refresh_976de5ce", "Settings observation: %s (at refresh)"),settingsObservation.c_str()), 1);

    }
    else NrDiagnosticsUi::Cell("##NrSettingsObservation", Neurotic::UiLiteral("ingame.dlssnr-menu.settings_observation_unavailable_bddb539f", "Settings observation: unavailable"), 1);
    ImGui::PopStyleColor();
    }
    if (view == PreflightView::Signals)
    {
    if (!compact || settings.DlssNrRoute.value_or_default()==3) {
    Neurotic::Sleek::ObservationActionResult actions;
    const float refreshWidth = ObservationRefreshButtonWidth(Neurotic::UiLiteral("ingame.dlssnr-menu.refresh_observations_57f5c6d4", "Refresh observations"));
    const float copyWidth = Neurotic::Sleek::ButtonWidth(Neurotic::UiLiteral("ingame.dlssnr-menu.copy_observation_report_0cee5963", "Copy observation report")) + ImGui::GetFontSize() * 2;
    const bool paired = ImGui::GetContentRegionAvail().x >= refreshWidth + ImGui::GetStyle().ItemSpacing.x + copyWidth;
    actions.refreshClicked = ObservationRefreshButton(Neurotic::UiLiteral("ingame.dlssnr-menu.refresh_observations_57f5c6d4", "Refresh observations"), Capability::ObservationRefresh::Busy(), refreshWidth);
    if (paired) ImGui::SameLine();
    actions.copyClicked = Neurotic::Sleek::FeedbackButton(Neurotic::UiLiteral("ingame.dlssnr-menu.copy_observation_report_0cee5963", "Copy observation report"));
    actions.copyId = ImGui::GetItemID();
    Capability::ObservationRefresh::Update(refreshState,observationVulkan,actions.refreshClicked,
        ObservationActivationHeld(),!actions.copyClicked);
    if(actions.copyClicked) {
        // Preserve explicit Vulkan trace export alongside the last completed report.
        if(!DlssNr::VkFlight::Export(Util::DllPath().remove_filename()/"NeuRotic-Vulkan-trace.csv"))
            LOG_WARN("Vulkan observation trace export failed");
        auto report=completed?completed->utf8:std::string();
        if(settings.DlssNrRoute.value_or_default()==3){
            // On-demand scalar receipt also covers a supervisor that never received Signal.
            // Keep the existing last-recorded capability report, when one exists.
            try{auto startup=RenderingOutput::StartupDiagnostics();
                if(!report.empty())report+="\n\n";
                report+="NR Anything startup\n";report+=startup;
            }catch(...){LOG_WARN("NR Anything startup receipt unavailable");}
        }
        if(!report.empty()) {
            ImGui::SetClipboardText(report.c_str());
            const auto copied = ImGui::GetClipboardText();
            const bool copySucceeded = copied && report == copied;
            Neurotic::Sleek::CompleteButtonFeedback(actions.copyId,copySucceeded);
            if(!copySucceeded)LOG_WARN("Observation report clipboard copy failed; retry Copy observation report");
        }else {
            // An unavailable report must not destroy the user's existing clipboard.
            Neurotic::Sleek::CompleteButtonFeedback(actions.copyId,false);
            LOG_WARN("Observation report unavailable; refresh observations before copying");
        }
    }
    ImGui::Spacing();
    }
    const float width = ImGui::GetContentRegionAvail().x;
    struct Lamp { const char* label; State state; const char* detail; bool inUse=false; };
    const bool nativeInUse=outputActive&&outputSnapshot.producer==RenderingOutput::Producer::Native&&nativeOperational;
    const bool presentInUse=outputActive&&outputSnapshot.producer==RenderingOutput::Producer::Present&&presentOperational;
    const bool preparedInUse=outputActive&&outputSnapshot.producer==RenderingOutput::Producer::BuiltIn&&preparedInput&&effectiveConnection.status.outputValid;
    const bool gameGuidesInUse=nativeInUse||(presentInUse&&present.actualInputClass==PresentInputDecision::InputClass::Guided);
    const auto nativeDelivery = !nativeExpected ? State::Inactive :
        native.failed || native.outputQuarantined ? State::Fault :
        nativeOperational ? Delivery(true, native.frames,
            native.gpuCompletedOutputEvaluations, false) : State::Inactive;
    const auto presentDelivery = !presentExpected || !present.requested ? State::Inactive :
        present.failed ? State::Fault :
        presentOperational ? Delivery(true, present.presentAttempts,
            present.acceptedOutputPresents, false) :
        present.presentAttempts ? State::Fault : State::Inactive;
    const auto& currentOutput=outputSnapshot;
    const auto selectedDelivery=RenderingOutput::Active(currentOutput,GetTickCount64())?State::Ready:
        RenderingOutput::Fresh(currentOutput,GetTickCount64())&&currentOutput.phase==RenderingOutput::Phase::Blocked?State::Fault:State::Inactive;
    const auto history = preparedInput ? (effectiveConnection.status.outputValid?State::Ready:State::Inactive) : nativeRoute
        ? (nativeOperational && native.gpuCompletedOutputEvaluations &&
           !native.historyResetRequested ? State::Ready : State::Inactive)
        : (presentOperational && present.uninterruptedFrames &&
           !present.historyResetPending ? State::Ready : State::Inactive);
    const auto guideState = preparedInput ? (effectiveConnection.status.guideReady?State::Ready:State::Inactive) : nativeRoute
        ? Signal(nativeExpected, nativeObserved,
                 native.guideWidth != 0 && native.guideHeight != 0, true)
        : Signal(presentExpected && (PresentInput::Selected(settings) == PresentInput::Policy::RequireGuides ||
                 present.actualInputClass == PresentInputDecision::InputClass::Guided),
                 presentOperational, present.actualInputClass == PresentInputDecision::InputClass::Guided, true);
    HdrObservation::Snapshot outputColorDescriptor;
    outputColorDescriptor.registered = present.hdrDescriptorRegistered;
    outputColorDescriptor.format = present.hdrDescriptorFormat;
    outputColorDescriptor.colorSpace = present.colorSpace;
    outputColorDescriptor.colorSpaceObserved = present.colorSpaceObserved;
    outputColorDescriptor.transitioning = present.hdrDescriptorTransitioning;
    const auto outputColorApi = present.api == PresentApi::Vulkan ? OutputColorState::Api::Vulkan :
        (present.api == PresentApi::D3D11 || present.api == PresentApi::D3D12)
            ? OutputColorState::Api::Dxgi : OutputColorState::Api::Unknown;
    const auto outputColor = OutputColorState::Read(outputColorApi, presentObserved,
        presentInUse || preparedInUse, outputColorDescriptor, present.backbufferFormat, present.vkFormatObserved);
    const std::array<Lamp, 17> lamps {{
        {Neurotic::UiLiteral("ingame.dlssnr-menu.graphics_input_9f20b036", "Graphics input"), nativeObserved&&host.currentFeature ? State::Ready : State::Inactive,
             Neurotic::UiLiteral("ingame.dlssnr-menu.the_game_upscaler_feature_is_observable_this_doe_ad3c4062", "The game upscaler feature is observable; this does not prove that NR inputs are qualified."), gameGuidesInUse},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.source_output_e0fa4b31", "Source output"), preparedInput ?
             (effectiveConnection.status.captureWidth&&effectiveConnection.status.captureHeight?State::Ready:State::Inactive) : nativeRoute ?
             Signal(true, nativeObserved,
                    native.frameWidth != 0 && native.frameHeight != 0, true) :
             Signal(true, presentObserved,
                    present.backbufferWidth != 0 && present.backbufferHeight != 0, true),
             Neurotic::UiLiteral("ingame.dlssnr-menu.selected_active_owner_has_a_recorded_source_rast_7f77fcae", "Selected active owner has a recorded source raster; this does not prove the current frame."), nativeInUse||presentInUse||preparedInUse},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.frame_raster_1f80ea11", "Frame raster"), Signal(true, nativeObserved,
             native.frameWidth != 0 && native.frameHeight != 0, true), Neurotic::UiLiteral("ingame.dlssnr-menu.native_frame_dimensions_were_recorded_55a26679", "Native frame dimensions were recorded."), nativeInUse},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.present_output_5523a95c", "Present output"), Signal(true, presentObserved,
             present.backbufferWidth != 0 && present.backbufferHeight != 0, true), Neurotic::UiLiteral("ingame.dlssnr-menu.present_backbuffer_dimensions_were_recorded_b119975f", "Present backbuffer dimensions were recorded."), presentInUse||preparedInUse},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.output_color_fc6f2b1e", "Output color"), outputColor, Neurotic::UiLiteral("ingame.dlssnr-menu.present_owner_observed_the_api_s_typed_color_for_fa168f15", "Present owner observed the API's typed color format."), presentInUse||preparedInUse},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.motion_history_6ae74bf6", "Motion history"), history, Neurotic::UiLiteral("ingame.dlssnr-menu.history_is_ready_only_after_active_work_without__0db2e8e2", "History is ready only after active work without a pending reset."), nativeInUse||presentInUse||preparedInUse},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.depth_guide_0d2c1a94", "Depth guide"), preparedInput?(effectiveConnection.status.depthOrigin!=PreparedGuides::Origin::Unknown?guideState:State::Inactive):nativeObserved&&refreshedOwner.depth ? State::Ready : guideState,
             Neurotic::UiLiteral("ingame.dlssnr-menu.native_guide_raster_or_qualified_present_guide_m_dd77fb8f", "Native guide raster or qualified Present guide match; last observation is not current-frame proof."), preparedInUse||gameGuidesInUse},
        {preparedInput&&effectiveConnection.status.motionOrigin==PreparedGuides::Origin::Derived?Neurotic::UiLiteral("ingame.dlssnr-menu.estimated_motion_c3d76169", "Estimated motion"):Neurotic::UiLiteral("ingame.dlssnr-menu.motion_vectors_e3926763", "Motion vectors"), preparedInput?(effectiveConnection.status.motionOrigin!=PreparedGuides::Origin::Unknown?guideState:State::Inactive):nativeObserved&&refreshedOwner.motion ? State::Ready : guideState,
             preparedInput&&effectiveConnection.status.motionOrigin==PreparedGuides::Origin::Derived?Neurotic::UiLiteral("ingame.dlssnr-menu.motion_estimated_from_captured_image_pairs_20fbbbce", "Motion estimated from captured image pairs."):Neurotic::UiLiteral("ingame.dlssnr-menu.guide_dimensions_or_matched_present_motion_no_pe_8f0e73e7", "Guide dimensions or matched Present motion; no per-frame quality claim."), preparedInUse||gameGuidesInUse},
        {"Jitter", nativeObserved&&refreshedOwner.jitter ? State::Ready : Signal(input.jitterSupplied, nativeObserved,
             input.jitterSupplied, input.jitter.has_value()), Neurotic::UiLiteral("ingame.dlssnr-menu.latest_native_input_supplied_a_complete_finite_j_0f914dbf", "Latest Native input supplied a complete finite jitter pair."), nativeInUse},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.exposure_texture_9cad026a", "Exposure texture"), nativeObserved && input.exposureTexture
             ? State::Ready : State::Inactive, Neurotic::UiLiteral("ingame.nr-input.exposure_detected_only", "Exposure detected. Consumption is not reported.")},
        {"Pre-exposure", nativeObserved&&refreshedOwner.preExposure ? State::Ready : Signal(input.preExposureSupplied,
             nativeObserved, input.preExposureSupplied,
             input.preExposure.has_value()),
             Neurotic::UiLiteral("ingame.nr-input.pre_exposure_detected_only", "Pre-exposure detected. Consumption is not reported.")},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.scene_color_2a19d0e7", "Scene color"), State::Inactive, Neurotic::UiLiteral("ingame.dlssnr-menu.game_scene_color_has_no_independent_observation__39861cbf", "Game scene color has no independent observation in this panel.")},
        {"Route", selectedDelivery, Neurotic::UiLiteral("ingame.dlssnr-menu.selected_owner_is_active_and_has_submitted_model_9aaf2ace", "Selected owner is active and has submitted model work this session."), outputActive},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.nr_evaluation_6c36569f", "NR evaluation"), nativeDelivery, Neurotic::UiLiteral("ingame.dlssnr-menu.completed_native_nr_evaluations_this_session_0cbba011", "Completed Native NR evaluations this session."), nativeInUse},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.present_delivery_41b759a2", "Present delivery"), preparedInput?selectedDelivery:presentDelivery, Neurotic::UiLiteral("ingame.dlssnr-menu.completed_copyback_does_not_independently_prove__57f1b00e", "Completed copyback does not independently prove display."), presentInUse||preparedInUse},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.ray_reconstruction_8e0fdd67", "Ray reconstruction"), nativeObserved&&native.nativeRayReconstructionActive ? State::Ready : State::Inactive,
             Neurotic::UiLiteral("ingame.dlssnr-menu.native_owner_reports_ray_reconstruction_active_25ff1768", "Native owner reports Ray Reconstruction active."), nativeObserved&&native.nativeRayReconstructionActive},
        {Neurotic::UiLiteral("ingame.dlssnr-menu.frame_generation_7e5b7df2", "Frame generation"), fgDetected||fgInUse ? State::Ready : State::Inactive,
             Neurotic::UiLiteral("ingame.nr-input.fg_progress", "Uses current generated-presentation progress. Display is not independently verified. Other providers without current activity evidence remain unknown."), fgInUse}
    }};
    const int columns = width >= ImGui::GetFontSize() * 40.0f ? 4 :
                        width >= ImGui::GetFontSize() * 24.0f ? 2 : 1;
    if (ImGui::BeginTable("##PreflightLamps", columns,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV))
    {
        for (int index = 0; index < static_cast<int>(lamps.size()); ++index)
        {
            const auto& lamp = lamps[index];
            const auto lampState=NrPreflightState::Usage(lamp.state,lamp.inUse);
            ImGui::TableNextColumn();
            ImGui::PushID(index);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 size((std::max)(1.0f, ImGui::GetContentRegionAvail().x),
                              ImGui::GetTextLineHeightWithSpacing());
            ImGui::InvisibleButton("##State", size, ImGuiButtonFlags_EnableNav);
            if (ImGui::IsItemHovered() || ImGui::IsItemFocused()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize()*28.f);
                ImGui::TextUnformatted(Neurotic::Translate(lamp.label).c_str());
                ImGui::TextUnformatted(lampState==State::Detected?Neurotic::UiLiteral("ingame.nr-input.detected", "Detected, not in use"):
                    lampState==State::Ready?Neurotic::UiLiteral("ingame.nr-input.in_use", "In use"):
                    lampState==State::Fault?Neurotic::UiLiteral("ingame.nr-input.invalid_signal", "Invalid or blocked"):
                    Neurotic::UiLiteral("ingame.nr-input.no_current_signal", "No current signal"));
                ImGui::TextUnformatted(Neurotic::Translate(lamp.detail).c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            auto* draw = ImGui::GetWindowDrawList();
            const float radius = (std::max)(4.0f, ImGui::GetFontSize() * 0.29f);
            const ImVec2 marker(pos.x + size.x - radius - 3.0f, pos.y + size.y * 0.5f);
            draw->PushClipRect(pos, {marker.x - radius - 3.0f, pos.y + size.y}, true);
            draw->AddText(pos, ImGui::GetColorU32(color(lampState)), Neurotic::Translate(lamp.label).c_str());
            draw->PopClipRect();
            const ImU32 markColor = ImGui::GetColorU32(color(lampState));
            const float stroke = (std::max)(1.5f, radius * 0.38f);
            if (lampState == State::Detected)
                draw->AddCircle(marker,radius*.75f,markColor,0,stroke);
            else if (lampState == State::Ready)
            {
                draw->AddLine({ marker.x - radius, marker.y },
                              { marker.x - radius * 0.2f, marker.y + radius * 0.7f }, markColor, stroke);
                draw->AddLine({ marker.x - radius * 0.2f, marker.y + radius * 0.7f },
                              { marker.x + radius, marker.y - radius * 0.8f }, markColor, stroke);
            }
            else if (lampState == State::Fault)
            {
                draw->AddLine({ marker.x - radius * 0.75f, marker.y - radius * 0.75f },
                              { marker.x + radius * 0.75f, marker.y + radius * 0.75f }, markColor, stroke);
                draw->AddLine({ marker.x - radius * 0.75f, marker.y + radius * 0.75f },
                              { marker.x + radius * 0.75f, marker.y - radius * 0.75f }, markColor, stroke);
            }
            else
                draw->AddLine({ marker.x - radius * 0.75f, marker.y },
                              { marker.x + radius * 0.75f, marker.y }, markColor, stroke);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    const auto detectedLabel=Neurotic::UiLiteral("ingame.nr-input.detected", "Detected, not in use");
    const auto usedLabel=Neurotic::UiLiteral("ingame.nr-input.in_use", "In use");
    const bool legendFits=ImGui::GetContentRegionAvail().x>=ImGui::CalcTextSize(detectedLabel).x+ImGui::CalcTextSize(usedLabel).x+ImGui::GetStyle().ItemSpacing.x;
    ImGui::TextColored(detected,"%s",detectedLabel);
    if(legendFits)ImGui::SameLine();
    ImGui::TextColored(good,"%s",usedLabel);
    if (compact) return;
    return;
    }
    if (view == PreflightView::Footer) return;

    const auto dimensions = [](unsigned int width, unsigned int height)
    {
        if (!width || !height) return std::string(Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed"));
        char text[48] {};
        std::snprintf(text, sizeof(text), Neurotic::UiLiteral("ingame.dlssnr-menu.u_x_u_last_recorded_e96dceef", "%u x %u (last recorded)"), width, height);
        return std::string(text);
    };
    const auto pair = [&](const std::optional<NrPreflightSignals::Pair>& value, bool supplied)
    {
        if (!value) return std::string(supplied ? Neurotic::UiLiteral("ingame.dlssnr-menu.supplied_but_incomplete_or_non_finite_22dbceb0", "Supplied but incomplete or non-finite") : input.observations ?
            Neurotic::UiLiteral("ingame.dlssnr-menu.not_supplied_on_last_native_dlss_input_53146409", "Not supplied on last Native DLSS input") : Neurotic::UiLiteral("ingame.dlssnr-menu.no_native_dlss_input_observed_cc511282", "No Native DLSS input observed"));
        char text[96] {};
        std::snprintf(text, sizeof(text), Neurotic::UiLiteral("ingame.dlssnr-menu.x_4g_y_4g_last_native_dlss_input_dac1e85e", "X %.4g, Y %.4g (last Native DLSS input)"), value->x, value->y);
        return std::string(text);
    };
    const auto table = [&](const char* id, const auto& rows)
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) return;
        ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.provider.1e9806e4227b", "Signal"), ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.dlssnr-menu.owner_observation_c433228c", "Owner observation"), ImGuiTableColumnFlags_WidthStretch, 0.58f);
        rows();
        ImGui::EndTable();
    };

    ImGui::PushTextWrapPos(0.0f);
    // Keep two signal groups on each row at normal menu width. Narrow layouts
    // return to one column so labels and unknown readings remain legible.
    const bool grid = ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 42.0f &&
        ImGui::BeginTable("##PreflightGrid", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX);
    if (grid) ImGui::TableNextColumn();
    ImGui::PushID("##PreflightSourceCard");
    ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.provider.a810cd819631", "Source & output"));
    ImGui::Separator();
    table("##PreflightSource", [&]
    {
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.graphics_path_6c2a4a77", "Graphics path"), ApiUpscalerInputName(host.currentInputApiName), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.game_color_input_0c638760", "Game color input"), Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_independently_f0c99511", "Not observed independently"), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.nr_frame_raster_365a57e0", "NR frame raster"), dimensions(native.frameWidth, native.frameHeight), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.model_work_raster_a321ff7d", "Model work raster"), dimensions(native.workWidth, native.workHeight), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.present_backbuffer_49369a97", "Present backbuffer"), dimensions(present.backbufferWidth, present.backbufferHeight), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.backbuffer_format_7ecea65f", "Backbuffer format"), present.backbufferFormat == DXGI_FORMAT_UNKNOWN ?
                      Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed") : BackbufferFormatName(present.backbufferFormat), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.nr-diagnostics.format_id", "Format ID"), present.api == PresentApi::Vulkan ?
            (present.vkFormatObserved ? std::to_string(static_cast<int>(present.vkBackbufferFormat)) : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed")) :
            (present.backbufferFormat != DXGI_FORMAT_UNKNOWN ? std::to_string(static_cast<int>(present.backbufferFormat)) : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed")), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.nr-diagnostics.samples_swap", "Samples / swap effect"), present.api != PresentApi::Vulkan && present.backbufferWidth ?
            std::to_string(present.backbufferSampleCount) + " / " + std::to_string(static_cast<int>(present.swapEffect)) : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed"), muted);
    });
    ImGui::PopID();

    if (grid) ImGui::TableNextColumn();
    else ImGui::Spacing();
    ImGui::PushID("##PreflightMotionCard");
    ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.provider.9766f12b0b0c", "Motion & history"));
    ImGui::Separator();
    table("##PreflightMotion", [&]
    {
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.depth_motion_guides_ee184e26", "Depth / motion guides"), native.guideWidth && native.guideHeight ?
                      dimensions(native.guideWidth, native.guideHeight) : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed"), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.present_guide_matching_5f800173", "Present guide matching"), guides.captureAttempts ? guides.status : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed"), muted);
        NrDiagnosticsUi::ObservationRow("Jitter", jitterText, muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.motion_scale_72610ac1", "Motion scale"), pair(input.motionScale, input.motionScaleSupplied), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.history_reset_49efc73d", "History reset"), !native.frames && !present.presentAttempts ? Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed") :
                      native.historyResetRequested || present.historyResetPending ?
                      Neurotic::UiLiteral("ingame.dlssnr-menu.requested_pending_owner_report_271a470a", "Requested / pending (owner report)") : Neurotic::UiLiteral("ingame.dlssnr-menu.no_reset_pending_reported_0f9b2595", "No reset pending reported"), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.game_reset_events_79d9a531", "Game reset events"), native.frames ? std::to_string(native.gameResets) + Neurotic::UiLiteral("ingame.dlssnr-menu.session_count_c8eb7841", " (session count)") :
                      Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed"), muted);
    });
    ImGui::PopID();

    if (grid) ImGui::TableNextColumn();
    else ImGui::Spacing();
    ImGui::PushID("##PreflightSceneCard");
    ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.provider.55b016366458", "Scene & color"));
    ImGui::Separator();
    table("##PreflightScene", [&]
    {
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.exposure_texture_9cad026a", "Exposure texture"), input.observations ?
                      (input.exposureTexture ? Neurotic::UiLiteral("ingame.dlssnr-menu.offered_on_last_native_dlss_input_bbbbcf0d", "Offered on last Native DLSS input") : Neurotic::UiLiteral("ingame.dlssnr-menu.absent_on_last_native_dlss_input_34e7c6ae", "Absent on last Native DLSS input")) :
                      Neurotic::UiLiteral("ingame.dlssnr-menu.no_native_dlss_input_observed_cc511282", "No Native DLSS input observed"), muted);
        std::string preExposure = input.preExposureSupplied ? Neurotic::UiMessage("ingame.dlssnr-menu.supplied_but_non_finite_6f328188", "Supplied but non-finite") :
            input.observations ? Neurotic::UiLiteral("ingame.dlssnr-menu.not_supplied_on_last_native_dlss_input_53146409", "Not supplied on last Native DLSS input") : Neurotic::UiLiteral("ingame.dlssnr-menu.no_native_dlss_input_observed_cc511282", "No Native DLSS input observed");
        if (input.preExposure)
        {
            char text[96] {};
            std::snprintf(text, sizeof(text), Neurotic::UiLiteral("ingame.dlssnr-menu.6g_last_native_dlss_input_3463766b", "%.6g (last Native DLSS input)"), *input.preExposure);
            preExposure = text;
        }
        NrDiagnosticsUi::ObservationRow("Pre-exposure", preExposure, muted);
        std::string exposureValue = Neurotic::UiMessage("ingame.dlssnr-menu.no_valid_texture_readback_recorded_2617a4f1", "No valid texture readback recorded");
        if (exposure.everOffered && std::isfinite(exposure.exposure) && exposure.exposure > 0.0f)
        {
            char text[96] {};
            std::snprintf(text, sizeof(text), Neurotic::UiLiteral("ingame.dlssnr-menu.6g_last_readback_may_be_held_f0780712", "%.6g (last readback; may be held)"), exposure.exposure);
            exposureValue = text;
        }
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.exposure_value_b85b546a", "Exposure value"), exposureValue, muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.output_color_space_de9a56cd", "Output color space"), present.api == PresentApi::Vulkan ?
                      (present.vkFormatObserved ? Neurotic::UiLiteral("ingame.dlssnr-menu.vulkan_format_and_color_space_observed_f3b97dc3", "Vulkan format and color space observed") : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed")) :
                      (present.colorSpaceObserved ? Neurotic::UiLiteral("ingame.dlssnr-menu.owner_observed_a_dxgi_color_space_608bc128", "Owner observed a DXGI color space") : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed")), muted);
        NrDiagnosticsUi::ObservationRow("Normals / roughness / albedo", Neurotic::UiLiteral("ingame.dlssnr-menu.no_adapter_on_this_path_0bc263ec", "No adapter on this path"), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.nr-diagnostics.color_space_id", "Color space ID"), present.api == PresentApi::Vulkan ?
            (present.vkFormatObserved ? std::to_string(static_cast<int>(present.vkColorSpace)) : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed")) :
            (present.colorSpaceObserved ? std::to_string(static_cast<int>(present.colorSpace)) : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed")), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.reactive_transparency_masks_3a99b668", "Reactive / transparency masks"), Neurotic::UiLiteral("ingame.dlssnr-menu.no_adapter_on_this_path_0bc263ec", "No adapter on this path"), muted);
    });
    ImGui::PopID();

    if (grid) ImGui::TableNextColumn();
    else ImGui::Spacing();
    ImGui::PushID("##PreflightRouteCard");
    ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.nr-diagnostics.other_processing", "Other processing"));
    ImGui::Separator();
    table("##PreflightRoute", [&]
    {
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.ray_reconstruction_ab6479c7", "Ray Reconstruction"), native.nativeRayReconstructionActive ? Neurotic::UiLiteral("ingame.dlssnr-menu.native_owner_reports_active_f0d00be8", "Native owner reports active") :
                      Neurotic::UiLiteral("ingame.dlssnr-menu.no_active_native_rr_report_222ed1e0", "No active native RR report"), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.frame_generation_c41396f4", "Frame Generation"), fg ? Neurotic::UiLiteral("ingame.dlssnr-menu.native_dlssg_accepted_an_enabled_mode_deba27a5", "Native DLSSG accepted an enabled mode") :
                      Neurotic::UiLiteral("ingame.dlssnr-menu.no_enabled_native_dlssg_observation_other_provid_3a4e2444", "No enabled native DLSSG observation; other providers unverified"), muted);
        NrDiagnosticsUi::ObservationRow(Neurotic::UiLiteral("ingame.dlssnr-menu.fg_provider_7d7261d0", "FG provider"), fg ? Neurotic::UiLiteral("ingame.dlssnr-menu.native_dlssg_enabled_fbb0f039", "Native DLSSG enabled") : configuredFg ?
                      std::string(magic_enum::enum_name(host.activeFgInput)) + Neurotic::UiLiteral("ingame.dlssnr-menu.configured_activity_unverified_c85aee4e", " (configured; activity unverified)") :
                      fgProvider.known ? Neurotic::UiLiteral("ingame.dlssnr-menu.native_dlssg_disabled_ca784141", "Native DLSSG disabled") : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed"), muted);
    });
    ImGui::PopID();
    if (grid) ImGui::EndTable();
    ImGui::PopTextWrapPos();
}

} // namespace

enum class NrReadoutGroup { Activity, Output };

static void RenderLiveReadouts(Config* config, NrConfigSnapshot<Config> uiConfig, bool enabled,
                               bool basicOwnsMain, NrReadoutGroup group,
                               const std::optional<MenuStatus::RuntimeStatus>& status)
{
    if (basicOwnsMain) BasicMultipass::Derive(uiConfig);
    const auto nrTelemetry = SelectedNativeTelemetry();
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
        presentTelemetry.requestedPlacement == "Present";
    const bool presentActive = presentMatches && presentTelemetry.active &&
        (vulkan || (presentTelemetry.resolution == policy.mode && presentTelemetry.workload == policy.scale));
    const auto outputSnapshot=RenderingOutput::Query();
    const auto outputNow=GetTickCount64();
    const bool outputActive=enabled && RenderingOutput::Active(outputSnapshot,outputNow);
    const bool nativeOutput=outputActive && outputSnapshot.producer==RenderingOutput::Producer::Native;

    if (group == NrReadoutGroup::Activity)
    {
    StatusPanel::Linef("##NrState", outputActive ? green :
        (RenderingOutput::Fresh(outputSnapshot,outputNow) && outputSnapshot.phase==RenderingOutput::Phase::Blocked ? red : yellow),
        RenderingOutput::StatusLabel(outputSnapshot,outputNow));
    if (!outputSnapshot.reason.empty() && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*30.0f);
        const auto reason=RenderingOutput::ReasonLabel(outputSnapshot.reason);
        ImGui::TextUnformatted(reason.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }

    static std::optional<NrConfigSnapshot<Config>> timingSettings;
    static bool timingVulkan=false;
    static uint64_t timingSelection=0;
    const std::array<uint64_t,7> timingContext={nrTelemetry.frameWidth,nrTelemetry.frameHeight,
        nrTelemetry.workWidth,nrTelemetry.workHeight,nrTelemetry.gameResets,
        nrTelemetry.featureBuilds,nrTelemetry.featureRebuilds};
    static std::array<uint64_t,7> previousTimingContext{};
    if (previousTimingContext!=timingContext) { previousTimingContext=timingContext; ++timingSelection; }
    if (!timingSettings || !timingSettings->SameConfiguration(uiConfig) || timingVulkan!=vulkan) {
        timingSettings=uiConfig; timingVulkan=vulkan; ++timingSelection;
    }
    static MenuStatus::TimingObservation nativeTiming;
    static MenuStatus::ActivityObservation timingProgress;
    const bool timingFresh=timingProgress.Fresh(timingSelection,nrTelemetry.frames,
        ImGui::GetTime(),nrTelemetry.lifecycleGeneration);
    const bool timingActive=nativeOutput && timingFresh && !nrTelemetry.failed && !nrTelemetry.resetPending;
    // Vulkan already retains exact-context samples in its owner. Its model/guide generation
    // is finer than the public lifecycle epoch, so never bridge a Vulkan refusal here.
    const auto retainedTime=nativeTiming.Update(timingSelection,nrTelemetry.lifecycleGeneration,
        timingActive && !vulkan,nrTelemetry.totalGpuMs,ImGui::GetTime());
    const auto nativeTime=vulkan ? nrTelemetry.totalGpuMs : retainedTime;
    if (timingActive && nativeTime)
        StatusPanel::Linef("##NrTiming", green, Neurotic::UiLiteral("ingame.dlssnr-menu.nr_processing_2f_ms_per_frame_a5ee5276", "NR processing: %.2f ms per frame"), *nativeTime);
    else if (enabled && !presentRoute)
        StatusPanel::Linef("##NrTiming", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                           Neurotic::UiLiteral("ingame.dlssnr-menu.nr_processing_waiting_for_gpu_timing_83d6b91f", "NR processing: waiting for GPU timing"));
    else if (enabled && presentActive && presentTelemetry.presentGpuValid &&
             presentTelemetry.presentGpuRoute == (route == 2
                 ? PresentPacing::Route::PresentEnhanced
                 : PresentPacing::Route::PresentImageOnly))
        StatusPanel::Linef("##NrTiming", green, Neurotic::UiLiteral("ingame.dlssnr-menu.nr_processing_2f_ms_per_frame_a5ee5276", "NR processing: %.2f ms per frame"), presentTelemetry.presentGpuMs);
    else
        StatusPanel::Line("##NrTiming", {}, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }

    if (group == NrReadoutGroup::Output)
    {
    uint32_t actualW = 0, actualH = 0;
    uint32_t outputW = presentTelemetry.backbufferWidth, outputH = presentTelemetry.backbufferHeight;
    if (presentActive) { actualW = presentTelemetry.workWidth; actualH = presentTelemetry.workHeight; }
    else if (nativeOutput)
    {
        actualW = nrTelemetry.workWidth; actualH = nrTelemetry.workHeight;
        const auto feature = State::Instance().currentFeature;
        const bool before = renderMode != 0 && !nrTelemetry.nativeRayReconstructionActive;
        outputW = before ? (feature ? feature->DisplayWidth() : 0u) : nrTelemetry.frameWidth;
        outputH = before ? (feature ? feature->DisplayHeight() : 0u) : nrTelemetry.frameHeight;
    }
    if (enabled && status && nativeOutput)
    {
        outputW = status->outputWidth; outputH = status->outputHeight;
        const bool active = nativeOutput;
        actualW = active ? status->workWidth : 0u; actualH = active ? status->workHeight : 0u;
    }
    StatusPanel::Line("##NrDimensions", StageUi::DimensionText(actualW, actualH, outputW, outputH),
                      ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    std::string notices;
    const auto note = [&](const char* text) {
        if (!notices.empty()) notices += "\n";
        notices += Neurotic::Translate(text);
    };
    if (!presentRoute && (vulkan ? DlssNr::NativeRayReconstructionVk() : nrTelemetry.nativeRayReconstructionActive))
        note(Neurotic::UiLiteral("ingame.dlssnr-menu.ray_reconstruction_keeps_nr_after_reconstruction_3f5168a0", "Ray Reconstruction keeps NR after reconstruction. Before-stage placement is unavailable on this path."));
    if (enabled && !config->DlssNrApplyModel.value_or_default())
        note(Neurotic::UiLiteral("ingame.dlssnr-menu.model_effect_hidden_enable_apply_the_model_to_sh_36252012", "Model effect hidden. Enable Apply the model to show it."));
    if (!notices.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, yellow);
        ImGui::TextWrapped("%s",Neurotic::Translate(notices.c_str()).c_str());
        ImGui::PopStyleColor();
    }
    }
}

void TickAdvisor(Config* config)
{
    auto& advisor = Advisor();
    if (config == nullptr) return;
    const auto present = DlssNr::PresentTelemetry();
    if (!advisor.running)
    {
        if (advisor.analyzed && advisor.coverageSettings)
        {
            const auto current = TryNrConfigSnapshot(*config);
            if (!current || !advisor.coverageSettings->SameConfiguration(*current) ||
                advisor.providerGeneration != AdvisorProviderGeneration(present) ||
                advisor.rayReconstruction != SelectedNativeTelemetry().nativeRayReconstructionActive ||
                advisor.lifecycleGeneration != SelectedNativeTelemetry().lifecycleGeneration ||
                advisor.fgMode != State::Instance().dlssgLastSetMode.load() ||
                advisor.fgInput != State::Instance().activeFgInput || advisor.fgOutput != State::Instance().activeFgOutput ||
                advisor.fgRatio != config->FGDLSSGInterpolationCount.value_or_default() ||
                advisor.xeRatio != config->FGXeFGInterpolationCount.value_or_default() ||
                (advisor.originalWidth && present.backbufferWidth != advisor.originalWidth) ||
                (advisor.originalHeight && present.backbufferHeight != advisor.originalHeight))
            {
                advisor.routes = {}; advisor.analyzed = false; advisor.recommendation = -1;
                advisor.appliedRoute = -1; advisor.coverageSettings.reset();
                advisor.status = Neurotic::UiMessage("ingame.dlssnr-menu.settings_or_rendering_context_changed_previous_t_9b39e9e5", "Settings or rendering context changed; previous tests are no longer current.");
            }
        }
        static uint64_t lastBaseline = 0;
        if (present.cadence.sequence != lastBaseline && present.cadence.native &&
            std::isfinite(present.cadence.intervalMs) && present.cadence.intervalMs > 0)
        {
            lastBaseline = present.cadence.sequence;
            advisor.baselineIntervals.push_back(present.cadence.intervalMs);
            if (advisor.baselineIntervals.size() > 120) advisor.baselineIntervals.erase(advisor.baselineIntervals.begin());
        }
        return;
    }
    const auto currentSettings = TryNrConfigSnapshot(*config);
    const auto lifecycle = SelectedNativeTelemetry();
    if (!currentSettings || !advisor.expectedSettings ||
        !advisor.expectedSettings->SameConfiguration(*currentSettings) ||
        !lifecycle.lifecycleOpen || lifecycle.lifecycleGeneration != advisor.lifecycleGeneration)
    {
        CancelAdvisorAnalysis(config, Neurotic::UiLiteral("ingame.dlssnr-menu.route_settings_or_rendering_session_changed_orig_d7d4842f", "Route, settings or rendering session changed; original settings were restored."));
        return;
    }
    if ((advisor.originalWidth && present.backbufferWidth && advisor.originalWidth != present.backbufferWidth) ||
        (advisor.originalHeight && present.backbufferHeight && advisor.originalHeight != present.backbufferHeight))
    {
        CancelAdvisorAnalysis(config, Neurotic::UiLiteral("ingame.dlssnr-menu.output_size_changed_analysis_stopped_and_origina_ef342ece", "Output size changed; analysis stopped and original settings were restored."));
        return;
    }
    if (State::Instance().isShuttingDown)
    {
        CancelAdvisorAnalysis(config, Neurotic::UiLiteral("ingame.dlssnr-menu.rendering_device_is_shutting_down_original_setti_753d44c0", "Rendering device is shutting down; original settings were restored."));
        return;
    }
    const double now = AdvisorNow();
    const double elapsed = now - advisor.phaseStarted;
    const double tickMs = (now - advisor.lastTick) * 1000.0;
    advisor.lastTick = now;
    const bool fresh = advisor.sampling.Consume(present.cadence);
    if (advisor.sampling.RejectStall(advisor.phase == AdvisorPhase::Sample,
                                    tickMs, fresh, present.cadence.intervalMs))
    {
        LOG_WARN("Advisor stalled route={} sinceStart={:.3f}s tick={:.3f}ms frame={:.3f}ms",
            advisor.routeIndex, now - advisor.testStarted, tickMs, present.cadence.intervalMs);
        FailAdvisorRoute(*config, Neurotic::UiLiteral("ingame.dlssnr-menu.test_interrupted_by_a_rendering_stall_unmeasured_022c7da6", "Test interrupted by a rendering stall; unmeasured. Original settings restored."));
        return;
    }
    if (AdvisorProviderGeneration(present) != advisor.providerGeneration ||
        advisor.fgInput != State::Instance().activeFgInput || advisor.fgOutput != State::Instance().activeFgOutput ||
        advisor.fgMode != State::Instance().dlssgLastSetMode.load() ||
        advisor.rayReconstruction != lifecycle.nativeRayReconstructionActive ||
        advisor.fgRatio != config->FGDLSSGInterpolationCount.value_or_default() ||
        advisor.xeRatio != config->FGXeFGInterpolationCount.value_or_default())
    {
        CancelAdvisorAnalysis(config, Neurotic::UiLiteral("ingame.dlssnr-menu.fg_provider_changed_test_unmeasured_and_original_662755f8", "FG provider changed; test unmeasured and original settings restored."));
        return;
    }
    const auto completedObservation = advisor.routeIndex == 0 ? lifecycle.gpuCompletedOutputEvaluations : present.presentGpuSamples;
    const bool matchingOutput = completedObservation > advisor.lastCompletedObservation && (advisor.routeIndex == 0
        ? lifecycle.running && !lifecycle.failed && !lifecycle.transitionPending && !lifecycle.outputQuarantined &&
          lifecycle.gpuCompletedOutputEvaluations > advisor.startNativeFrames
        : present.active && !present.failed && present.presentGpuValid &&
          static_cast<unsigned int>(present.presentGpuRoute) == unsigned(advisor.routeIndex) &&
          present.modelEvaluations > advisor.startPresentEvaluations);
    const bool matchingCadence = fresh && present.cadence.route == unsigned(advisor.routeIndex) &&
        present.cadence.configurationGeneration == advisor.configurationGeneration;
    advisor.receivedOutput = advisor.receivedOutput || matchingOutput;
    if (matchingCadence && matchingOutput) advisor.lastCompletedObservation = completedObservation;
    if (advisor.ready && present.resourceGeneration != advisor.resourceGeneration)
    {
        CancelAdvisorAnalysis(config, Neurotic::UiLiteral("ingame.dlssnr-menu.rendering_resources_changed_test_unmeasured_and__5069ea87", "Rendering resources changed; test unmeasured and original settings restored."));
        return;
    }
    if (advisor.phase == AdvisorPhase::Warmup)
    {
        if (!advisor.ready && matchingOutput && matchingCadence)
        {
            advisor.ready = true;
            advisor.resourceGeneration = present.resourceGeneration;
            advisor.transitionSeconds = now - advisor.testStarted;
            advisor.phaseStarted = now;
        }
        const bool policyBlocked = present.policyBlocked && AdvisorPolicy::CurrentFailure(
            advisor.routeIndex, advisor.configurationGeneration, advisor.startPresentAttempts,
            present.cadence, present.lastFallbackAttempt);
        if (!advisor.ready && (policyBlocked || advisor.sampling.StartupExpired(now - advisor.testStarted)))
        {
            const auto reason = AdvisorFeedbackFailure(present);
            FailAdvisorRoute(*config, reason.c_str());
            return;
        }
        if (advisor.ready)
        {
            if (advisor.sampling.WarmupFrame(matchingCadence && matchingOutput, fresh,
                                            present.cadence.intervalMs, tickMs))
            {
                advisor.phase = AdvisorPhase::Sample;
                advisor.phaseStarted = now;
                advisor.routes[advisor.routeIndex].detail = Neurotic::UiMessage("ingame.dlssnr-menu.measuring_fresh_native_frames_1bfab15f", "Measuring fresh native frames...");
            }
            else if (now - advisor.phaseStarted >= 15.0)
                FailAdvisorRoute(*config, Neurotic::UiLiteral("ingame.dlssnr-menu.stable_warmup_did_not_complete_unmeasured_origin_12231ed1", "Stable warmup did not complete; unmeasured. Original settings restored."));
        }
    }
    else if (advisor.phase == AdvisorPhase::Sample)
    {
        if (matchingCadence && matchingOutput)
        {
            advisor.sampling.totalMs += present.cadence.intervalMs;
            ++advisor.sampling.samples;
            advisor.frameIntervalTotal += present.cadence.intervalMs;
            ++advisor.frameIntervalSamples;
        }

        if (advisor.routeIndex == 0)
        {
            const auto native = SelectedNativeTelemetry();
            if (native.gpuCompletedOutputEvaluations > advisor.lastNativeGpuFrame)
            {
                advisor.lastNativeGpuFrame = native.gpuCompletedOutputEvaluations;
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

        if (advisor.sampling.Complete(elapsed))
            FinishAdvisorRoute(*config);
        else if (elapsed >= 15.0)
            FailAdvisorRoute(*config, Neurotic::UiLiteral("ingame.dlssnr-menu.insufficient_fresh_native_frames_before_the_samp_b203d822", "Insufficient fresh native frames before the sample deadline; unmeasured. Original settings restored."));
    }
}

void CancelAdvisorAnalysis(Config* config, const char* reason)
{
    auto& advisor = Advisor();
    if (!advisor.running || config == nullptr) return;
    RestoreAdvisorSettings(*config, advisor.original);
    advisor.running = false;
    advisor.analyzeAll = false;
    advisor.phase = AdvisorPhase::Idle;
    advisor.analyzed = false;
    advisor.recommendation = -1;
    advisor.appliedRoute = -1;
    advisor.routes[advisor.routeIndex] = {};
    advisor.routes[advisor.routeIndex].detail = Neurotic::UiMessage("ingame.dlssnr-menu.interrupted_not_measured_bb776677", "Interrupted - not measured");
    advisor.status = reason != nullptr ? reason : Neurotic::UiLiteral("ingame.dlssnr-menu.analysis_cancelled_original_settings_restored_460fc72e", "Analysis cancelled; original settings restored.");
    advisor.reason = Neurotic::UiMessage("ingame.dlssnr-menu.no_recommendation_was_applied_95c6c94d", "No recommendation was applied.");
}

static void RenderIntakeMenu(Config* config, const char* gpuName,
                             const std::optional<MenuStatus::RuntimeStatus>& status)
{
    ScopedNestedTextWrap wrap;
    // The Diagnostics board remains the single renderer. This compact copy has
    // an independent persisted disclosure and no diagnostic counters/actions.
    BeginNrCard("##NrIntakeSignalCard", nullptr);
    bool signalsExpanded=config->MenuPreflightExpanded.value_or_default();
    if(ImGui::BeginTable("##NrSignalHeading",2,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("##Title",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##Disclosure",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFrameHeight());
        ImGui::TableNextColumn();
        Neurotic::Sleek::WindowSectionHeader(Neurotic::UiLiteral("ingame.nr-input.signals", "Signals"));
        ImGui::TableNextColumn();
        if(ImGui::ArrowButton("##ToggleSignals",signalsExpanded?ImGuiDir_Up:ImGuiDir_Down)) {
            signalsExpanded=!signalsExpanded;
            config->MenuPreflightExpanded=signalsExpanded;
            config->SaveIni();
        }
        ImGui::EndTable();
    }
    {
        Neurotic::Sleek::AnimatedRegion signals("##NrIntakeSignalBody",signalsExpanded);
        RenderPreflight(config,PreflightView::Signals,signals.Visible(),true);
    }
    EndNrCard();
    auto settings = config->GetDlssNrConfigSnapshot();
    const auto present = PresentTelemetry();
    const bool observationVulkan = Capability::ObservationUsesVulkan(settings.DlssNrRoute.value_or_default(),
        IsVulkanInput(), present.api != PresentApi::Unknown, present.api == PresentApi::Vulkan);
    const auto native = observationVulkan ? NativeTelemetryVk() : Telemetry();
    const auto guides = PresentGuides::Instance().Inspect();
    ImGui::Spacing();
    const bool grid = ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 44.0f &&
        ImGui::BeginTable("##NrIntakeGrid", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX);
    if (grid) ImGui::TableNextColumn();
    BeginNrCard("##NrInputSource", Neurotic::UiLiteral("ingame.dlssnr-menu.input_source_1d59f442", "Input source"));
    const int method = StageUi::UnifiedMethodSelection(settings);
#ifdef _WIN64
    int inputSource = config->DlssNrInputSource.value_or_default();
    Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.dlssnr-menu.input_source_1d59f442", "Input source"));
    if (ImGui::Combo("##Input source", &inputSource, Neurotic::UiOptions("ingame.dlssnr-menu.automatic_33f5300c|ingame.dlssnr-menu.native_bf0ec6da|ingame.dlssnr-menu.built_in_capture_8d435da4|ingame.dlssnr-menu.reshade_4b3aa516|ingame.dlssnr-menu.external_ab876246|", "Automatic\0Native\0Built-in capture\0ReShade\0External\0"))) {
        config->DlssNrInputSource = inputSource; config->SaveIni(); CancelNrEdits();
        Connections::RequestInputSelection(inputSource,config->DlssNrInputTransport.value_or_default(),config->DlssNrAllowCpuFallback.value_or_default());
    }
    if(ImGui::IsItemHovered()||ImGui::IsItemFocused()) {
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*28.f);
        ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.nr-input.selection_help", "Automatic prefers qualified game inputs, then built-in capture. Manual choices stay selected when unavailable."));
        ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
    bool standalone=config->DlssNrNativeFrameGeneration.value_or_default();
    ImGui::BeginDisabled(inputSource!=2);
    if(ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.standalone_fsr_frame_generation_2x_1ffa262a", "Standalone FSR frame generation (2x)"),&standalone)){
        config->DlssNrNativeFrameGeneration=standalone;config->SaveIni();CancelNrEdits();
    }
    ImGui::EndDisabled();
    if(inputSource!=2)ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.standalone_fg_requires_built_in_capture_at_game__eb0c3a45", "Standalone FG requires Built-in capture at game startup."));
    const auto fgProgress=NativeFg::Progress();
    static NativeFg::PresentationObservation fgObservation;
    const auto fgState=fgObservation.Update(standalone,NativeFg::Selected(),fgProgress.owners!=0,
        fgProgress.identity,fgProgress.physical.generatedRetired,ImGui::GetTime());
    const auto fgColor=fgState==NativeFg::PresentationState::Generating?ImVec4(.30f,.82f,.48f,1):
        fgState==NativeFg::PresentationState::Off?ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled):ImVec4(1.f,.72f,.25f,1);
    ImGui::TextColored(fgColor,Neurotic::UiLiteral("ingame.dlssnr-menu.universal_fg_s_1a3f9d0e", "Universal FG: %s"),Neurotic::Translate(NativeFg::PresentationLabel(fgState)).c_str());
    if(standalone && fgState!=NativeFg::PresentationState::Generating && fgState!=NativeFg::PresentationState::Off){
        auto reason=fgProgress.reason.substr(0,fgProgress.reason.find('\n'));
        ImGui::TextWrapped("%s",Neurotic::Translate(fgState==NativeFg::PresentationState::RestartRequired?
            Neurotic::UiLiteral("ingame.dlssnr-menu.restart_the_game_to_apply_the_frame_generation_s_1f97d457", "Restart the game to apply the frame generation setting."):reason.c_str()).c_str());
    }
    if(ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.advanced_input_options_acac1b9d", "Advanced input options")))ImGui::OpenPopup(Neurotic::UiLiteral("ingame.dlssnr-menu.advanced_input_options_acac1b9d", "Advanced input options##NrInputOverlay"));
    Neurotic::SetBoundedPopupSize(34.f,29.f);
    if(ImGui::BeginPopupModal(Neurotic::UiLiteral("ingame.dlssnr-menu.advanced_input_options_acac1b9d", "Advanced input options##NrInputOverlay"),nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings)){
        ImGui::BeginChild("##NrInputOptionsBody",{0,-ImGui::GetFrameHeightWithSpacing()});
        ImGui::PushTextWrapPos(0);
        int inputTransport=config->DlssNrInputTransport.value_or_default();
        Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.dlssnr-menu.input_transport_eb183b9c", "Input transport"));
        if(ImGui::Combo("##Input transport",&inputTransport,Neurotic::UiOptions("ingame.dlssnr-menu.automatic_33f5300c|ingame.dlssnr-menu.gpu_only_ac2ce5ad|ingame.dlssnr-menu.cpu_d30a8cca|", "Automatic\0GPU only\0CPU\0"))){
            config->DlssNrInputTransport=inputTransport;config->SaveIni();CancelNrEdits();
            Connections::RequestInputSelection(inputSource,inputTransport,config->DlssNrAllowCpuFallback.value_or_default());
        }
        bool fallback=config->DlssNrAllowCpuFallback.value_or_default();
        if(Neurotic::WrappedPopupCheckbox(Neurotic::UiLiteral("ingame.dlssnr-menu.allow_clean_cpu_fallback_00d02e41", "Allow clean CPU fallback"),&fallback)){
            config->DlssNrAllowCpuFallback=fallback;config->SaveIni();CancelNrEdits();
            Connections::RequestInputSelection(inputSource,inputTransport,fallback);
        }
        if(inputSource==0||inputSource==2){
            ImGui::SeparatorText(Neurotic::UiLiteral("ingame.dlssnr-menu.built_in_capture_8d435da4", "Built-in capture"));
            bool vulkanRenderer=config->DlssNrNativeVulkanRenderer.value_or_default();
            if(Neurotic::WrappedPopupCheckbox(Neurotic::UiLiteral("ingame.dlssnr-menu.vulkan_nr_renderer_be792456", "Vulkan NR renderer"),&vulkanRenderer)){
                config->DlssNrNativeVulkanRenderer=vulkanRenderer;config->SaveIni();CancelNrEdits();
            }
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.renderer_changes_require_restart_standalone_fg_u_ff15e674", "Renderer changes require restart. Standalone FG uses the D3D12 renderer."));
            int direction=config->DlssNrNativeDepthDirection.value_or_default();
            int selection=direction==0?1:direction==1?2:0;
            Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.dlssnr-menu.depth_direction_7b0b4349", "Depth direction"));
            if(ImGui::Combo("##Depth direction",&selection,Neurotic::UiOptions("ingame.dlssnr-menu.automatic_33f5300c|ingame.dlssnr-menu.forward_far_1_896a5ee1|ingame.dlssnr-menu.reversed_far_0_0d398930|", "Automatic\0Forward (far = 1)\0Reversed (far = 0)\0"))){
                config->DlssNrNativeDepthDirection=selection-1;NativeGuides::ConfigureDepthDirection(selection-1);
                NativeVulkanGuides::Unavailable(Neurotic::UiLiteral("ingame.dlssnr-menu.depth_direction_changed_waiting_for_a_fresh_fram_5718f375", "Depth direction changed; waiting for a fresh frame"));config->SaveIni();CancelNrEdits();
            }
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.automatic_uses_an_observed_clear_manual_depth_di_3c6c3442", "Automatic uses an observed clear. Manual depth direction applies immediately; an incorrect convention can distort rendering."));
        }
        RenderInputSelectionStatus();
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        if(ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.done_f2fca027", "Done"))||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    RenderInputSelectionStatus();

#endif
    static const char* policies[] = { Neurotic::UiLiteral("ingame.dlssnr-menu.image_only_095e6ab0", "Image only"), Neurotic::UiLiteral("ingame.dlssnr-menu.require_guides_dcdf9e75", "Require guides"), Neurotic::UiLiteral("ingame.dlssnr-menu.automatic_33f5300c", "Automatic"), Neurotic::UiLiteral("ingame.dlssnr-menu.invalid_saved_policy_195d379d", "Invalid saved policy") };
    int policy = int((std::min)(settings.DlssNrPresentInputPolicy.value_or_default(), 3u));
    ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.dlssnr-menu.missing_guide_policy_2453b31e", "Missing-guide policy"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(method != 0);
    if (ImGui::BeginCombo("##NrIntakePolicy", policies[policy]))
    {
        for (int choice : { 2, 1, 0 })
            if (ImGui::Selectable(policies[choice], choice == policy))
                config->DlssNrPresentInputPolicy = uint32_t(choice);
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    Neurotic::Sleek::ControlDivider();
    if (ImGui::BeginTable("##NrIntakeSource", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
    {
        const auto muted = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        ObservationSignal(Neurotic::UiLiteral("ingame.dlssnr-menu.graphics_api_66585b5d", "Graphics API"), ApiUpscalerInputName(State::Instance().currentInputApiName), muted);
        ObservationSignal(Neurotic::UiLiteral("ingame.dlssnr-menu.graphics_card_70c0d519", "Graphics card"), gpuName && *gpuName ? gpuName : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed"), muted);
        ObservationSignal(Neurotic::UiLiteral("ingame.menu-common.output_b5db16a0", "Output"), Neurotic::Translate(StageUi::UnifiedMethods[method]), muted);
        ImGui::EndTable();
    }
    EndNrCard();

    if (grid) ImGui::TableNextColumn();
    BeginNrCard("##NrIntakeObservation", Neurotic::UiLiteral("ingame.dlssnr-menu.observed_inputs_62aa563e", "Observed inputs"));
    const auto connection=PreparedGuides::QueryStatusV2(GetTickCount64());
    const auto output=RenderingOutput::Query();
    const bool outputActive=RenderingOutput::Active(output,GetTickCount64());
    const bool preparedActive=outputActive&&output.producer==RenderingOutput::Producer::BuiltIn&&connection.available&&connection.fresh;
    const char* effectiveLabel=Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed");
    if(outputActive) {
        if(preparedActive)effectiveLabel=ConnectionSourceLabel(connection.status.selectedSource);
        else if(output.producer==RenderingOutput::Producer::Native||
            (output.producer==RenderingOutput::Producer::Present&&present.actualInputClass==PresentInputDecision::InputClass::Guided))
            effectiveLabel=ConnectionSourceLabel(Connections::Source::Native);
        else if(output.producer==RenderingOutput::Producer::Present)effectiveLabel=Neurotic::UiLiteral("ingame.dlssnr-menu.present_image_ca2fa26e", "Present image");
        else if(output.producer==RenderingOutput::Producer::CapturedImage)effectiveLabel=Neurotic::UiLiteral("ingame.nr-input.captured_image", "Captured image");
    }
    StatusPanel::Linef("##EffectiveInput",outputActive?ImVec4(.30f,.82f,.48f,1):ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
        Neurotic::UiLiteral("ingame.dlssnr-menu.effective_source_s_s_f3f4296e", "Effective source: %s%s"),Neurotic::Translate(effectiveLabel).c_str(),"");
    StatusPanel::Linef("##ProcessInput",ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
        Neurotic::UiLiteral("ingame.nr-input.current_preference", "Current preference: %s"),
        Neurotic::Translate(ConnectionSourceLabel(static_cast<Connections::Source>(NativeGuides::SelectedSource()))).c_str());
    if(preparedActive){
        const auto& effective=connection.status;
        const bool ready=connection.fresh&&effective.guideReady;
        StatusPanel::Linef("##EffectiveGuides",ready?ImVec4(.30f,.82f,.48f,1):ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
            Neurotic::UiLiteral("ingame.provider.e46ec0311ecf", "Guides: %s"),Neurotic::Translate(ready?
                (effective.motionOrigin==PreparedGuides::Origin::Derived?Neurotic::UiLiteral("ingame.dlssnr-menu.captured_depth_estimated_motion_e41ba66f", "Captured depth / estimated motion"):
                 Neurotic::UiLiteral("ingame.nr-input.qualified_guides", "Qualified source guides")):
                Neurotic::UiLiteral("ingame.dlssnr-menu.waiting_for_fresh_inputs_6710d65c", "Waiting for fresh inputs")).c_str());
    }else StatusPanel::Line("##EffectiveGuides",outputActive&&
        ((output.producer==RenderingOutput::Producer::Present&&present.actualInputClass==PresentInputDecision::InputClass::Guided)||
         (output.producer==RenderingOutput::Producer::Native&&native.guideWidth&&native.guideHeight))?
        Neurotic::UiLiteral("ingame.nr-input.game_guides", "Guides: Game-supplied"):
        Neurotic::UiLiteral("ingame.nr-input.no_guides", "Guides: Not in use"),ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    RenderLiveReadouts(config,settings,settings.DlssNrEnabled.value_or_default(),BasicMultipass::Active(settings),NrReadoutGroup::Output,status);

    if (ImGui::BeginTable("##NrIntakeSignals", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH))
    {
        ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.menu-common.input_f238798e", "Input")); ImGui::TableSetupColumn(Neurotic::UiLiteral("ingame.provider.d239e9cf6a51", "Observation")); ImGui::TableHeadersRow();
        const auto muted = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        ObservationSignal(Neurotic::UiLiteral("ingame.dlssnr-menu.native_input_764bf430", "Native input"), native.nativeInputs.observations ? Neurotic::UiLiteral("ingame.dlssnr-menu.recorded_by_the_native_owner_655dd565", "Recorded by the Native owner") : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed"), muted);
        ObservationSignal(Neurotic::UiLiteral("ingame.dlssnr-menu.present_image_ca2fa26e", "Present image"), present.backbufferWidth && present.backbufferHeight ?
            StatusPanel::Format(Neurotic::UiLiteral("ingame.dlssnr-menu.u_x_u_last_recorded_e96dceef", "%u x %u (last recorded)"), present.backbufferWidth, present.backbufferHeight) : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed"), muted);
        ObservationSignal(Neurotic::UiLiteral("ingame.dlssnr-menu.guide_matching_4c4a9f9b", "Guide matching"), guides.captureAttempts ? guides.status : Neurotic::UiLiteral("ingame.dlssnr-menu.not_observed_bc6a9be6", "Not observed"), muted);
        ObservationSignal("Fallback", present.fallbackReason.empty() ? Neurotic::UiLiteral("ingame.dlssnr-menu.no_reason_published_1f91d783", "No reason published") : present.fallbackReason, muted);
        ImGui::EndTable();
    }
    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.last_recorded_see_diagnostics_for_details_21509aab", "Last recorded - see Diagnostics for details"));
    EndNrCard();
    if (grid) ImGui::EndTable();
}


#include "NrDiagnosticsRuntime.h"
#include "NrDiagnosticsConnections.h"

static ImVec4 OutputActivityColor(const RenderingOutput::Snapshot& output,uint64_t now) {
    const auto muted=ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    if(!RenderingOutput::Fresh(output,now)||output.phase==RenderingOutput::Phase::Off)return muted;
    const auto background=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const bool light=background.x+background.y+background.z>1.5f;
    if(RenderingOutput::Active(output,now))return light?ImVec4(.08f,.48f,.25f,1):ImVec4(.45f,.86f,.61f,1);
    if(output.phase==RenderingOutput::Phase::Blocked)return light?ImVec4(.72f,.12f,.12f,1):ImVec4(.95f,.30f,.28f,1);
    return light?ImVec4(.57f,.36f,.05f,1):ImVec4(.99f,.77f,.37f,1);
}

static void RenderDiagnostics(Config* config,const std::optional<MenuStatus::RuntimeStatus>& status) {
    RenderPreflight(config,PreflightView::Signals);
    ImGui::Spacing();ImGui::Separator();
    const auto output=RenderingOutput::Query();const auto now=GetTickCount64();
    const bool blocked=RenderingOutput::Fresh(output,now)&&output.phase==RenderingOutput::Phase::Blocked;
    const auto muted=ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const auto background=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const bool light=background.x+background.y+background.z>1.5f;
    const ImVec4 bad=light?ImVec4(.72f,.12f,.12f,1):ImVec4(.95f,.30f,.28f,1);
    const ImVec4 warning=light?ImVec4(.57f,.36f,.05f,1):ImVec4(.99f,.77f,.37f,1);
    ImGui::PushStyleColor(ImGuiCol_Text,OutputActivityColor(output,now));
    NrDiagnosticsUi::Cell("##NrDiagnosticActivity",RenderingOutput::StatusLabel(output,now),1);
    ImGui::PopStyleColor();
    const auto native=SelectedNativeTelemetry();const auto present=DlssNr::PresentTelemetry();
    const auto route=config->DlssNrRoute.value_or_default();
    const auto vkFailure=DlssNr::FailureReasonVk();
    auto reason=RenderingOutput::ReasonLabel(output.reason);
    if(route==0&&native.failed&&native.failureReason&&native.failureReason[0])reason=native.failureReason;
    else if(route==0&&IsVulkanInput()&&vkFailure&&vkFailure[0])reason=vkFailure;
    else if((route==1||route==2)&&present.failed&&!present.failure.empty())reason=present.failure;
    else if(reason.empty()&&(route==1||route==2))reason=present.fallbackReason;
    if(reason.empty())reason=Neurotic::UiMessage("ingame.nr-diagnostics.no_failure","No renderer failure reported.");
    ImGui::PushStyleColor(ImGuiCol_Text,blocked?bad:muted);
    NrDiagnosticsUi::Cell("##NrDiagnosticNotice",reason,2);ImGui::PopStyleColor();
    const auto connection=PreparedGuides::QueryStatusV2(now);
    const bool restart=connection.available&&connection.status.restartRequired;
    ImGui::PushStyleColor(ImGuiCol_Text,restart?warning:muted);
    NrDiagnosticsUi::Cell("##NrDiagnosticRestart",restart?Neurotic::UiLiteral("ingame.dlssnr-menu.restart_required_before_another_connection_can_b_32b78efd","Restart required before another connection can begin."):Neurotic::UiLiteral("ingame.nr-diagnostics.last_recorded","Last-recorded owner values · session totals"),2);
    ImGui::PopStyleColor();
    static int selected=0;const auto page=NrDiagnosticsUi::Tabs(selected);
    if(page==NrDiagnosticsUi::Page::Inputs)RenderPreflight(config,PreflightView::Details);
    else {
        if(page==NrDiagnosticsUi::Page::Connections)RenderDiagnosticConnections();
        RenderDiagnosticRuntime(config,page,status);
    }
    ImGui::Separator();RenderPreflight(config,PreflightView::Footer);
    std::string details=Neurotic::Translate(reason);
    const char* notes[]={
        Neurotic::UiLiteral("ingame.dlssnr-menu.read_only_owner_reports_paused_or_old_frames_may_60998cc5","Read-only owner reports. Paused or old frames may leave last-recorded values visible."),
        Neurotic::UiLiteral("ingame.dlssnr-menu.canonical_input_readiness_unknown_complete_curre_6952a20a","Canonical input readiness: Unknown - complete current input qualification and use receipts are not published to this panel."),
        Neurotic::UiLiteral("ingame.dlssnr-menu.image_quality_not_measured_from_nr_output_057a8f52","Image quality: Not measured from NR output"),
        Neurotic::UiLiteral("ingame.dlssnr-menu.cpu_call_timing_and_fence_completion_do_not_meas_b97c1b01","CPU call timing and fence completion do not measure scanout or prove displayed frames."),
        Neurotic::UiLiteral("ingame.dlssnr-menu.copyback_does_not_verify_display_camera_and_jitt_ed7d4da7","Copyback does not verify display. Camera and jitter are not native observations."),
        Neurotic::UiLiteral("ingame.dlssnr-menu.model_sliders_request_settings_on_release_compos_a8b7b941","Model sliders request settings on release; composition controls remain live."),
        Neurotic::UiLiteral("ingame.nr-diagnostics.counter_limits","Evaluation, submission and completion totals are separate. Session totals do not prove current-frame use."),
        Neurotic::UiLiteral("ingame.nr-diagnostics.retained_model_help","Turning NR off retains any loaded model for the next enable request."),
        Neurotic::UiLiteral("ingame.nr-diagnostics.hidden_model_help","Turning Apply model off hides the edit. Model evaluation still runs when NR is running.")
    };
    for(const auto* note:notes){details+="\n\n";details+=Neurotic::Translate(note);}
    NrDiagnosticsUi::InfoDialog(details);
}

void RenderMenu(Config* config, float menuResScale, const std::optional<MenuStatus::RuntimeStatus>& status,
                const char* gpuName, MenuPage page)
{
    Advisor().gpuName = gpuName != nullptr && gpuName[0] != 0 ? gpuName : Neurotic::UiLiteral("ingame.dlssnr-menu.detecting_graphics_card_5066a0c7", "Detecting graphics card...");
    CancelAdvisorAnalysis(config, Neurotic::UiLiteral("ingame.dlssnr-menu.advisor_archived_original_settings_restored_1867d682", "Advisor archived; original settings restored."));
    if (page == MenuPage::Preflight) { RenderIntakeMenu(config, gpuName, status); return; }
    if (page == MenuPage::Diagnostics) { RenderDiagnostics(config,status); return; }
    if(config->DlssNrRoute.value_or_default()==3) {
        const auto output=RenderingOutput::Query();const auto now=GetTickCount64();
        StatusPanel::Line("##NrActivity",RenderingOutput::StatusLabel(output,now),
            OutputActivityColor(output,now));
        const auto reason=RenderingOutput::ReasonLabel(output.reason);
        StatusPanel::Line("##NrNotice",reason,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        BeginNrCard("##NrRenderingCard",Neurotic::UiLiteral("ingame.provider.e2fe353986e6", "Rendering"));
        AnythingModeUi::Render(*config);
        EndNrCard();
        return;
    }
    if (page == MenuPage::Multipass) { RenderMultipassMenu(config, menuResScale); return; }
    // Neural Rendering page -----------------------------
    ImGui::Spacing();
    {
    bool enabled = config->DlssNrEnabled.value_or_default();
    bool basicOwnsMain = false;
    auto uiConfig = config->GetDlssNrConfigSnapshot();
    int route = std::clamp((int) uiConfig.DlssNrRoute.value_or_default(), 0, 2);
    int stage = StageUi::Stage(uiConfig);
    int renderMode = std::clamp(uiConfig.DlssNrRenderingMode.value_or_default(), 0, 1);
    bool presentRoute = route != 0;
    static const char* renderModeNames[] = { Neurotic::UiLiteral("ingame.provider.1b2c08a8733d", "Quality"), Neurotic::UiLiteral("ingame.provider.6c5d98ea2e5d", "Performance (Default)") };
    const auto& routeNames = StageUi::Methods;
        ImGui::PushTextWrapPos(0.0f);

        basicOwnsMain = BasicMultipass::Active(config->GetDlssNrConfigSnapshot());
        // RR preserves After placement; Match Native only resizes NR's private raster.
        const bool nativeAfterOnly = !IsVulkanInput() && SelectedNativeTelemetry().nativeRayReconstructionActive;
        static const char* nrStyleNames[] = { Neurotic::UiLiteral("ingame.option.ef6691545d2c", "Standard"), Neurotic::UiLiteral("ingame.option.d6acb6d51cfc", "Natural"), Neurotic::UiLiteral("ingame.option.912d0988b065", "Cinematic") };
        static const char* nrPresetNames[] = { Neurotic::UiLiteral("ingame.menu-common.default_92fe477b", "Default"), Neurotic::UiLiteral("ingame.provider.d1ced7405198", "Preset 1"), Neurotic::UiLiteral("ingame.provider.c475c1784510", "Preset 2"), Neurotic::UiLiteral("ingame.provider.e7aebbc38aae", "Preset 3") };
        int style = std::clamp((int) config->DlssNrStyle.value_or_default(), 0, 2);
        int preset = (int) config->DlssNrPreset.value_or_default();
        const auto renderStyle = [&]()
        {
            Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.provider.7e744141b40e", "Style"));
            if (ImGui::Combo("##NrStyle", &style, nrStyleNames, IM_ARRAYSIZE(nrStyleNames)))
                config->DlssNrStyle = (uint32_t) style;
        };
        const auto renderModelPreset = [&]()
        {
            Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.provider.7252e7ce0085", "Preset"));
            if (ImGui::Combo("##NrModelPreset", &preset, nrPresetNames, IM_ARRAYSIZE(nrPresetNames)))
                config->DlssNrPreset = (uint32_t) preset;
        };
        RenderLiveReadouts(config, config->GetDlssNrConfigSnapshot(), enabled, basicOwnsMain,
                           NrReadoutGroup::Activity, status);
        std::optional<Neurotic::Sleek::CardColumns> mainCards;
        mainCards.emplace("##NrMainCards");
        const auto renderResampling = [&]
        {
        if (auto resampling = ScopedCollapsingHeader(Neurotic::UiLiteral("ingame.dlssnr-menu.advanced_resampling_27371461", "Advanced resampling##NrResampling")); resampling.IsHeaderOpen())
        {
        ScopedIndent resamplingIndent {};
        ImGui::BeginDisabled(presentRoute);
        const int scalePercent = StageUi::DisplayPercent(StageUi::ResolutionScale(uiConfig));

        if (!presentRoute && scalePercent > 100)
            ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.supersampling_2fx_the_model_runs_above_native_th_9c103bc7", "Supersampling %.2fx: the model runs ABOVE native, then\n"
                                "is sampled back down. Experimental, and costly -- time grows with the area."),
                                scalePercent / 100.0f);


        // Meaningful only when the model runs BELOW the frame's size. At 100% -- and above, where
        // supersampling composites its down-legged answer at native -- the residual collapses to the
        // model's own picture and the two modes are identical, so the control says so by going grey.
        {
            const bool reduced = StageUi::ResolutionScale(uiConfig) < 0.999f;

            if (!reduced)
                ImGui::BeginDisabled();

            static const char* enlargeNames[] = { Neurotic::UiLiteral("ingame.option.ae25d6481fc1", "Classic"), Neurotic::UiLiteral("ingame.dlssnr-menu.matched_residual_fd28d530", "Matched residual") };
            int enlarge = config->DlssNrTransfer.value_or_default() == 1 ? 1 : 0;

            if (ImGui::Combo(Neurotic::UiLiteral("ingame.dlssnr-menu.enlargement_8f7cc83f", "Enlargement"), &enlarge, enlargeNames, IM_ARRAYSIZE(enlargeNames)))
                config->DlssNrTransfer = (uint32_t) enlarge;

            if (!reduced)
                ImGui::EndDisabled();

            HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.how_the_model_s_work_is_brought_back_up_when_it__62607e09", "How the model's work is brought back up when it ran below the frame's size."
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
                           "\n\nFrom hhkbble's multi-pass work on this fork."));
        }
        ImGui::EndDisabled();
        }
        };
        const auto renderProxy = [&]
        {
        // Experimental. 0 off (soft knee), 1 Neutwo + our composition, 2 Neutwo + pure-inverse replace,
        // 3 hybrid+composed, 4 hybrid+replace (identity midtones + unclipped highlights). Always shown.
        static const char* reversibleNames[] = { Neurotic::UiLiteral("ingame.dlssnr-menu.off_soft_knee_d8e3178a", "Off (soft knee)"), Neurotic::UiLiteral("ingame.dlssnr-menu.neutwo_proxy_composed_66d8946f", "Neutwo proxy + composed"),
                                                 Neurotic::UiLiteral("ingame.dlssnr-menu.neutwo_proxy_replace_7ad891b1", "Neutwo proxy + replace"), Neurotic::UiLiteral("ingame.dlssnr-menu.hybrid_proxy_composed_6ca6d792", "Hybrid proxy + composed"),
                                                 Neurotic::UiLiteral("ingame.dlssnr-menu.hybrid_proxy_replace_c2a71834", "Hybrid proxy + replace") };
        int reversible = (int) config->DlssNrReversibleMode.value_or_default();
        if (reversible < 0 || reversible > 4)
            reversible = 0;
        if (ImGui::Combo(Neurotic::UiLiteral("ingame.dlssnr-menu.reversible_proxy_68eaf8b3", "Reversible proxy"), &reversible, reversibleNames,
                         IM_ARRAYSIZE(reversibleNames)))
            config->DlssNrReversibleMode = (uint32_t) reversible;

        HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.what_the_model_is_shown_and_how_its_answer_comes_1ad8f03f", "What the model is shown, and how its answer comes back."
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
                       "\n\nOff is byte-identical to before."));

        };

        BeginNrCard("##NrRenderingCard", Neurotic::UiLiteral("ingame.provider.e2fe353986e6", "Rendering"));
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.enable_neural_rendering_38e3f189", "Enable Neural Rendering"), &enabled)) config->SetDlssNrEnabled(enabled);
        bool applyModel = config->DlssNrApplyModel.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.apply_the_model_8fad708a", "Apply the model"), &applyModel)) config->DlssNrApplyModel = applyModel;
        const auto telemetry = SelectedNativeTelemetry();
        if (enabled && !status && !(basicOwnsMain && BasicMultipass::Count(config->DlssNrBasicMultipass.value_or_default()) == 0) &&
            !IsVulkanInput() && !DlssNr::IsRunningVk() && config->DlssNrRoute.value_or_default() == 0 &&
            telemetry.failureReason[0] && telemetry.retryAllowed && !DlssNr::FailureReasonVk()[0] &&
            ImGui::SmallButton(Neurotic::UiLiteral("ingame.provider.942087cc2d41", "Retry"))) DlssNr::RetryAfterFailure();
        Neurotic::Sleek::ControlDivider();
            if (StageUi::RenderControls(*config, basicOwnsMain, nativeAfterOnly, false, true)) CancelNrEdits();

            uiConfig = config->GetDlssNrConfigSnapshot();
            if (basicOwnsMain) BasicMultipass::Derive(uiConfig);
            stage = StageUi::Stage(uiConfig);
            ImGui::BeginDisabled(basicOwnsMain || StageUi::NativePlacementRefusal(uiConfig, nativeAfterOnly) != nullptr);
            if (auto reveal = Neurotic::Sleek::AnimatedRegion("##ManualResolution", StageUi::ResolutionSelection(uiConfig) == 1); reveal.Visible())
            {
                static NrOptional<float> scalePreview { 1.0f };
                static uint64_t previousSelection = 0;
                const auto selection = PresentResolution::CaptureKey(uiConfig) * 4 + stage;
                if (selection != previousSelection) CancelNrEdits();
                previousSelection = selection;
                const auto editGeneration = NrConfigSynchronization::ProfileGeneration();
                const float originalScale = StageUi::ResolutionScale(uiConfig);
                scalePreview = originalScale;
                ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.dlssnr-menu.manual_resolution_c3ca8218", "Manual resolution"));
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.sets_the_neural_rendering_working_resolution_as__2dbd6710", "Sets the Neural Rendering working resolution as a percentage of the selected "
                           "stage: the game's render input Before upscaling, or the final upscaled output "
                           "After. Lower values reduce model cost and fine detail. Values above 100% "
                           "supersample, increase cost roughly with image area, and reveal the downscaler. "
                           "Reset restores 100%."));
                const float resetWidth = ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                const float sliderRoom = ImGui::GetContentRegionAvail().x - resetWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f;
                ImGui::SetNextItemWidth((std::min)(220.0f * menuResScale,
                    (std::max)(65.0f * menuResScale, sliderRoom)));
                if (DeferredNrSlider("##NrManualScale", { &scalePreview }, 0.25f, 2.0f, 1.0f, "%d%%", true))
                {
                    NrConfigSynchronization::Transaction transaction;
                    const auto current = config->GetDlssNrConfigSnapshot();
                    if (editGeneration == NrConfigSynchronization::ProfileGeneration() &&
                        selection == PresentResolution::CaptureKey(current) * 4 + StageUi::Stage(current) &&
                        originalScale == StageUi::ResolutionScale(current))
                    {
                        const auto inputPolicy = config->DlssNrPresentInputPolicy;
                        StageUi::SelectUnifiedResolutionScale(*config, scalePreview.value_or_default());
                        config->DlssNrPresentInputPolicy = inputPolicy;
                    }
                    else CancelNrEdits();
                }
                uiConfig = config->GetDlssNrConfigSnapshot();
                if (basicOwnsMain) BasicMultipass::Derive(uiConfig);
            }
            ImGui::EndDisabled();
        // The ordinary downscaler sits directly below manual resolution.
        if (StageUi::ResolutionScale(uiConfig) > 1.0f)
        {
            ImGui::BeginDisabled(basicOwnsMain);
            static const char* names[] = { Neurotic::UiLiteral("ingame.option.b3290cbc23e0", "FSR1"), Neurotic::UiLiteral("ingame.option.d64f696cc3e4", "Bicubic"), Neurotic::UiLiteral("ingame.option.e817551fdce4", "Catmull-Rom"), Neurotic::UiLiteral("ingame.option.4143056699ee", "Lanczos2"), Neurotic::UiLiteral("ingame.option.9feb40edb86b", "Lanczos3"), Neurotic::UiLiteral("ingame.option.6de2fa378185", "Kaiser2"), Neurotic::UiLiteral("ingame.option.592782aa7dcb", "Kaiser3"), Neurotic::UiLiteral("ingame.option.e7b0c0160426", "MAGIC") };
            int downscaler = std::clamp(int(uiConfig.DlssNrScalingDownscaler.value_or_default()), 0, 7);
            if (ImGui::Combo(Neurotic::UiLiteral("ingame.dlssnr-menu.downscaler_b3373b8a", "Downscaler##NrDownscaler"), &downscaler, names, 8))
                config->DlssNrScalingDownscaler = (Scaler) downscaler;
            ImGui::EndDisabled();
        }
            ImGui::Spacing();
            Neurotic::Sleek::ControlDivider();
            ImGui::Spacing();
            renderResampling();
            renderStyle();
            renderModelPreset();
            RenderLiveReadouts(config, config->GetDlssNrConfigSnapshot(), enabled, basicOwnsMain,
                               NrReadoutGroup::Output, status);
            if (const auto* placementRefusal = StageUi::NativePlacementRefusal(uiConfig, nativeAfterOnly))
                ImGui::TextWrapped("%s",Neurotic::Translate(placementRefusal).c_str());
            else if (uiConfig.DlssNrRoute.value_or_default() == 0 &&
                     StageUi::Stage(uiConfig) == 0 && StageUi::Manual(uiConfig))
                ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.legacy_before_upscaling_manual_placement_is_pres_2163c958", "Legacy before-upscaling Manual placement is preserved until you choose a resolution."));
            if (basicOwnsMain) ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.controlled_by_basic_multi_pass_9185ace2", "Controlled by Basic Multi Pass"));
        EndNrCard();
        mainCards->Next();
        uiConfig = config->GetDlssNrConfigSnapshot();
        if (basicOwnsMain) BasicMultipass::Derive(uiConfig);
        route = std::clamp((int) uiConfig.DlssNrRoute.value_or_default(), 0, 2);
        stage = StageUi::Stage(uiConfig);
        renderMode = std::clamp(uiConfig.DlssNrRenderingMode.value_or_default(), 0, 1);
        presentRoute = route != 0;

        BeginNrCard("##NrAppearanceCard", Neurotic::UiLiteral("ingame.provider.8045d3f875fd", "Model & composition"));
        ImGui::BeginDisabled(basicOwnsMain);
        if (basicOwnsMain)
        {
            Neurotic::Sleek::ControlLabel(Neurotic::UiLiteral("ingame.dlssnr-menu.model_strength_a6a09f10", "Model Strength"));
            float strength = uiConfig.DlssNrIntensity.value_or_default();
            const float resetWidth = ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")).x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - resetWidth - ImGui::GetStyle().ItemSpacing.x));
            Neurotic::Sleek::CardSliderFloat("##NrMainModelStrength", &strength, 0.0f, 1.0f, "%.2f");
            ImGui::SameLine(); ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##Model Strength"));
        }
        else DeferredSlider(Neurotic::UiLiteral("ingame.dlssnr-menu.model_strength_a6a09f10", "Model Strength"), &config->DlssNrIntensity, 0.0f, 2.0f, 1.0f,
                            "%.2f", Neurotic::UiLiteral("ingame.dlssnr-menu.model_strength_a6a09f10", "Model Strength"));
        Neurotic::Sleek::ControlDivider();
        const auto liveStrength = [&](const char* label, const char* id, const char* resetId,
                                      auto& option, float maximum)
        {
            Neurotic::Sleek::ControlLabel(label);
            const float resetWidth = ImGui::CalcTextSize(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset")).x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetNextItemWidth((std::max)(1.0f, ImGui::GetContentRegionAvail().x - resetWidth - ImGui::GetStyle().ItemSpacing.x));
            float value = basicOwnsMain && &option == &config->DlssNrTransferStrength ?
                uiConfig.DlssNrTransferStrength.value_or_default() : option.value_or_default();
            if (Neurotic::Sleek::CardSliderFloat(id, &value, 0.0f, maximum, "%.2f")) option = value;
            ImGui::SameLine();
            if (ImGui::SmallButton(resetId)) option = 1.0f;
            Neurotic::Sleek::ControlDivider();
        };
        liveStrength(Neurotic::UiLiteral("ingame.dlssnr-menu.detail_strength_d781fe36", "Detail Strength"), "###Detail strength", Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##detail"), config->DlssNrTransferStrength, 2.0f);
        ImGui::EndDisabled();
        liveStrength(Neurotic::UiLiteral("ingame.dlssnr-menu.colour_strength_0db6aec0", "Colour Strength"), "###Colour strength", Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##colour"), config->DlssNrColourStrength, 4.0f);
        DeferredSlider(Neurotic::UiLiteral("ingame.dlssnr-menu.local_structure_5d8bd8d5", "Local Structure###Local structure"), &config->DlssNrLocalStructure,
                       0.0f, 2.0f, 1.0f, "%.2f", Neurotic::UiLiteral("ingame.dlssnr-menu.local_structure_5d8bd8d5", "Local Structure"));
        Neurotic::Sleek::ControlDivider();
        DeferredSlider(Neurotic::UiLiteral("ingame.dlssnr-menu.local_tone_a02133bf", "Local Tone###Local tone"), &config->DlssNrLocalTone,
                       0.0f, 2.0f, 1.0f, "%.2f", Neurotic::UiLiteral("ingame.dlssnr-menu.local_tone_a02133bf", "Local Tone"));
        Neurotic::Sleek::ControlDivider();
        DeferredSlider(Neurotic::UiLiteral("ingame.dlssnr-menu.skin_structure_349e40a3", "Skin Structure###Skin structure"), &config->DlssNrSkinStructure,
                       -1.0f, 2.0f, -1.0f, "%.2f", Neurotic::UiLiteral("ingame.dlssnr-menu.skin_structure_349e40a3", "Skin Structure"));
        Neurotic::Sleek::ControlDivider();
        bool autoMask = config->DlssNrAutoMask.value_or_default();
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.auto_skin_mask_30c7534c", "Auto skin mask"), &autoMask)) config->DlssNrAutoMask = autoMask;
        Neurotic::Sleek::ControlDivider();
        renderProxy();
        EndNrCard();
        mainCards.reset();
        ImGui::Spacing();
        ImGui::PushItemWidth(std::clamp(ImGui::GetContentRegionAvail().x - 240.0f * menuResScale,
                                      40.0f * menuResScale, 220.0f * menuResScale));
        {
        {

        BeginNrCard(Neurotic::UiLiteral("ingame.menu-common.color_af6f8b8d", "Color##DlssNrColourSection"), Neurotic::UiLiteral("ingame.menu-common.color_af6f8b8d", "Color"));
        {
        ImGui::Spacing();
        ScopedNestedTextWrap nestedWrap {};



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
            const bool vk = UsesVulkanNrRoute(*config);
            if(vk) DlssNr::ExposureScan::LoadAnchors(config->DlssNrScanAnchors.value_or_default());
            const bool haveExposure = vk ? DlssNr::ExposureOfferedVk() : ex.everOffered;

            const float anchorNow = (IsVulkanInput() ? DlssNr::BestExposureScanVk() : DlssNr::ExposureScan::BestValue());
            const bool haveAnchor = !DlssNr::ExposureScan::Anchors().empty();

            const char* sourceNames[] = { Neurotic::UiLiteral("ingame.dlssnr-menu.paper_white_only_5d3268f6", "Paper white only"), Neurotic::UiLiteral("ingame.dlssnr-menu.the_game_s_own_exposure_6e5f266a", "The game's own exposure"),
                                                 vk ? Neurotic::UiLiteral("ingame.dlssnr-menu.completed_vulkan_exposure_color_meter_034d9ee8", "Completed Vulkan exposure/color meter") : Neurotic::UiLiteral("ingame.dlssnr-menu.a_buffer_the_scan_found_7bb04c21", "A buffer the scan found") };

            if (presentRoute) ImGui::TextWrapped(vk ?
                Neurotic::UiLiteral("ingame.dlssnr-menu.vulkan_present_uses_its_validated_color_recipe_n_4bc23c75", "Vulkan Present uses its validated color recipe; Native exposure and paper-white settings are retained inactive.") :
                State::Instance().isHdrActive ? Neurotic::UiLiteral("ingame.dlssnr-menu.hdr_present_uses_a_fixed_white_point_of_2_5_nati_e18bb616", "HDR Present uses a fixed white point of 2.5; Native exposure settings are retained inactive.") :
                Neurotic::UiLiteral("ingame.dlssnr-menu.sdr_present_uses_manual_paper_white_native_expos_364c6c16", "SDR Present uses manual paper white; Native exposure and scan settings are retained inactive."));
            int source = (int) config->DlssNrWhitePointSource.value_or_default();

            if (source < 0 || source > 2)
                source = 0;

            ImGui::BeginDisabled(presentRoute);
            if (ImGui::Combo(Neurotic::UiLiteral("ingame.dlssnr-menu.white_point_from_10cbdfb6", "White point from"), &source, sourceNames, IM_ARRAYSIZE(sourceNames)))
            {
                config->DlssNrWhitePointSource = (uint32_t) source;

                // Nothing else to set. The scan asks the source whether it is wanted, so choosing
                // it here is the whole of switching it on -- there is no second flag to keep in
                // step, and so no way for the two to disagree.
            }

            ImGui::EndDisabled();
            HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.where_the_number_that_divides_the_frame_comes_fr_843c73a1", "Where the number that divides the frame comes from."
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
                           "\nafterwards."));

            if (!presentRoute) {
            // Availability, in colour, for the option currently chosen.
            if (source == 1)
            {
                if (!vk && ex.seenFrames == 0)
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.waiting_for_a_frame_dd27e6c4", "Waiting for a frame..."));
                else if (!haveExposure)
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                       Neurotic::UiLiteral("ingame.dlssnr-menu.this_game_supplies_no_exposure_paper_white_is_in_f21bc91d", "This game supplies no exposure -- paper white is in use. Try "
                                       "the scan instead."));
                else if (vk)
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.an_exposure_texture_is_supplied_native_color_pro_6527dfec", "An exposure texture is supplied. Native color processing uses it only after a qualified readback completes."));
                else if (ex.exposure > 1e-6f)
                {
                    const float trim =
                        std::clamp(config->DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       Neurotic::UiLiteral("ingame.dlssnr-menu.game_exposure_4f_white_point_2f_s_099684d1", "Game exposure %.4f  ->  white point %.2f%s"), ex.exposure,
                                       ex.preExposure / ex.exposure * trim,Neurotic::Translate(ex.offeredNow ? "" : Neurotic::UiLiteral("ingame.dlssnr-menu.held_absent_this_frame_5b680ffc", "  (held: absent this frame)")).c_str());
                }
                else
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.reading_the_exposure_6c6f534f", "Reading the exposure..."));
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
                                           Neurotic::UiLiteral("ingame.dlssnr-menu.nothing_in_this_game_is_shaped_like_an_exposure_3f32f1fe", "Nothing in this game is shaped like an exposure."));
                    else
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                           Neurotic::UiLiteral("ingame.dlssnr-menu.watching_u_none_moving_yet_go_between_light_and__a70690ea", "Watching %u, none moving yet -- go between light and shade."),
                                           watching);
                }
                else if (!haveAnchor)
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                       Neurotic::UiLiteral("ingame.dlssnr-menu.found_one_set_paper_white_below_until_the_pictur_bbcfa860", "Found one. Set paper white below until the picture looks "
                                       "right, then press Anchor here."));
                // Once anchored, the scan -> white point readout sits above the sliders below; it is
                // not repeated up here.
            }
            else if (haveExposure)
            {
                ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                   Neurotic::UiLiteral("ingame.dlssnr-menu.this_game_supplies_an_exposure_the_option_above__670fc5b1", "This game supplies an exposure -- the option above would use it."));
            }
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
        ImGui::BeginDisabled(presentRoute && (UsesVulkanNrRoute(*config) || State::Instance().isHdrActive));
        const int wpSource = presentRoute ? 0 : (int) config->DlssNrWhitePointSource.value_or_default();

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
                const float liveScan = (IsVulkanInput() ? DlssNr::BestExposureScanVk() : DlssNr::ExposureScan::BestValue());

                if (liveScan > 0.0f)
                {
                    const float w = DlssNr::ExposureScan::AnchoredWhitePoint(
                        liveScan, config->DlssNrScanInverted.value_or_default(),
                        config->DlssNrScanTrim.value_or_default());

                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       Neurotic::UiLiteral("ingame.dlssnr-menu.scan_5f_white_point_2f_u_point_s_a21e7c3b", "Scan %.5f  ->  white point %.2f   (%u point%s)"), liveScan, w,
                                       (unsigned) anchors.size(),Neurotic::Translate(anchors.size() == 1 ? "" : "s").c_str());
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
                    snprintf(lbl, sizeof(lbl), Neurotic::UiLiteral("ingame.dlssnr-menu.paper_white_editing_point_d_1fe247de", "Paper white (editing point %d)"), selectedAnchor + 1);
                else
                    snprintf(lbl, sizeof(lbl), Neurotic::UiLiteral("ingame.dlssnr-menu.paper_white_f39ef2c8", "Paper white"));

                if (ImGui::SliderFloat(lbl, &pw, 0.25f, 2000.0f, Neurotic::UiLiteral("ingame.dlssnr-menu.2fx_093b9cc9", "%.2fx"), ImGuiSliderFlags_Logarithmic))
                {
                    if (editingRow)
                    {
                        DlssNr::ExposureScan::AnchorSetWhite(selectedAnchor, pw);
                        config->DlssNrScanAnchors = DlssNr::ExposureScan::SerializeAnchors();
                    }
                    else
                        config->DlssNrWhitePointScale = pw;
                }

                if (!editingRow) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##scanPaperWhite"))) config->DlssNrWhitePointScale = 1.0f;
                }
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.the_white_point_for_the_selected_calibration_poi_8d3b3156", "The white point for the selected calibration point, or -- with no row"
                               "\nselected -- the value the next Anchor press captures."
                               "\n\nSet it until the picture looks right here, then Anchor. Move to very"
                               "\ndifferent light and do it again: two points fix the buffer's real"
                               "\nrelationship and the white point holds between them. Click a row below"
                               "\nto come back and adjust that point; click it again to let go."));
            }

            // The trim multiplies the interpolated result, and in the steady state it is the control
            // that stands in for paper white: adjust it until the picture looks right in the current
            // light, then Anchor bakes that trimmed value into a new point and resets the trim to 1.
            if (!anchors.empty())
            {
                float trim = config->DlssNrScanTrim.value_or_default();

                if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.dlssnr-menu.trim_x_the_scan_583d14bf", "Trim (x the scan)"), &trim, 0.25f, 4.0f, Neurotic::UiLiteral("ingame.dlssnr-menu.2fx_093b9cc9", "%.2fx"),
                                       ImGuiSliderFlags_Logarithmic))
                    config->DlssNrScanTrim = std::clamp(trim, 0.25f, 4.0f);

                ImGui::SameLine();

                if (ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##scantrim")))
                    config->DlssNrScanTrim = 1.0f;

                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.a_multiplier_on_the_scan_s_white_point_and_the_c_108e121c", "A multiplier on the scan's white point, and the control you adjust between"
                               "\nanchor points: dial it until the picture looks right in the current"
                               "\nlight, then press Anchor here -- it captures the trimmed value as a new"
                               "\npoint and resets the trim to 1."));
            }
        }
        else if (wpSource == 1)
        {
            const bool ofScan = false;

            float trim = ofScan ? config->DlssNrScanTrim.value_or_default()
                                : config->DlssNrWhitePointTrim.value_or_default();

            if (ImGui::SliderFloat(ofScan ? Neurotic::UiLiteral("ingame.dlssnr-menu.trim_x_the_scan_583d14bf", "Trim (x the scan)") : Neurotic::UiLiteral("ingame.dlssnr-menu.trim_x_the_game_s_exposure_e2e08a30", "Trim (x the game's exposure)"), &trim,
                                   0.25f, 4.0f, Neurotic::UiLiteral("ingame.dlssnr-menu.2fx_093b9cc9", "%.2fx"), ImGuiSliderFlags_Logarithmic))
            {
                if (ofScan)
                    config->DlssNrScanTrim = std::clamp(trim, 0.25f, 4.0f);
                else
                    config->DlssNrWhitePointTrim = std::clamp(trim, 0.25f, 4.0f);
            }

            ImGui::SameLine();

            // Deliberately always present rather than greyed at 1. The point of it is that the safe
            // value is one click away without having to know what the safe value is.
            if (ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##wptrim")))
            {
                if (ofScan)
                    config->DlssNrScanTrim = 1.0f;
                else
                    config->DlssNrWhitePointTrim = 1.0f;
            }

            HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.a_multiplier_on_the_exposure_the_game_supplied_1_c2453057", "A multiplier on the exposure the game supplied. 1.00x takes its number"
                           "\nexactly, and that is the right answer here."
                           "\n\nThis is not a fudge factor. If a game needs the trim far from 1 to look"
                           "\nright, that is evidence the exposure being read is wrong for that game,"
                           "\nnot that the game wants trimming. Somewhere around 0.8 to 1.25 is honest"
                           "\ntuning; reaching for 4 means something upstream is broken and the trim is"
                           "\nhiding it."
                           "\n\nYour manual paper white is kept separately and comes back untouched if"
                           "\nyou switch the option above off."));
        }
        else
        {
            // Logarithmic, because the useful range is not linear. A quarter to 2000: the low end
            // because a frame the game already tone mapped wants roughly 1, the high end because
            // there is no principled ceiling -- this is a divisor on an open-ended linear buffer, and
            // how far up a given game needs to go is a property of that game's exposure rather than
            // of anything that can be bounded here. One tester was still improving at 100.
            float wpScale = config->DlssNrWhitePointScale.value_or_default();

            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.dlssnr-menu.paper_white_f39ef2c8", "Paper white"), &wpScale, 0.25f, 2000.0f, Neurotic::UiLiteral("ingame.dlssnr-menu.2fx_093b9cc9", "%.2fx"),
                                   ImGuiSliderFlags_Logarithmic))
                config->DlssNrWhitePointScale = wpScale;
            ImGui::SameLine();
            if (ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##paperWhite"))) config->DlssNrWhitePointScale = 1.0f;

        HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.what_the_frame_is_divided_by_before_the_model_se_8020353d", "What the frame is divided by before the model sees it. There is no other white"
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
                       "\n\nAt strength zero the frame is still bit-identical whatever this says."));
        }

        ImGui::EndDisabled();

        // Highlight guard, directly under the white point / trim -- it bounds the model's edit and
        // belongs with the exposure controls it works alongside.
        float maxRatio = config->DlssNrMaxRatio.value_or_default();
        if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.dlssnr-menu.highlight_guard_4dfd1908", "Highlight guard"), &maxRatio, 1.0f, 8.0f, Neurotic::UiLiteral("ingame.dlssnr-menu.1fx_02b952fe", "%.1fx")))
            config->DlssNrMaxRatio = maxRatio;

        ImGui::SameLine();
        if (ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##guard")))
            config->DlssNrMaxRatio = 2.0f;

        HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.the_most_the_pass_may_move_any_pixel_as_a_multip_dc109d72", "The most the pass may move any pixel, as a multiple of what it already was, in"
                       "\nboth directions -- a pixel may not be brightened past this nor darkened past"
                       "\nits reciprocal. Lights are where the model has least to say and rescaling its"
                       "\nanswer does the most damage; 2x leaves detail intact while stopping a strip"
                       "\nlight turning into a string of coloured cells. Raise it only if bright areas"
                       "\nlook clipped."));

        ImGui::BeginDisabled(presentRoute);
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
                    ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.show_the_light_meter_on_screen_11dd142d", "Show the light meter on screen"), &meter))
                    config->DlssNrScanMeter = meter;

                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.a_lamp_in_the_corner_red_for_dark_green_for_full_b711d545", "A lamp in the corner: red for dark, green for full light, and the"
                               "\nshades between, with the reading beside it."
                               "\n\nIt is how you see at a glance that the scan is TRACKING rather"
                               "\nthan merely running. Walk into shade and it should slide toward"
                               "\nred; step out and it should go green. If it moves the wrong way,"
                               "\nthat is what the setting above is for."
                               "\n\nPurely a readout. It changes nothing."));

            // Shown when the scan is actually running, whichever way it got switched on.
            if (IsVulkanInput() ? (config->DlssNrWhitePointSource.value_or_default()==2 || config->DlssNrScanMeter.value_or_default()) : DlssNr::ExposureScan::Scanning())
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
                const float live = (IsVulkanInput() ? DlssNr::BestExposureScanVk(&which, &low, &high) : DlssNr::ExposureScan::BestValue(&which, &low, &high));

                const bool isSource = config->DlssNrWhitePointSource.value_or_default() == 2;

                // Anchor captures (currentScan, currentPaperWhite) and ADDS a row -- it does not
                // replace. One row is the old single-anchor ratio law; add a second in different
                // light and the white point is interpolated between the points, so it holds across
                // the whole range instead of only near one anchor. Greyed unless the scan is the
                // chosen source and it currently has a value to capture.
                ImGui::BeginDisabled(live <= 0.0f || !isSource);

                if (ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.anchor_here_ca00d5c0", "Anchor here")))
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

                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.make_the_picture_look_right_then_press_this_it_c_12bae595", "Make the picture look right, then press this -- it captures the current look"
                               "\nas a point. For the first point use the Paper white slider above; for"
                               "\nevery point after, move to different light and use the Trim, which the"
                               "\nAnchor then bakes into a new point."
                               "\n\nThe first press calibrates one point -- the white point then"
                               "\nfollows the scan by ratio from there, as before. Walk into very"
                               "\ndifferent light, set paper white again, and press it again: the"
                               "\nsecond point pins down the buffer's real curve and everything"
                               "\nbetween the two is right, not just near one anchor. Up to eight."
                               "\n\nThe table is per game and shareable: one person calibrates a game"
                               "\nand the numbers are the same for everyone who takes the profile."));

                if (!isSource)
                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.the_scan_is_only_watching_the_white_point_above__32fa3a87", "(the scan is only watching -- the white point above comes "
                                        "from somewhere else)"));

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

                    ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.click_a_row_to_edit_it_with_the_slider_above_cli_fae97cb6", "Click a row to edit it with the slider above; click it again"
                                        " to control the live point. > is the point in use now."));
                }

                // The direction flag only means anything with a single point; with two or more the
                // direction the white point moves is already fixed by the data.
                if (anchors.size() == 1)
                {
                    bool inverted = config->DlssNrScanInverted.value_or_default();
                    if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.the_number_runs_the_other_way_f51dcad9", "The number runs the other way"), &inverted))
                        config->DlssNrScanInverted = inverted;

                    HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.flip_this_if_the_picture_gets_worse_in_the_direc_d3d2949c", "Flip this if the picture gets worse in the direction it should be"
                                   "\ngetting better. Most engines store an exposure that falls as"
                                   "\nthe scene brightens; some store its reciprocal, and a buffer"
                                   "\nfound by shape does not say which. Add a second anchor point in"
                                   "\ndifferent light and this is decided for you, so it disappears."));
                }

                // The scan -> white point readout is shown above the sliders now, not here.

                // Everything below is read-out rather than control: what the scan is looking at and
                // how to tell whether it found the right thing. Folded away because the two decisions
                // that matter -- anchor, and which way the number runs -- are above it.
                if (auto tree = Neurotic::Sleek::ScopedTreeNode(Neurotic::UiLiteral("ingame.objectruleeditor.advanced_cfbc9ae1", "Advanced")); tree.IsOpen())
                {

                    const auto found = DlssNr::ExposureScan::Report();
                    const char* why = IsVulkanInput() ? Neurotic::UiLiteral("ingame.dlssnr-menu.vulkan_reads_the_qualified_sr_exposure_or_owned__09a4150c", "Vulkan reads the qualified SR exposure or owned color meter after actual GPU completion and recording release. Foreign exposure discovery is unavailable without source ownership.") : DlssNr::ExposureScan::Status();

                    if (found.empty())
                    {
                        ImGui::TextDisabled("%s",Neurotic::Translate(why != nullptr && why[0] != 0
                                                      ? why
                                                      : Neurotic::UiLiteral("ingame.dlssnr-menu.nothing_matched_yet_11f58ac5", "nothing matched yet.")).c_str());
                    }
                    else
                    {
                        for (size_t i = 0; i < found.size(); ++i)
                        {
                            const auto& c = found[i];

                            if (c.reads == 0)
                            {
                                ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.zu_s_not_read_yet_a9f15053", "%zu. %s -- not read yet"), i + 1,Neurotic::Translate(c.shape.c_str()).c_str());
                                continue;
                            }

                            // Moving is the whole signal, so it is the thing that is coloured.
                            ImGui::TextColored(c.moves ? ImVec4(0.45f, 0.8f, 0.45f, 1.0f)
                                                       : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                               Neurotic::UiLiteral("ingame.provider.ae643386d3ad", "%zu. %s = %.5f  (seen %.5f..%.5f) %s"), i + 1,Neurotic::Translate(c.shape.c_str()).c_str(), c.latest, c.lowest, c.highest,Neurotic::Translate(c.moves ? Neurotic::UiLiteral("ingame.provider.1fa1f999236c", "MOVES") : Neurotic::UiLiteral("ingame.dlssnr-menu.flat_so_far_a38f1870", "flat so far")).c_str());
                        }

                        ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.walk_from_shade_into_daylight_a_real_exposure_mo_c4e0e2f9", "Walk from shade into daylight. A real exposure moves."));
                        ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.one_that_only_ever_climbs_is_a_counter_not_an_ex_0efafa49", "One that only ever climbs is a counter, not an exposure."));
                    }


                }
            }
        }


        ImGui::EndDisabled();
        }
        }


        }
        EndNrCard();
        }
        ImGui::PopItemWidth();
        ImGui::PopTextWrapPos();

    }
}

void RenderCompareMenu(Config* config, float menuResScale)
{
    ScopedNestedTextWrap wrap;
    ImGui::PushItemWidth((std::min)(220.0f * menuResScale, ImGui::GetContentRegionAvail().x));
    BeginNrCard("##NrCompareTools", Neurotic::UiLiteral("ingame.dlssnr-menu.nr_compare_ec5dd733", "NR Compare"));
    const bool dx12Present = !UsesVulkanNrRoute(*config) && config->DlssNrRoute.value_or_default() != 0;
    if (dx12Present) ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.hold_compare_and_debug_view_are_unavailable_on_t_70770fa8", "Hold, Compare and Debug view are unavailable on this Present route; saved settings are retained for Native."));
    ImGui::BeginDisabled(dx12Present);
        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader(Neurotic::UiLiteral("ingame.dlssnr-menu.compare_614fcd95", "Compare##DlssNrCompareSection")); ch.IsHeaderOpen())
        {
        ScopedIndent indent {};
        ImGui::Spacing();
        ScopedNestedTextWrap nestedWrap {};

        // Freeze the frame the model works on, so a setting change re-renders it in place -- the only
        // clean way to A/B our own settings (a moving scene confounds every other comparison). See
        // design/frame-hold.md.
        bool held = config->DlssNrHoldFrame.value_or_default();
        const bool unsupportedHold = UsesVulkanNrRoute(*config) && !DlssNr::CurrentVulkanNrCapabilities().hold.available;
        ImGui::BeginDisabled(unsupportedHold);
        if (unsupportedHold) held = false;
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.hold_frame_7b9c7233", "Hold frame"), &held))
            config->DlssNrHoldFrame = held;
        ImGui::EndDisabled();

        HelpMarker(unsupportedHold ? DlssNr::CurrentVulkanNrCapabilities().hold.reason.c_str()
                                  : Neurotic::UiLiteral("ingame.dlssnr-menu.freezes_the_frame_the_model_works_on_while_held__5ab409d5", "Freezes the frame the model works on. While held, change paper white, the"
                       "\nstrengths, the reversible mode, the model preset -- anything below the"
                       "\nupscaler -- and only that setting moves; the scene does not."
                       "\n\nWhat it CANNOT show: DLSS/FSR/XeSS upscaler presets or anything upstream"
                       "\n(the upscaler is not re-run on a held frame), and the game's own HUD and"
                       "\npost-processing, which run after this pass and keep updating. The white"
                       "\npoint stops being measured and holds its value while frozen, so it cannot"
                       "\ndrift and confound the comparison."
                       "\n\nHide the menu and it stays held. Untoggle to resume."));

        static const char* compareNames[] = { Neurotic::UiLiteral("ingame.objectruleeditor.off_dc516be5", "Off"), Neurotic::UiLiteral("ingame.dlssnr-menu.side_by_side_3dee4cf0", "Side by side"), Neurotic::UiLiteral("ingame.option.3404644e0117", "Wipe") };
        const bool unsupportedCompare = UsesVulkanNrRoute(*config) && !DlssNr::NativeRayReconstructionVk() && config->DlssNrRoute.value_or_default() == 0 &&
            StageUi::Stage(config->GetDlssNrConfigSnapshot()) == 0;
        if (unsupportedCompare) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.25f, 1.0f));
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.compare_is_unavailable_before_upscaling_on_vulka_38a7ff61", "Compare is unavailable before upscaling on Vulkan; saved settings are retained."));
            ImGui::PopStyleColor();
        }
        ImGui::BeginDisabled(unsupportedCompare);
        int compare = (int) config->DlssNrCompare.value_or_default();
        if (ImGui::Combo(Neurotic::UiLiteral("ingame.dlssnr-menu.compare_614fcd95", "Compare"), &compare, compareNames, IM_ARRAYSIZE(compareNames)))
            config->DlssNrCompare = (uint32_t) compare;

        HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.shows_the_pass_against_itself_so_the_two_can_be__822e02e3", "Shows the pass against itself, so the two can be seen at once rather than"
                       "\ntoggled and remembered."
                       "\n\nSide by side puts the whole frame in each half, untouched on the left and"
                       "\nedited on the right. Both halves are squeezed horizontally to fit, so it is"
                       "\nfor looking at rather than playing in."
                       "\n\nWipe cuts a single frame at the split and resamples nothing, so the picture"
                       "\nis the right shape and can be played normally. Drag the split below; it is a"
                       "\nstored setting and stays put once the menu is closed."
                       "\n\nNeither needs the menu open to keep working. A hairline marks the join."));

        if (compare != 0)
        {
            bool swap = config->DlssNrCompareSwap.value_or_default();
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.swap_sides_83d98149", "Swap sides"), &swap))
                config->DlssNrCompareSwap = swap;

            bool tags = config->DlssNrCompareTags.value_or_default();
            if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.label_the_sides_45f036fd", "Label the sides"), &tags))
                config->DlssNrCompareTags = tags;

            HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.writes_which_side_is_which_onto_the_frame_itself_0115a956", "Writes which side is which onto the frame itself, so a screenshot still"
                           "\nsays so after it has left this machine. Drawn into the picture's own"
                           "\nplane: in the wipe the split reveals and hides the label exactly as it"
                           "\ndoes the images, and there is nothing to drag. Swap sides moves the"
                           "\nlabels with their pictures."));

            if (tags)
            {
                float tagScale = config->DlssNrTagScale.value_or_default();
                if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.dlssnr-menu.label_size_c1be5e74", "Label size"), &tagScale, 0.5f, 5.0f, Neurotic::UiLiteral("ingame.dlssnr-menu.1fx_02b952fe", "%.1fx")))
                    config->DlssNrTagScale = std::clamp(tagScale, 0.5f, 5.0f);
                    ImGui::SameLine();
                    if (ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##tagScale"))) config->DlssNrTagScale = 1.5f;
            }

            HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.puts_the_edited_frame_on_the_other_side_worth_do_14f1fea8", "Puts the edited frame on the other side."
                           "\n\nWorth doing once you have decided which you prefer: the eye is not"
                           "\neven-handed about left and right, and a difference can read as an"
                           "\nimprovement purely from where it sits. If the same side still wins after"
                           "\nswapping, it is the pass you are seeing and not the placement."));
        }

        if (compare == 1)
        {
            float zoom = config->DlssNrCompareZoom.value_or_default();
            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.dlssnr-menu.zoom_fbc70be0", "Zoom"), &zoom, 1.0f, 2.0f, "%.2f"))
                config->DlssNrCompareZoom = std::clamp(zoom, 1.0f, 2.0f);
                ImGui::SameLine();
                if (ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##compareZoom"))) config->DlssNrCompareZoom = 1.0f;

            HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.how_much_of_the_frame_each_half_shows_a_half_is__2be658cd", "How much of the frame each half shows."
                           "\n\nA half is half as wide as the frame and just as tall, so the frame"
                           "\ncannot fill it and keep its shape."
                           "\n\nAt 1 the whole frame is there at its right proportions, with bars above"
                           "\nand below. At 2 the half is filled and the sides are cropped away"
                           "\ninstead. Anything between trades one for the other."));
        }

        if (compare == 2)
        {
            float split = config->DlssNrCompareSplit.value_or_default();
            if (ImGui::SliderFloat(Neurotic::UiLiteral("ingame.dlssnr-menu.split_244ee47d", "Split"), &split, 0.0f, 1.0f, "%.2f"))
                config->DlssNrCompareSplit = std::clamp(split, 0.0f, 1.0f);
                ImGui::SameLine();
                if (ImGui::SmallButton(Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##compareSplit"))) config->DlssNrCompareSplit = 0.5f;

            HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.where_the_wipe_cuts_left_of_it_is_the_frame_as_t_0d17f9f5", "Where the wipe cuts. Left of it is the frame as the upscaler produced it,"
                           "\nright of it is the frame the model edited."));
        }

        ImGui::EndDisabled();
        static const char* debugNames[] = { Neurotic::UiLiteral("ingame.objectruleeditor.off_dc516be5", "Off"), Neurotic::UiLiteral("ingame.dlssnr-menu.proxy_what_the_model_sees_c4cf3eb8", "Proxy (what the model sees)"), Neurotic::UiLiteral("ingame.dlssnr-menu.model_output_raw_6c07469d", "Model output (raw)"),
                                            Neurotic::UiLiteral("ingame.provider.e90b56703c26", "Difference (amplified)") };
        int debugView = (int) config->DlssNrDebugView.value_or_default();
        if (ImGui::Combo(Neurotic::UiLiteral("ingame.dlssnr-menu.debug_view_220b1693", "Debug view"), &debugView, debugNames, IM_ARRAYSIZE(debugNames)))
            config->DlssNrDebugView = (uint32_t) debugView;

        HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.proxy_is_the_picture_handed_to_the_model_if_that_d99911fa", "Proxy is the picture handed to the model -- if that looks wrong, the white point"
                       "\nis wrong and nothing downstream can be judged."
                       "\n\nDifference shows what the model actually changed, amplified twenty times and"
                       "\ncentred on grey. A flat grey frame there means it is doing nothing."));
        }

    ImGui::EndDisabled();
    EndNrCard();
    ImGui::PopItemWidth();
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
    {
        ScopedNestedTextWrap wrap {};
        const bool d3d12 = IsVulkanInput() || State::Instance().api == API::DX12;
        const bool presentRoute = config->DlssNrRoute.value_or_default() != 0;
        static unsigned int previousPassCount = 0;

        auto basic = BasicMultipass::Normalize(config->DlssNrBasicMultipass.value_or_default());
        int editor = basic.advanced ? 1 : 0;
        static const char* editors[] = { Neurotic::UiLiteral("ingame.option.0e35f6e9742e", "Basic"), Neurotic::UiLiteral("ingame.objectruleeditor.advanced_cfbc9ae1", "Advanced") };
        const float selectorWidth = ImGui::GetContentRegionAvail().x * 0.25f;
        bool enabled = config->DlssNrMultipassEnabled.value_or_default();
        if (!d3d12 && !presentRoute) ImGui::BeginDisabled();
        if (MenuControls::EmphasizedCheckbox(Neurotic::UiLiteral("ingame.dlssnr-menu.enable_nr_multipass_5f89a127", "Enable NR Multipass"), &enabled))
        {
            NrConfigSynchronization::Transaction transaction;
            config->DlssNrMultipassEnabled = enabled;
            config->DlssNrSecondLayer = enabled && config->DlssNrPasses.value_or_default() > 1;
            CancelNrEdits();
        }
        if (!d3d12 && !presentRoute) ImGui::EndDisabled();
        HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.enables_a_bounded_chain_of_one_to_ten_neural_ren_625c6687", "Enables a bounded chain of one to ten Neural Rendering passes on D3D12 and Vulkan. Each later pass consumes the fully composed image from the preceding pass and owns an independent model session and temporal history. Cost increases approximately linearly with the selected pass count."));

        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
        const auto divider = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(1.0f, ImGui::GetFrameHeight()));
        ImGui::GetWindowDrawList()->AddLine(divider,
            ImVec2(divider.x, divider.y + ImGui::GetFrameHeight()),
            ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetNextItemWidth((std::min)(selectorWidth, ImGui::GetContentRegionAvail().x));
        if (ImGui::Combo("##NrMultipassEditor", &editor, editors, 2))
        {
            BasicMultipass::Update(*config, [&](auto& p) { p.advanced = editor == 1; });
            CancelNrEdits();
            basic = config->DlssNrBasicMultipass.value_or_default();
        }

        if (!basic.advanced)
        {
            const float basicWidth=MenuControls::HalfWidthSliderGroup({Neurotic::UiLiteral("ingame.dlssnr-menu.model_resolution_cd73e06b", "Model Resolution"), Neurotic::UiLiteral("ingame.dlssnr-menu.model_strength_a6a09f10", "Model Strength"), Neurotic::UiLiteral("ingame.dlssnr-menu.detail_strength_d781fe36", "Detail Strength")});
            const auto slider = [&](const char* title, const char* id, NrOptional<float>& preview,
                                    float BasicMultipass::Profile::* member, float minimum, float maximum,
                                    const char* hint = nullptr,
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
                const std::string sliderLabel=std::string(title)+"###"+id;
                ImGui::SetNextItemWidth(basicWidth);
                ImVec2 sliderMin {}, sliderMax {};
                const bool changed = DeferredNrSlider(sliderLabel.c_str(), { &preview }, minimum, maximum, 1.0f,
                    "%d%%", true, cumulativePasses > 0 ? &sliderMin : nullptr,
                    cumulativePasses > 0 ? &sliderMax : nullptr, cumulativePasses > 0 ? title : nullptr);
                if (hint) HelpMarker(hint);
                if (cumulativePasses > 0)
                    DrawCumulativePassSegments(sliderMin, sliderMax, cumulativePasses, menuResScale);
                if (changed)
                    if (!BasicMultipass::CommitEdit(*config, original, generation, member, preview.value_or_default()))
                        CancelNrEdits();
            };
            static NrOptional<float> resolution { 1.0f }, model { 1.0f }, detail { 1.0f };
            static const char* maximums[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };
            int maximum = int(basic.maximum) - 1;
            ImGui::SetNextItemWidth(selectorWidth);
            if (ImGui::Combo(Neurotic::UiLiteral("ingame.dlssnr-menu.maximum_passes_7b761b57", "Maximum passes##NrBasicMaximum"), &maximum, maximums, 10))
            {
                BasicMultipass::Update(*config, [&](auto& p) { p.maximum = uint32_t(maximum + 1); });
                CancelNrEdits();
            }
            basic = config->DlssNrBasicMultipass.value_or_default();
            // The lambda adds ###; an ID beginning with ## would also make Reset's
            // derived label restart ImGui's hash and alias the slider itself.
            slider(Neurotic::UiLiteral("ingame.dlssnr-menu.model_resolution_cd73e06b", "Model Resolution"), "NrBasicResolution", resolution,
                   &BasicMultipass::Profile::resolution, 0.25f, 2.0f,
                   Neurotic::UiLiteral("ingame.dlssnr-menu.sets_one_model_raster_percentage_for_pass_1_and__b644f839", "Sets one model-raster percentage for Pass 1 and every active additional pass "
                   "in Basic mode. The composed frame remains full resolution. Releasing commits "
                   "the shared value and rebuilds changed models; disabling Multipass restores the "
                   "saved main settings."));
            basic = config->DlssNrBasicMultipass.value_or_default();
            if (auto reveal = Neurotic::Sleek::AnimatedRegion("##BasicDownscaler", basic.resolution > 1.0f); reveal.Visible())
            {
                static const char* downscalers[] = { Neurotic::UiLiteral("ingame.option.b3290cbc23e0", "FSR1"), Neurotic::UiLiteral("ingame.option.d64f696cc3e4", "Bicubic"), Neurotic::UiLiteral("ingame.option.e817551fdce4", "Catmull-Rom"), Neurotic::UiLiteral("ingame.option.4143056699ee", "Lanczos2"), Neurotic::UiLiteral("ingame.option.9feb40edb86b", "Lanczos3"), Neurotic::UiLiteral("ingame.option.6de2fa378185", "Kaiser2"), Neurotic::UiLiteral("ingame.option.592782aa7dcb", "Kaiser3"), Neurotic::UiLiteral("ingame.option.e7b0c0160426", "MAGIC") };
                int selected = int(basic.downscaler);
                ImGui::SetNextItemWidth(selectorWidth);
                if (ImGui::Combo(Neurotic::UiLiteral("ingame.dlssnr-menu.downscaler_b3373b8a", "Downscaler##NrBasicDownscaler"), &selected, downscalers, 8))
                    BasicMultipass::Update(*config, [&](auto& p) { p.downscaler = uint32_t(selected); });
            }
            const char* cumulativeHint =
                Neurotic::UiLiteral("ingame.dlssnr-menu.each_coloured_segment_represents_one_pass_cumula_e3376b66", "Each coloured segment represents one pass. Cumulative totals fill passes from left to right.");
            BeginNrCard("##NrBasicStrengths", Neurotic::UiLiteral("ingame.dlssnr-menu.model_detail_strength_50d5a617", "Model / detail strength"));
            slider(Neurotic::UiLiteral("ingame.dlssnr-menu.model_strength_a6a09f10", "Model Strength"), "NrBasicModel", model, &BasicMultipass::Profile::model,
                   0.0f, float(basic.maximum), cumulativeHint, basic.maximum);
            slider(Neurotic::UiLiteral("ingame.dlssnr-menu.detail_strength_d781fe36", "Detail Strength"), "NrBasicDetail", detail, &BasicMultipass::Profile::detail,
                   0.0f, float(basic.maximum), cumulativeHint, basic.maximum);
            EndNrCard();
            basic = config->DlssNrBasicMultipass.value_or_default();
            const auto requested = BasicMultipass::Count(basic);
            auto telemetry = SelectedNativeTelemetry();
            if(IsVulkanInput()){const auto chain=DlssNr::PassChainStatusVk(presentRoute?DlssNr::VkNrRoute::Present:DlssNr::VkNrRoute::Native);telemetry.layerCount=chain.completed;}
            static MenuStatus::SelectionObservation completedObservation;
            const uint64_t key = uint64_t(std::lround(basic.model * 100)) |
                (uint64_t(std::lround(basic.detail * 100)) << 12) |
                (uint64_t(StageUi::DisplayPercent(basic.resolution)) << 24) |
                (uint64_t(enabled) << 36) | (uint64_t(config->DlssNrRoute.value_or_default()) << 37);
            const bool fresh = completedObservation.Fresh(key, telemetry.gpuCompletedOutputEvaluations);
            if (enabled && requested == 0)
                ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.0_passes_requested_effect_bypassed_loaded_resour_8d0b4a9d", "0 passes requested. Effect bypassed; loaded resources retained."));
            else if (enabled && fresh && telemetry.running)
                ImGui::Text(Neurotic::UiLiteral("ingame.dlssnr-menu.u_requested_u_completed_on_the_last_frame_814a7c1a", "%u requested | %u completed on the last frame"), requested, telemetry.layerCount);
            else
                ImGui::Text(Neurotic::UiLiteral("ingame.dlssnr-menu.u_requested_completed_unavailable_4464a02c", "%u requested | completed: unavailable"), requested);
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.totals_include_pass_1_230_means_100_100_30_remai_eb91f51a", "Totals include Pass 1. 230%% means 100%% + 100%% + 30%%. Remaining settings inherit NR Settings."));
            if (!d3d12 && !presentRoute)
                ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.multipass_requires_a_supported_native_or_present_3c133a42", "Multipass requires a supported Native or Present adapter."));
            return;
        }

        BeginNrCard("##NrAdvancedChain", Neurotic::UiLiteral("ingame.dlssnr-menu.pass_chain_fcb46cea", "Pass chain"));
        ImGui::SetNextItemWidth(selectorWidth);
        const unsigned int passCount = RenderPassCountSelector(config);
        if (previousPassCount != passCount) CancelNrEdits();
        previousPassCount = passCount;

        if (ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.reset_all_86640c00", "Reset All"))) ImGui::OpenPopup(Neurotic::UiLiteral("ingame.dlssnr-menu.reset_all_multipass_settings_cf68d30c", "Reset all multipass settings?"));
        HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.restores_the_multipass_switch_and_every_setting__6737dfc3", "Restores the Multipass switch and every setting in all nine saved additional pass profiles to "
                   "their shipped defaults. Baseline Pass 1 and the shared pass count above remain unchanged. "
                   "A confirmation is required."));
        if (ImGui::BeginPopupModal(Neurotic::UiLiteral("ingame.dlssnr-menu.reset_all_multipass_settings_cf68d30c", "Reset all multipass settings?"), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(Neurotic::UiLiteral("ingame.dlssnr-menu.reset_every_additional_neural_rendering_pass_pro_b37891a7", "Reset every additional Neural Rendering pass profile to default?"));
            if (ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.confirm_ea44f268", "Confirm")))
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
            if (ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.cancel_7e4b3f1d", "Cancel"))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (!d3d12 && !presentRoute)
            ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.neural_rendering_multipass_requires_a_supported__ab85438a", "Neural Rendering Multipass requires a supported adapter."));

        auto telemetry = SelectedNativeTelemetry();
        if(IsVulkanInput()){const auto chain=DlssNr::PassChainStatusVk(presentRoute?DlssNr::VkNrRoute::Present:DlssNr::VkNrRoute::Native);telemetry.layerCount=chain.completed;telemetry.running=chain.deliverable;}
        if (enabled && d3d12)
        {
            if (telemetry.running)
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), Neurotic::UiLiteral("ingame.dlssnr-menu.u_of_u_requested_passes_completed_on_the_last_fr_ffe8f2a1", "%u of %u requested passes completed on the last frame."),
                                   telemetry.layerCount, passCount);
            else
                ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.the_pass_chain_becomes_active_when_dlss_neural_r_5f6c65d3", "The pass chain becomes active when DLSS Neural Rendering is enabled and ready."));
            if (passCount >= 4)
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                                   Neurotic::UiLiteral("ingame.dlssnr-menu.high_pass_counts_are_experimental_and_may_exhaus_e9dde6e5", "High pass counts are experimental and may exhaust GPU memory or frame time."));
        }

        EndNrCard();
        if (passCount > 1)
        {
            BeginNrCard("##NrAdvancedShared", Neurotic::UiLiteral("ingame.dlssnr-menu.shared_settings_passes_2_n_cb863f8a", "Shared settings - Passes 2-N"));
            const float globalWidth=MenuControls::HalfWidthSliderGroup({Neurotic::UiLiteral("ingame.dlssnr-menu.model_resolution_cd73e06b", "Model Resolution"), Neurotic::UiLiteral("ingame.dlssnr-menu.model_strength_a6a09f10", "Model Strength"), Neurotic::UiLiteral("ingame.dlssnr-menu.detail_strength_d781fe36", "Detail Strength")});
            const auto sharedSlider = [&](const char* label, NrOptional<float>* PassOptionRefs::* member,
                                          float minimum, float maximum, const char* hint, const char* mixedHint)
            {
                std::vector<NrOptional<float>*> targets;
                for (unsigned int index = 1; index < passCount; ++index)
                    targets.push_back(PassOptions(config, index).*member);
                ImGui::SetNextItemWidth(globalWidth);
                DeferredNrSlider(label, targets, minimum, maximum, 1.0f, "%d%%", true);
                HelpMarker(hint);
                if (pendingNrEdits[label].Mixed()) ImGui::TextDisabled("%s",Neurotic::Translate(mixedHint).c_str());
            };
            sharedSlider(Neurotic::UiLiteral("ingame.dlssnr-menu.model_resolution_cd73e06b", "Model Resolution###Model Resolution##AdditionalPassModelResolution"), &PassOptionRefs::workingScale, 0.25f, 2.0f,
                Neurotic::UiLiteral("ingame.dlssnr-menu.changes_the_model_resolution_for_every_additiona_18dd4d5f", "Changes the Model resolution for every additional pass at once: Pass 2 through the selected final pass. It never changes Pass 1. Dragging previews the shared percentage; releasing commits that percentage to all additional passes and rebuilds them once."),
                Neurotic::UiLiteral("ingame.dlssnr-menu.passes_2_n_have_mixed_model_resolutions_adjustin_2723e966", "Passes 2–N have mixed model resolutions; adjusting this slider applies one value to all of them."));
            sharedSlider(Neurotic::UiLiteral("ingame.dlssnr-menu.model_strength_a6a09f10", "Model Strength###Model Strength##AdditionalPassModelStrength"), &PassOptionRefs::intensity, 0.0f, 2.0f,
                Neurotic::UiLiteral("ingame.dlssnr-menu.sets_internal_model_intensity_for_pass_2_through_a7906573", "Sets internal model intensity for Pass 2 through the selected final pass. Release to apply and rebuild only changed child models. 100% is default; 0% does not disable model execution. Pass 1 is unchanged."),
                Neurotic::UiLiteral("ingame.dlssnr-menu.passes_2_n_have_mixed_model_strengths_adjusting__3a5ad482", "Passes 2–N have mixed model strengths; adjusting this slider applies one value to all of them."));
            sharedSlider(Neurotic::UiLiteral("ingame.dlssnr-menu.detail_strength_d781fe36", "Detail Strength###Detail Strength##AdditionalPassDetailStrength"), &PassOptionRefs::transferStrength, 0.0f, 2.0f,
                Neurotic::UiLiteral("ingame.dlssnr-menu.sets_detail_blending_for_pass_2_through_the_sele_d933815a", "Sets detail blending for Pass 2 through the selected final pass. Release to apply without rebuilding models. 100% is default; 0% hides the detail edit. Pass 1 and colour strength are unchanged."),
                Neurotic::UiLiteral("ingame.dlssnr-menu.passes_2_n_have_mixed_detail_strengths_adjusting_f777bdb1", "Passes 2–N have mixed detail strengths; adjusting this slider applies one value to all of them."));
            EndNrCard();
        }
        else
        {
            ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.select_two_or_more_passes_to_adjust_shared_model_41f23ef4", "Select two or more passes to adjust shared Model Resolution, Model Strength and Detail Strength."));
        }

        if (passCount == 1)
            ImGui::TextDisabled(Neurotic::UiLiteral("ingame.dlssnr-menu.pass_1_is_configured_in_nr_settings_select_two_o_55dc1788", "Pass 1 is configured in NR Settings. Select two or more passes above to configure additional passes here."));
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
                BeginNrCard("##NrSelectedPass", label);
                const auto pass = PassOptions(config, index);
                char scaleLabel[96] {};
                snprintf(scaleLabel, sizeof(scaleLabel), Neurotic::UiLiteral("ingame.dlssnr-menu.model_resolution_8a931f87", "Model resolution##pass%u"), index + 1);

                char resetPopup[64] {};
                snprintf(resetPopup, sizeof(resetPopup), Neurotic::UiLiteral("ingame.dlssnr-menu.reset_pass_u_profile_76de493d", "Reset Pass %u profile?##pass%u"), index + 1, index + 1);
                if (ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.reset_this_pass_51d37484", "Reset this pass"))) ImGui::OpenPopup(resetPopup);
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.restores_only_this_additional_pass_profile_to_it_1f1b0625", "Restores only this additional pass profile to its shipped defaults. Baseline Pass 1, "
                           "other additional pass profiles, and the shared pass count remain unchanged."));
                if (ImGui::BeginPopupModal(resetPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Text(Neurotic::UiLiteral("ingame.dlssnr-menu.reset_every_setting_for_pass_u_to_default_74c671cc", "Reset every setting for Pass %u to default?"), index + 1);
                    if (ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.confirm_ea44f268", "Confirm")))
                    {
                        ResetPassOptions(pass);
                        CancelNrEdits();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.cancel_7e4b3f1d", "Cancel"))) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }

                if (index > 0)
                {
                    ImGui::SameLine();
                    char copyLabel[48] {};
                    snprintf(copyLabel, sizeof(copyLabel), Neurotic::UiLiteral("ingame.dlssnr-menu.copy_pass_u_settings_5ff68ff5", "Copy Pass %u settings"), index);
                    if (ImGui::SmallButton(copyLabel))
                    {
                        CopyPassOptions(PassOptions(config, index - 1), pass);
                        CancelNrEdits();
                    }
                    HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.copies_every_saved_setting_from_the_preceding_pa_8608180e", "Copies every saved setting from the preceding pass into this pass. It does not change the shared pass count or enable Multipass."));
                }

                ImGui::PushItemWidth(220.0f * menuResScale);
                char id[96] {};
                DeferredNrSlider(scaleLabel, { pass.workingScale }, 0.25f, 2.0f, 1.0f, "%d%%", true);
                const int scale = (int) lroundf(pendingNrEdits[scaleLabel].Value() * 100.0f);
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.sets_this_pass_s_model_raster_from_25_to_200_per_39a24959", "Sets this pass's model raster from 25 to 200 percent. The composed frame remains full resolution; cost changes roughly with the square of this value."));

                static const char* downscalers[] = { Neurotic::UiLiteral("ingame.option.b3290cbc23e0", "FSR1"), Neurotic::UiLiteral("ingame.option.d64f696cc3e4", "Bicubic"), Neurotic::UiLiteral("ingame.option.e817551fdce4", "Catmull-Rom"), Neurotic::UiLiteral("ingame.option.4143056699ee", "Lanczos2"),
                                                     Neurotic::UiLiteral("ingame.option.9feb40edb86b", "Lanczos3"), Neurotic::UiLiteral("ingame.option.6de2fa378185", "Kaiser2"), Neurotic::UiLiteral("ingame.option.592782aa7dcb", "Kaiser3"), Neurotic::UiLiteral("ingame.option.e7b0c0160426", "MAGIC") };
                int downscaler = std::clamp((int) pass.scalingDownscaler->value_or_default(), 0, 7);
                if (scale <= 100) ImGui::BeginDisabled();
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.downscaler_b3373b8a", "Downscaler##pass%u"), index + 1);
                if (ImGui::Combo(id, &downscaler, downscalers, IM_ARRAYSIZE(downscalers)))
                    *pass.scalingDownscaler = (Scaler) downscaler;
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##pass%u-downscaler"), index + 1);
                ResetButton(id, [&] { *pass.scalingDownscaler = Scaler::Lanczos3; });
                if (scale <= 100) ImGui::EndDisabled();
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.chooses_the_filter_that_averages_this_pass_s_abo_5580a1dd", "Chooses the filter that averages this pass's above-native model answer back to the full-resolution frame. It applies only above 100 percent."));

                static const char* presets[] = { Neurotic::UiLiteral("ingame.menu-common.default_92fe477b", "Default"), Neurotic::UiLiteral("ingame.provider.d1ced7405198", "Preset 1"), Neurotic::UiLiteral("ingame.provider.c475c1784510", "Preset 2"), Neurotic::UiLiteral("ingame.provider.e7aebbc38aae", "Preset 3") };
                int preset = std::clamp((int) pass.preset->value_or_default(), 0, 3);
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.model_preset_1be982ca", "Model preset##pass%u"), index + 1);
                if (ImGui::Combo(id, &preset, presets, IM_ARRAYSIZE(presets))) *pass.preset = (uint32_t) preset;
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##pass%u-preset"), index + 1);
                ResetButton(id, [&] { *pass.preset = 0u; });
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.selects_the_model_preset_for_this_pass_s_indepen_91cc17ac", "Selects the model preset for this pass's independent Feature 18 session. Changing it rebuilds only this pass."));

                static const char* styles[] = { Neurotic::UiLiteral("ingame.provider.1edcf76142e5", "Default (standard)"), Neurotic::UiLiteral("ingame.option.d6acb6d51cfc", "Natural"), Neurotic::UiLiteral("ingame.option.912d0988b065", "Cinematic") };
                int style = std::clamp((int) pass.style->value_or_default(), 0, 2);
                snprintf(id, sizeof(id), "Style##pass%u", index + 1);
                if (ImGui::Combo(id, &style, styles, IM_ARRAYSIZE(styles))) *pass.style = (uint32_t) style;
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##pass%u-style"), index + 1);
                ResetButton(id, [&] { *pass.style = 0u; });
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.selects_this_pass_s_model_processing_profile_def_0de8c0f3", "Selects this pass's model processing profile. Default is strongest; Natural and Cinematic are progressively gentler alternatives."));

                static const char* transfers[] = { Neurotic::UiLiteral("ingame.option.ae25d6481fc1", "Classic"), Neurotic::UiLiteral("ingame.dlssnr-menu.matched_residual_fd28d530", "Matched residual") };
                int transfer = pass.transfer->value_or_default() == 1 ? 1 : 0;
                if (scale >= 100) ImGui::BeginDisabled();
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.enlargement_8f7cc83f", "Enlargement##pass%u"), index + 1);
                if (ImGui::Combo(id, &transfer, transfers, IM_ARRAYSIZE(transfers))) *pass.transfer = (uint32_t) transfer;
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##pass%u-enlargement"), index + 1);
                ResetButton(id, [&] { *pass.transfer = 1u; });
                if (scale >= 100) ImGui::EndDisabled();
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.chooses_how_a_sub_native_model_edit_returns_to_f_28680eb0", "Chooses how a sub-native model edit returns to full size. Matched residual enlarges only the model's difference and generally preserves colour better."));

                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.detail_strength_e855f2da", "Detail strength##pass%u"), index + 1);
                DeferredSlider(id, pass.transferStrength, 0.0f, 2.0f, 1.0f);
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.controls_how_much_of_this_pass_s_detail_edit_rea_52d3f731", "Controls how much of this pass's detail edit reaches the composed frame. Zero hides this pass's edit; values above one exaggerate it."));
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.colour_strength_6fdcccd5", "Colour strength##pass%u"), index + 1);
                DeferredSlider(id, pass.colourStrength, 0.0f, 4.0f, 1.0f);
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.controls_how_much_of_this_pass_s_colour_change_a_8b4a62e5", "Controls how much of this pass's colour change accompanies its lighting edit. Zero preserves the preceding pass's hue."));
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.highlight_guard_4dfd1908", "Highlight guard##pass%u"), index + 1);
                DeferredSlider(id, pass.maxRatio, 1.0f, 8.0f, 2.0f, Neurotic::UiLiteral("ingame.dlssnr-menu.1fx_02b952fe", "%.1fx"));
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.limits_the_brightness_multiplication_or_division_03c66e49", "Limits the brightness multiplication or division this pass may apply. The 2x default protects moving highlights."));
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.model_strength_a6a09f10", "Model Strength##pass%u"), index + 1);
                DeferredSlider(id, pass.intensity, 0.0f, 2.0f, 1.0f);
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.sets_this_pass_s_internal_model_strength_it_comm_3ac50c05", "Sets this pass's internal model strength. It commits when the slider is released and rebuilds only this pass's feature. Zero does not disable model execution."));
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.local_structure_278e0ab8", "Local structure##pass%u"), index + 1);
                DeferredSlider(id, pass.localStructure, 0.0f, 2.0f, 1.0f);
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.sets_the_local_structure_strength_for_this_pass__65e647ab", "Sets the local-structure strength for this pass's independent model session."));
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.local_tone_bfd8370e", "Local tone##pass%u"), index + 1);
                DeferredSlider(id, pass.localTone, 0.0f, 2.0f, 1.0f);
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.sets_the_local_tone_strength_for_this_pass_s_ind_f6d12d1a", "Sets the local-tone strength for this pass's independent model session."));
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.skin_structure_7500cc17", "Skin structure##pass%u"), index + 1);
                DeferredSlider(id, pass.skinStructure, -1.0f, 2.0f, -1.0f);
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.sets_skin_structure_strength_for_this_pass_minus_8ba2cbd2", "Sets skin-structure strength for this pass. Minus one follows Local structure; zero and above tune it independently."));

                bool autoMask = pass.autoMask->value_or_default();
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.auto_skin_mask_30c7534c", "Auto skin mask##pass%u"), index + 1);
                if (ImGui::Checkbox(id, &autoMask)) *pass.autoMask = autoMask;
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##pass%u-auto-mask"), index + 1);
                ResetButton(id, [&] { *pass.autoMask = true; });
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.lets_this_pass_s_model_identify_skin_rather_than_73421b48", "Lets this pass's model identify skin rather than treating every region uniformly."));

                static const char* reversibleModes[] = { Neurotic::UiLiteral("ingame.dlssnr-menu.soft_knee_composition_c250dfcd", "Soft-knee composition"), Neurotic::UiLiteral("ingame.dlssnr-menu.neutwo_composition_d3aa36e9", "Neutwo composition"),
                                                          Neurotic::UiLiteral("ingame.dlssnr-menu.neutwo_pure_inverse_aad577e4", "Neutwo pure inverse"), Neurotic::UiLiteral("ingame.dlssnr-menu.hybrid_composition_b4f92297", "Hybrid composition"), Neurotic::UiLiteral("ingame.dlssnr-menu.hybrid_pure_inverse_4bbbf18c", "Hybrid pure inverse") };
                int reversible = std::clamp((int) pass.reversibleMode->value_or_default(), 0, 4);
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.proxy_composition_cf6178ff", "Proxy composition##pass%u"), index + 1);
                if (ImGui::Combo(id, &reversible, reversibleModes, IM_ARRAYSIZE(reversibleModes)))
                    *pass.reversibleMode = (uint32_t) reversible;
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##pass%u-proxy"), index + 1);
                ResetButton(id, [&] { *pass.reversibleMode = 0u; });
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.chooses_this_pass_s_reversible_proxy_composition_cd4462d7", "Chooses this pass's reversible proxy/composition path. Soft-knee is the shipped default; the Neutwo modes remain experimental."));

                bool apply = pass.applyModel->value_or_default();
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.dlssnr-menu.apply_the_model_8fad708a", "Apply the model##pass%u"), index + 1);
                if (ImGui::Checkbox(id, &apply)) *pass.applyModel = apply;
                snprintf(id, sizeof(id), Neurotic::UiLiteral("ingame.menu-common.reset_14de5ef1", "Reset##pass%u-apply"), index + 1);
                ResetButton(id, [&] { *pass.applyModel = true; });
                HelpMarker(Neurotic::UiLiteral("ingame.dlssnr-menu.off_keeps_this_pass_evaluating_and_preserving_it_683837d0", "Off keeps this pass evaluating and preserving its history but hides only its edit, leaving the preceding completed image visible."));

                ImGui::PopItemWidth();
                EndNrCard();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
}

void RenderScreenshotMenu(Config* config)
{
    {
        ScopedNestedTextWrap wrap {};
        const unsigned int route = config->DlssNrRoute.value_or_default();
        const bool present = route == 1 || route == 2;
        const bool enabled = config->GetDlssNrRuntimeSnapshot().enabled;
        const bool busy = ComparisonScreenshotBusy();
        const bool nativePair = NativeComparisonScreenshotAvailable();
        const auto vkCapabilities=CurrentVulkanNrCapabilities();
        const bool vulkan=State::Instance().api==API::Vulkan;
        const char* backendRefusal = vulkan?(vkCapabilities.capture.available?nullptr:vkCapabilities.capture.reason.c_str()):Screenshots::BackendRefusal(route, enabled, State::Instance().api == API::DX12);
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.save_before_after_pngs_to_neuroticscreenshots_be_49ebb0ae", "Save before/after PNGs to NeuroticScreenshots beside the game."));
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.hdr_images_use_a_shared_sdr_preview_conversion_t_adc2e1fa", "HDR images use a shared SDR preview conversion; they may differ from your display."));
        ImGui::SeparatorText(Neurotic::UiLiteral("ingame.dlssnr-menu.images_ebcc0fbe", "Images"));
        const bool analysis = Advisor().running;
        ImGui::BeginDisabled(busy || analysis || backendRefusal != nullptr);
        bool before = config->ScreenshotNrOff.value_or_default();
        ImGui::BeginDisabled(enabled && !present && !nativePair);
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.nr_off_365e7c7a", "NR off"), &before)) config->ScreenshotNrOff = before;
        ImGui::EndDisabled();
        bool native = config->ScreenshotNativeNr.value_or_default();
        ImGui::BeginDisabled(present || !enabled);
        if (ImGui::Checkbox(nativePair ? Neurotic::UiLiteral("ingame.dlssnr-menu.native_nr_on_92235424", "Native NR on###ScreenshotNative") : Neurotic::UiLiteral("ingame.dlssnr-menu.current_full_output_d94c895f", "Current full output###ScreenshotNative"), &native)) config->ScreenshotNativeNr = native;
        ImGui::EndDisabled();
        bool imageOnly = config->ScreenshotPresentNr.value_or_default();
        ImGui::BeginDisabled(!present || !enabled);
        if (ImGui::Checkbox(Neurotic::UiLiteral("ingame.dlssnr-menu.present_nr_on_10dafc3b", "Present NR on"), &imageOnly)) config->ScreenshotPresentNr = imageOnly;
        ImGui::EndDisabled();
        if (ImGui::Button(backendRefusal ? Neurotic::UiLiteral("ingame.dlssnr-menu.unavailable_for_this_route_558a8680", "Unavailable for this route###TakeComparisonScreenshots")
                                         : Neurotic::UiLiteral("ingame.dlssnr-menu.take_comparison_screenshots_2bdd674c", "Take comparison screenshots###TakeComparisonScreenshots")))
            RequestComparisonScreenshot();
        ImGui::EndDisabled();
        if (busy && ImGui::Button(Neurotic::UiLiteral("ingame.dlssnr-menu.cancel_screenshots_ce5d1649", "Cancel screenshots"))) CancelComparisonScreenshot();
        ImGui::TextWrapped("%s",Neurotic::Translate(ComparisonScreenshotStatus().c_str()).c_str());
        if (backendRefusal) ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.comparison_unavailable_for_the_current_route_s_a1abf07e", "Comparison unavailable for the current route: %s"),Neurotic::Translate(backendRefusal).c_str());
        if (analysis) ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.finish_or_cancel_analysis_before_taking_comparis_c575235d", "Finish or cancel analysis before taking comparisons."));
        ImGui::SeparatorText(Neurotic::UiLiteral("ingame.dlssnr-menu.capture_notes_ee7f3a8d", "Capture notes"));
        ImGui::TextWrapped(vulkan ? Neurotic::UiLiteral("ingame.dlssnr-menu.starts_after_five_seconds_the_neurotic_menu_is_e_ba649cb9", "Starts after five seconds. The NeuRotic menu is excluded; other overlays remain.") :
            Neurotic::UiLiteral("ingame.dlssnr-menu.captures_the_next_ready_frame_the_neurotic_menu__d44cfc9a", "Captures the next ready frame. The NeuRotic menu is excluded; other overlays remain."));
        if (!vulkan && nativePair && config->DlssNrRunBeforeSr.value_or_default() && !SelectedNativeTelemetry().nativeRayReconstructionActive)
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.performance_pairs_use_fresh_history_and_may_brie_f27ef9bc", "Performance pairs use fresh history and may briefly pause rendering or use extra memory. Live history is unchanged."));
        else if (nativePair)
            ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.native_pairs_show_the_same_scene_before_later_ga_43d87b0d", "Native pairs show the same scene before later game effects and HUD."));
        ImGui::TextWrapped(Neurotic::UiLiteral("ingame.dlssnr-menu.for_nr_on_images_enable_apply_model_and_turn_off_0c3f13c1", "For NR-on images, enable Apply Model and turn off Debug view / Compare."));
        ImGui::SeparatorText(Neurotic::UiLiteral("ingame.dlssnr-menu.screenshot_keybind_92e5f942", "Screenshot keybind"));
        MenuCommon::RenderScreenshotKeybind(config);
    }
}

} // namespace DlssNr



