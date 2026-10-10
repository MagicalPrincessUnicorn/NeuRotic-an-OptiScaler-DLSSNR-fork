#pragma once

#include "SysUtils.h"
#include <Config.h>

#include <imgui/imgui.h>
#include "Localization.h"
#include "MenuContextOwner.h"
#include "localization/LanguageRuntime.h"
#include "SleekSections.h"
#include <atomic>

class ScopedIndent
{
  public:
    explicit ScopedIndent(float indent = 16.0f) : m_indent(indent) { ImGui::Indent(m_indent); }

    ~ScopedIndent() { ImGui::Unindent(m_indent); }

  private:
    float m_indent;
};

template <typename T> struct MenuOption
{
    T value;
    std::string label;
    std::string tooltip;
    bool disabled = false;
    bool hidden = false;
    std::string labelId,tooltipId;
    MenuOption(T v,const char* label,const char* tip="",bool disabled=false,bool hidden=false):value(v),label(label),tooltip(tip),disabled(disabled),hidden(hidden),labelId(Neurotic::Localization::BoundLiteralId(label)),tooltipId(Neurotic::Localization::BoundLiteralId(tip)){}
    MenuOption(T v,std::string label,std::string tip="",bool disabled=false,bool hidden=false):value(v),label(std::move(label)),tooltip(std::move(tip)),disabled(disabled),hidden(hidden){}

    MenuOption& set_disabled(bool condition, const char* reason = "")
    {
        if (condition)
        {
            disabled = true;
            if (*reason) {tooltip = reason;tooltipId=Neurotic::Localization::BoundLiteralId(reason);}
        }
        return *this;
    }

    MenuOption& set_disabled(bool condition,const std::string& reason){if(condition){disabled=true;if(!reason.empty()){tooltip=reason;tooltipId.clear();}}return *this;}
    MenuOption& set_hidden(bool condition)
    {
        if (condition)
        {
            hidden = true;
        }
        return *this;
    }
};

class MenuCommon
{
  private:
    // internal values
    inline static HWND _handle = nullptr;
    // inline static WNDPROC _oWndProc = nullptr;
    inline static bool _isVisible = false;
    inline static std::atomic<uint64_t> _visibilityGeneration{0};
    static void SetVisibility(bool visible);
    inline static bool _rendererOwnsCapture = false;
    inline static bool _rendererCaptureAvailable = false;
    inline static double _rendererCapturePresentedAt = 0.0;
    inline static bool _isInited = false;
    inline static bool _isUWP = false;

    // mipmap calculations
    inline static bool _showMipmapCalcWindow = false;
    inline static bool _showHudlessWindow = false;
    inline static float _mipBias = 0.0f;
    inline static float _mipBiasCalculated = 0.0f;
    inline static uint32_t _mipmapUpscalerQuality = 0;
    inline static float _mipmapUpscalerRatio = 0;
    inline static uint32_t _displayWidth = 0;
    inline static uint32_t _renderWidth = 0;

    inline static UINT64 _frameCount = 0;

    // reflex
    inline static float _limitFps = std::numeric_limits<float>::infinity();

    // ffx
    inline static int _ffxUpscalerIndex = -1;
    inline static int _ffxFGIndex = -1;

    // output scaling
    inline static float _ssRatio = 0.0f;
    inline static bool _ssEnabled = false;
    inline static Scaler _ssDownsampler = Scaler::FSR1;

    // ui scale
    inline static int _selectedScale = 0;

    // overlay states
    inline static bool _dx11Ready = false;
    inline static bool _dx12Ready = false;
    inline static bool _vulkanReady = false;
    inline static bool _showMainMenuGraphs = false;

    inline static void ShowTooltip(const char* tip);

    inline static void ShowHelpMarker(const char* tip);
    inline static void ShowResetButton(CustomOptional<bool, NoDefault>* initFlag, const char* buttonName);
    inline static void ReInitUpscaler();

    inline static void SeparatorWithHelpMarker(const char* label, const char* tip);

    static Upscaler GetBackendCode(const API api);
    static void GetCurrentBackendInfo(const API api, Upscaler& upscaler, std::string* name);
    static void RenderUpscalerCombo(const API api, Upscaler currentUpscaler, const std::vector<Upscaler>& options);
    static void AddDx11Backends(Upscaler upscaler);
    static void AddDx12Backends(Upscaler upscaler);
    static void AddVulkanBackends(Upscaler upscaler);
    template <class Option> static void AddResourceBarrier(const char* name, Option* value);
    template <HasDefaultValue B> static void AddDLSSRenderPreset(const char* name, CustomOptional<uint32_t, B>* value);
    template <HasDefaultValue B> static void AddDLSSDRenderPreset(const char* name, CustomOptional<uint32_t, B>* value);
    template <typename TStorage, typename T>
    static void PopulateCombo(const char* name, TStorage& currentValue,
                              const std::vector<MenuOption<T>>& options);

    struct RenderMenuContext;

    // RenderMenu orchestration helpers. These keep the public RenderMenu() flow short
    // while preserving the original ImGui layout and draw order.
    static void UpdateRenderTiming(RenderMenuContext& ctx);
    static void UpdateMenuInputMode(RenderMenuContext& ctx);
    static void HandleMenuShortcuts(RenderMenuContext& ctx);
    static void UpdateVersionAndStartupNotifications(RenderMenuContext& ctx);
    static void BeginMenuFrameIfNeeded(RenderMenuContext& ctx);
    static void RenderSplashWindow(RenderMenuContext& ctx);
    static void RenderNotifications(RenderMenuContext& ctx);
    static void UpdateFrameTimeAverages(RenderMenuContext& ctx);
    static void RenderPerformanceOverlay(RenderMenuContext& ctx);

    // Labels for the Neural Rendering comparison views. Drawn every frame, into the frame plane,
    // so a screenshot keeps them and the wipe reveals and hides them like the images.
    static void RenderNrCompareTags();
    static void RenderMainMenuWindow(RenderMenuContext& ctx);

    // RenderMainMenuWindow section helpers. These keep the main window flow readable
    // while keeping each top-level page responsible for exactly one group of controls.
    static void RenderMainMenuHeaderMessages(RenderMenuContext& ctx);
    static void RenderMainMenuTabs(RenderMenuContext& ctx);
    static void RenderGeneralPage(RenderMenuContext& ctx);
    static void RenderUpscalingPage(RenderMenuContext& ctx);
    static void RenderUpscalerPreflight(RenderMenuContext& ctx);
    // This is the single owner for the Neural Rendering page, including the separate
    // collapsible Multipass section beneath the first-pass controls.
    static void RenderNeuralRenderingPage(RenderMenuContext& ctx);
    static void RenderNeuralRenderingExperimentalSettings(RenderMenuContext& ctx);
    static void RenderFrameGenerationPage(RenderMenuContext& ctx);
    static void RenderAdvancedPage(RenderMenuContext& ctx);
    static void RenderToolsPage(RenderMenuContext& ctx);
    static void RenderDiagnosticsPage(RenderMenuContext& ctx);
    static void RenderActiveUpscalerSettings(RenderMenuContext& ctx);
    static void RenderFrameGenerationSelection(RenderMenuContext& ctx);
    static void RenderFrameGenerationRuntimeSettings(RenderMenuContext& ctx);
    static void RenderFsrCommonSettings(RenderMenuContext& ctx);
    static void RenderFramerateSettings(RenderMenuContext& ctx);
    static void RenderFakenvapiSettings(RenderMenuContext& ctx);
    static void RenderLowLatencySettings(RenderMenuContext& ctx);
    static void RenderActiveImageSettings(RenderMenuContext& ctx);
    static void RenderMagnifierSettings(RenderMenuContext& ctx);
    static void RenderQuirksSettings(RenderMenuContext& ctx);
    static void RenderAdvancedSettings(RenderMenuContext& ctx);
    static void RenderLoggingSettings(RenderMenuContext& ctx);
    static void RenderThemeSettings(RenderMenuContext& ctx);
    static void RenderFpsOverlaySettings(RenderMenuContext& ctx);
    static void RenderUpscalerInputsSettings(RenderMenuContext& ctx);
    static void RenderVsyncSettings(RenderMenuContext& ctx);
    static void RenderMipmapBiasSettings(RenderMenuContext& ctx);
    static void RenderAnisotropicFilteringSettings(RenderMenuContext& ctx);
    static void RenderKeybindSettings(RenderMenuContext& ctx);
    static void RenderMainMenuGraphs(RenderMenuContext& ctx);
    static void RenderMainMenuBottomBar(RenderMenuContext& ctx);
    static void RenderMainMenuWindowActions(RenderMenuContext& ctx);
    static void RenderMainMenuSupportLink(RenderMenuContext& ctx);
    static void RenderMipmapBiasWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags);
    static void RenderHudlessResourcesWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags);

    static void UpdateManualInput(HWND targetHwnd);

  public:
    static Neurotic::MenuLayout::ContextOwner& OwnedContext();
    static Neurotic::MenuLayout::ContextOwner::Scope BindContext() {return Neurotic::MenuLayout::ContextOwner::Scope(OwnedContext());}
    static bool HasOwnedContext() {return OwnedContext().Get()!=nullptr;}
    static void Dx11Inited() { _dx11Ready = true; }
    static void RenderScreenshotKeybind(Config* config);
    static void Dx12Inited() { _dx12Ready = true; }
    static void VulkanInited() { _vulkanReady = true; }
    static bool IsInited() { return _isInited; }
    static bool IsVisible() { return _isVisible; }
    static uint64_t VisibilityGeneration() { return _visibilityGeneration.load(); }
    static HWND Handle() { return _handle; }

    static bool RenderMenu(float drawWidth=0, float drawHeight=0);
    static void FinalizeFrame();
    // Vulkan commits capture only after an actual overlay Present succeeds.
    static void DeferInputCapture();
    static void SetRendererCaptureAvailable(bool available);
    static bool CanRetainRendererCaptureOnBusyFrame();
    static void ProcessUnavailableInput();
    static void Init(HWND InHwnd, bool isUWP);
    static void Shutdown();
    static void HideMenu();
    static void Present();
};
