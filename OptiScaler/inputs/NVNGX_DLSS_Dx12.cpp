#include "pch.h"
#include <dlssnr/FrameTrace.h>
#include <dlssnr/PreFg.h>
#include <dlssnr/FgLifecycle.h>
#include <dlssnr/NrGpuSafety.h>
#include <dlssnr/NativeFeatureRegistry.h>
#include <dlssnr/NativeTemporalInputs.h>
#include "Util.h"
#include "Config.h"

#include "NVNGX_DLSS.h"
#include "NVNGX_Parameter.h"
#include "proxies/NVNGX_Proxy.h"
#include "dlssnr/DlssNr.h"
#include "dlssnr/DlssNr_ExposureScan.h"
#include <upscalers/dlss/DLSSFeature_Dx12.h>
#include <shaders/output_scaling/OS_Dx12.h>

#include <upscalers/FeatureProvider_Dx12.h>
#include "upscalers/dlss/DLSSFeature_Dx12.h"

#include <framegen/nvngx/Nvngx_FG.h>
#include "FG/FSR3_Dx12_FG.h"
#include "FG/Upscaler_Inputs_Dx12.h"

#include <imgui/ImGuiNotify.hpp>

#include <hooks/D3D12_Hooks.h>

#include <dxgi1_4.h>
#include <shared_mutex>
#include "detours/detours.h"
#include <ankerl/unordered_dense.h>
#include <misc/IdentifyGpu.h>

static ankerl::unordered_dense::map<unsigned int, ContextData<IFeature_Dx12>> Dx12Contexts;
static DlssNr::NativeFeatureRegistry<NVSDK_NGX_Feature> HandleToFeature;
static std::mutex ngxObservationMutex;
static std::mutex fgObservationMutex;
static ID3D12Device* D3D12Device = nullptr;

static const char* NgxFeatureName(NVSDK_NGX_Feature feature)
{
    switch (feature)
    {
    case NVSDK_NGX_Feature_SuperSampling:
        return "Super Resolution";
    case NVSDK_NGX_Feature_InPainting:
        return "InPainting";
    case NVSDK_NGX_Feature_ImageSuperResolution:
        return "Image Super Resolution";
    case NVSDK_NGX_Feature_SlowMotion:
        return "Slow Motion";
    case NVSDK_NGX_Feature_VideoSuperResolution:
        return "Video Super Resolution";
    case NVSDK_NGX_Feature_ImageSignalProcessing:
        return "Image Signal Processing";
    case NVSDK_NGX_Feature_DeepResolve:
        return "Deep Resolve";
    case NVSDK_NGX_Feature_FrameGeneration:
        return "Frame Generation";
    case NVSDK_NGX_Feature_DeepDVC:
        return "DeepDVC";
    case NVSDK_NGX_Feature_RayReconstruction:
        return "Ray Reconstruction";
    default:
        return "Unknown/Reserved";
    }
}

static void LogNgxCreateTrace(NVSDK_NGX_Feature feature, const char* route, NVSDK_NGX_Result result,
                              const NVSDK_NGX_Handle* handle)
{
    LOG_INFO("DLSS-NR Test 0.9 trace: Create feature {} (id {}) via {}; result 0x{:X}, handle {}",
             NgxFeatureName(feature), static_cast<int>(feature), route, static_cast<uint32_t>(result),
             handle != nullptr ? handle->Id : 0);
}

struct NgxEvaluationTraceObservation
{
    bool tracked = false;
    NVSDK_NGX_Feature feature = NVSDK_NGX_Feature_Reserved0;
};

static std::unordered_map<unsigned int, NgxEvaluationTraceObservation> NgxEvaluationTraceObservations;

static void LogNgxEvaluationTrace(unsigned int handleId, bool tracked, NVSDK_NGX_Feature feature)
{
    const NgxEvaluationTraceObservation observed { tracked, feature };
    std::lock_guard lock(ngxObservationMutex);
    const auto it = NgxEvaluationTraceObservations.find(handleId);
    if (it != NgxEvaluationTraceObservations.end() && it->second.tracked == observed.tracked &&
        it->second.feature == observed.feature)
        return;

    NgxEvaluationTraceObservations[handleId] = observed;
    const char* route = handleId < DLSS_MOD_ID_OFFSET ? "native NGX handle" : "OptiScaler/provider handle";
    LOG_INFO("DLSS-NR Test 0.9 trace: Evaluate handle {} feature {} (id {}), {}, {}", handleId,
             tracked ? NgxFeatureName(feature) : "untracked", tracked ? static_cast<int>(feature) : -1,
             tracked ? "mapped at creation" : "no mapped creation", route);
}

static bool IsNrPipelineFeature(NVSDK_NGX_Feature feature)
{
    return feature == NVSDK_NGX_Feature_SuperSampling ||
           feature == NVSDK_NGX_Feature_RayReconstruction;
}

// Log pipeline identity only when it changes. This is intentionally handle-scoped: a secondary
// viewport must not overwrite the main viewport's evidence merely because it evaluated later.
struct NrPipelineObservation
{
    NVSDK_NGX_Feature feature = (NVSDK_NGX_Feature) 0;
    bool preSr = false;
    unsigned int inputWidth = 0;
    unsigned int inputHeight = 0;
    unsigned int outputWidth = 0;
    unsigned int outputHeight = 0;
};

static std::unordered_map<unsigned int, NrPipelineObservation> NrPipelineObservations;

static void LogNrPipelineObservation(unsigned int handleId, NVSDK_NGX_Feature feature,
                                     NVSDK_NGX_Parameter* params, bool performanceMode)
{
    if (!IsNrPipelineFeature(feature) || params == nullptr)
        return;

    void* color = nullptr;
    void* output = nullptr;
    params->Get(NVSDK_NGX_Parameter_Color, &color);
    params->Get(NVSDK_NGX_Parameter_Output, &output);

    NrPipelineObservation observed {};
    observed.feature = feature;
    observed.preSr = feature == NVSDK_NGX_Feature_SuperSampling && performanceMode;

    if (color != nullptr)
    {
        const auto desc = static_cast<ID3D12Resource*>(color)->GetDesc();
        observed.inputWidth = static_cast<unsigned int>(desc.Width);
        observed.inputHeight = desc.Height;
    }
    if (output != nullptr)
    {
        const auto desc = static_cast<ID3D12Resource*>(output)->GetDesc();
        observed.outputWidth = static_cast<unsigned int>(desc.Width);
        observed.outputHeight = desc.Height;
    }

    std::lock_guard lock(ngxObservationMutex);
    const auto it = NrPipelineObservations.find(handleId);
    if (it != NrPipelineObservations.end() && it->second.feature == observed.feature &&
        it->second.preSr == observed.preSr && it->second.inputWidth == observed.inputWidth &&
        it->second.inputHeight == observed.inputHeight && it->second.outputWidth == observed.outputWidth &&
        it->second.outputHeight == observed.outputHeight)
        return;

    NrPipelineObservations[handleId] = observed;
    const bool rr = feature == NVSDK_NGX_Feature_RayReconstruction;
    LOG_INFO("DLSS-NR: handle {} feature {} input {}x{} output {}x{}; placement {}",
             handleId, rr ? "Ray Reconstruction" : "Super Resolution", observed.inputWidth,
             observed.inputHeight, observed.outputWidth, observed.outputHeight,
             observed.preSr ? "NR -> SR" : (rr ? "native mode-aware RR -> NR" : "SR -> NR"));
}

static int evalCounter = 0;
static bool shutdown = false;
static bool _skipInit = false;
static wchar_t const** paths;

class ScopedInitDx12
{
  private:
    bool previousState;

  public:
    ScopedInitDx12()
    {
        previousState = _skipInit;
        _skipInit = true;
    }

    ~ScopedInitDx12() { _skipInit = previousState; }
};

static void UpdateInitPaths(NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    State::Instance().NVNGX_FeatureInfo_Paths.clear();

    if (InFeatureInfo != nullptr)
    {
        auto exePath = Util::ExePath().remove_filename();

        std::optional<std::filesystem::path> nvngxDlssPath = std::nullopt;
        std::optional<std::filesystem::path> nvngxDlssDPath = std::nullopt;
        std::optional<std::filesystem::path> nvngxDlssGPath = std::nullopt;

        // Check DLSS path
        if (State::Instance().NVNGX_DLSS_Path.has_value())
        {
            nvngxDlssPath = std::filesystem::path(State::Instance().NVNGX_DLSS_Path.value());
        }
        else
        {
            auto path = Util::FindFilePath(exePath, "nvngx_dlss.dll");

            if (path.has_value())
                nvngxDlssPath = path.value();
        }

        // Check DLSS-D path
        if (State::Instance().NVNGX_DLSSD_Path.has_value())
        {
            nvngxDlssDPath = std::filesystem::path(State::Instance().NVNGX_DLSSD_Path.value());
        }
        else
        {
            auto path = Util::FindFilePath(exePath, "nvngx_dlssd.dll");

            if (path.has_value())
                nvngxDlssDPath = path.value();
        }

        // Check DLSS-G path
        if (State::Instance().NVNGX_DLSSG_Path.has_value())
        {
            nvngxDlssGPath = std::filesystem::path(State::Instance().NVNGX_DLSSG_Path.value());
        }
        else
        {
            auto path = Util::FindFilePath(exePath, "nvngx_dlssg.dll");

            if (path.has_value())
                nvngxDlssGPath = path.value();
        }

        // Override locations
        if (Config::Instance()->DLSSFeaturePath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(Config::Instance()->DLSSFeaturePath.value());

        // If DLSS path is overriden
        if (Config::Instance()->NVNGX_DLSS_Library.has_value() && nvngxDlssPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssPath.value().parent_path().wstring());

        // OptiDll Path
        State::Instance().NVNGX_FeatureInfo_Paths.push_back(Config::Instance()->MainDllPath.value());

        // Original paths from NVNGX
        for (size_t i = 0; i < InFeatureInfo->PathListInfo.Length; i++)
        {
            const wchar_t* path = InFeatureInfo->PathListInfo.Path[i];
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(std::wstring(path));
        }

        // Exe path
        State::Instance().NVNGX_FeatureInfo_Paths.push_back(exePath.wstring());

        // If DLSS path is not overriden
        if (!Config::Instance()->NVNGX_DLSS_Library.has_value() && nvngxDlssPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssPath.value().parent_path().wstring());

        // Add found locations
        if (nvngxDlssDPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssDPath.value().parent_path().wstring());

        if (nvngxDlssGPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssGPath.value().parent_path().wstring());

        // Build pointer array
        paths = new const wchar_t*[State::Instance().NVNGX_FeatureInfo_Paths.size()];
        for (size_t i = 0; i < State::Instance().NVNGX_FeatureInfo_Paths.size(); ++i)
        {
            paths[i] = State::Instance().NVNGX_FeatureInfo_Paths[i].c_str();
            LOG_DEBUG("Feature Path [{}]: {}", i, wstring_to_string(State::Instance().NVNGX_FeatureInfo_Paths[i]));
        }

        InFeatureInfo->PathListInfo.Path = paths;
        InFeatureInfo->PathListInfo.Length = (int) State::Instance().NVNGX_FeatureInfo_Paths.size();
    }
}

#pragma region DLSS Init Calls

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_Ext(unsigned long long InApplicationId,
                                                        const wchar_t* InApplicationDataPath, ID3D12Device* InDevice,
                                                        NVSDK_NGX_Version InSDKVersion,
                                                        const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    LOG_FUNC();

    NVSDK_NGX_FeatureCommonInfo localFeatureInfo = {};

    if (InFeatureInfo != nullptr)
        std::memcpy(&localFeatureInfo, InFeatureInfo, sizeof(NVSDK_NGX_FeatureCommonInfo));

    if (!_skipInit)
        UpdateInitPaths(&localFeatureInfo);

    State::Instance().NVNGX_ApplicationId = InApplicationId;
    State::Instance().NVNGX_ApplicationDataPath = std::wstring(InApplicationDataPath);
    State::Instance().NVNGX_Version = InSDKVersion;
    State::Instance().NVNGX_FeatureInfo = &localFeatureInfo;
    State::Instance().NVNGX_Version = InSDKVersion;

    if (Config::Instance()->DLSSEnabled.value_or_default() && !_skipInit)
    {
        if (Config::Instance()->UseGenericAppIdWithDlss.value_or_default())
            InApplicationId = app_id_override;

        if (NVNGXProxy::NVNGXModule() == nullptr)
            NVNGXProxy::InitNVNGX();

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D12_Init_Ext() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D12_Init_Ext");

            auto result = NVNGXProxy::D3D12_Init_Ext()(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion,
                                                       &localFeatureInfo);
            LOG_INFO("calling NVNGXProxy::D3D12_Init_Ext result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx12Inited(true);
        }
        else
        {
            LOG_WARN("NVNGXProxy::NVNGXModule or NVNGXProxy::D3D12_Init_Ext is nullptr!");
        }
    }

    if (InFeatureInfo != nullptr && InSDKVersion > 0x0000013)
        State::Instance().NVNGX_Logger = InFeatureInfo->LoggingInfo;

    if (State::Instance().nvngxDx12Inited && InDevice == D3D12Device)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None)
    {
        Nvngx_FG::D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, &localFeatureInfo);
    }

    LOG_INFO("AppId: {0}", InApplicationId);
    LOG_INFO("SDK: {0:x}", (unsigned int) InSDKVersion);
    LOG_INFO(L"InApplicationDataPath {0}", std::wstring(InApplicationDataPath));

    D3D12Device = InDevice;
    State::Instance().currentD3D12Device = InDevice;
    D3D12Hooks::HookDevice(InDevice);

    State::Instance().nvngxDx12Inited = true;

    UpscalerInputsDx12::Init(InDevice);
    DlssNr::NotifyDeviceInit(InDevice);

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init(unsigned long long InApplicationId,
                                                    const wchar_t* InApplicationDataPath, ID3D12Device* InDevice,
                                                    const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                    NVSDK_NGX_Version InSDKVersion)
{
    LOG_FUNC();

    NVSDK_NGX_FeatureCommonInfo localFeatureInfo = {};

    if (InFeatureInfo != nullptr)
        std::memcpy(&localFeatureInfo, InFeatureInfo, sizeof(NVSDK_NGX_FeatureCommonInfo));

    if (!_skipInit)
        UpdateInitPaths(&localFeatureInfo);

    if (Config::Instance()->DLSSEnabled.value_or_default() && !_skipInit)
    {
        if (Config::Instance()->UseGenericAppIdWithDlss.value_or_default())
            InApplicationId = app_id_override;

        if (NVNGXProxy::NVNGXModule() == nullptr)
            NVNGXProxy::InitNVNGX();

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D12_Init() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D12_Init");

            auto result = NVNGXProxy::D3D12_Init()(InApplicationId, InApplicationDataPath, InDevice, &localFeatureInfo,
                                                   InSDKVersion);

            LOG_INFO("calling NVNGXProxy::D3D12_Init result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx12Inited(true);
        }
    }

    if (State::Instance().nvngxDx12Inited && InDevice == D3D12Device)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    // if (State::Instance().activeFgInput == FGInput::NvngxFG)
    //{
    //     Nvngx_FG::D3D12_Init(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    // }

    ScopedInitDx12 scopedInit {};
    auto result =
        NVSDK_NGX_D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, &localFeatureInfo);

    LOG_DEBUG("was called NVSDK_NGX_D3D12_Init_Ext");
    return result;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_ProjectID(const char* InProjectId,
                                                              NVSDK_NGX_EngineType InEngineType,
                                                              const char* InEngineVersion,
                                                              const wchar_t* InApplicationDataPath,
                                                              ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                                              const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    LOG_FUNC();

    NVSDK_NGX_FeatureCommonInfo localFeatureInfo = {};

    if (InFeatureInfo != nullptr)
        std::memcpy(&localFeatureInfo, InFeatureInfo, sizeof(NVSDK_NGX_FeatureCommonInfo));

    if (!_skipInit)
        UpdateInitPaths(&localFeatureInfo);

    if (Config::Instance()->DLSSEnabled.value_or_default() && !_skipInit)
    {
        if (Config::Instance()->UseGenericAppIdWithDlss.value_or_default())
            InProjectId = project_id_override;

        if (NVNGXProxy::NVNGXModule() == nullptr)
            NVNGXProxy::InitNVNGX();

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D12_Init_ProjectID() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D12_Init_ProjectID");

            auto result =
                NVNGXProxy::D3D12_Init_ProjectID()(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath,
                                                   InDevice, InSDKVersion, &localFeatureInfo);

            LOG_INFO("calling NVNGXProxy::D3D12_Init_ProjectID result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx12Inited(true);
        }
    }

    LOG_INFO("InProjectId: {0}", InProjectId);
    LOG_INFO("InEngineType: {0}", (int) InEngineType);
    LOG_INFO("InEngineVersion: {0}", InEngineVersion);

    State::Instance().NVNGX_ProjectId = std::string(InProjectId);
    State::Instance().NVNGX_Engine = InEngineType;
    State::Instance().NVNGX_EngineVersion = std::string(InEngineVersion);

    if (State::Instance().nvngxDx12Inited && InDevice == D3D12Device)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    ScopedInitDx12 scopedInit {};
    auto result = NVSDK_NGX_D3D12_Init_Ext(0x1337, InApplicationDataPath, InDevice, InSDKVersion, &localFeatureInfo);
    return result;
}

// Not sure about this one, original nvngx does not export this method
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_with_ProjectID(
    const char* InProjectId, NVSDK_NGX_EngineType InEngineType, const char* InEngineVersion,
    const wchar_t* InApplicationDataPath, ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
    NVSDK_NGX_Version InSDKVersion)
{
    LOG_FUNC();

    LOG_INFO("InProjectId: {0}", InProjectId);
    LOG_INFO("InEngineType: {0}", (int) InEngineType);
    LOG_INFO("InEngineVersion: {0}", InEngineVersion);

    State::Instance().NVNGX_ProjectId = std::string(InProjectId);
    State::Instance().NVNGX_Engine = InEngineType;
    State::Instance().NVNGX_EngineVersion = std::string(InEngineVersion);

    if (State::Instance().nvngxDx12Inited)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    auto result = NVSDK_NGX_D3D12_Init_Ext(0x1337, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);

    return result;
}

#pragma endregion

#pragma region DLSS Shutdown Calls

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Shutdown(void)
{
    shutdown = true;

    // NR owns driver features, capability parameters, and device-bound scratch resources.
    // Release them while the native NGX core is still live so a later initialization cannot reuse a
    // stale generation.
    if (!DlssNr::Shutdown())
    {
        shutdown = false;
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
    State::Instance().nvngxDx12Inited = false;

    D3D12Device = nullptr;

    State::Instance().currentFeature = nullptr;

    // Unhooking and cleaning stuff causing issues during shutdown.
    // Disabled for now to check if it cause any issues
    // UnhookAll();
    DLSSFeatureDx12::Shutdown(D3D12Device);

    // Added `&& !State::Instance().isShuttingDown` hack for crash on exit
    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::IsDx12Inited() &&
        NVNGXProxy::D3D12_Shutdown() != nullptr && !State::Instance().isShuttingDown)
    {
        auto result = NVNGXProxy::D3D12_Shutdown()();
        NVNGXProxy::SetDx12Inited(false);
    }

    // Unhooking and cleaning stuff causing issues during shutdown.
    // Disabled for now to check if it cause any issues
    // HooksDx::UnHook();

    // Disabled to prevent crash
    if (State::Instance().currentFG != nullptr && State::Instance().activeFgInput == FGInput::Upscaler)
    {
        if (State::Instance().isShuttingDown)
            State::Instance().currentFG->Shutdown();
        else
            State::Instance().currentFG->DestroyFGContext();

        State::Instance().clearCapturedHudlesses = true;
    }

    shutdown = false;

    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None)
    {
        Nvngx_FG::D3D12_Shutdown();
    }

    State::Instance().nvngxDx12Inited = false;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Shutdown1(ID3D12Device* InDevice)
{
    shutdown = true;
    // Shutdown1 must release NR before either native NGX shutdown variant is invoked.
    if (!DlssNr::Shutdown())
    {
        shutdown = false;
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
    State::Instance().nvngxDx12Inited = false;

    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None)
    {
        Nvngx_FG::D3D12_Shutdown1(InDevice);
    }

    // Added `&& !State::Instance().isShuttingDown` hack for crash on exit
    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::IsDx12Inited() &&
        NVNGXProxy::D3D12_Shutdown1() != nullptr && !State::Instance().isShuttingDown)
    {
        auto result = NVNGXProxy::D3D12_Shutdown1()(InDevice);
        NVNGXProxy::SetDx12Inited(false);
    }

    return NVSDK_NGX_D3D12_Shutdown();
}

#pragma endregion

#pragma region DLSS Parameter Calls

/**
 * @brief [Deprecated NGX API] Superceeded by NVSDK_NGX_AllocateParameters and NVSDK_NGX_GetCapabilityParameters.
 *
 * Retrieves a common NVSDK parameter map for providing params to the SDK. The lifetime of this
 * map is NOT managed by the application. It is expected to be managed internally by the SDK.
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetParameters(NVSDK_NGX_Parameter** OutParameters)
{
    LOG_FUNC();

    if (OutParameters == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    // If DLSS is enabled and the real DLSS module is loaded, get native NGX table
    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::D3D12_GetParameters() != nullptr)
    {
        LOG_INFO("Calling NVNGXProxy::D3D12_GetParameters");
        auto result = NVNGXProxy::D3D12_GetParameters()(OutParameters);
        LOG_INFO("Calling NVNGXProxy::D3D12_GetParameters result: {0:X}, ptr: {1:X}", (UINT) result,
                 (UINT64) *OutParameters);

        // Copy OptiScaler config to real NGX param table
        if (result == NVSDK_NGX_Result_Success)
        {
            InitNGXParameters(*OutParameters, API::DX12);
            SetNGXParamAllocType(*(*OutParameters), NGX_AllocTypes::NVPersistent);
            return NVSDK_NGX_Result_Success;
        }
    }

    // Get custom parameters if using custom backend
    static NVNGX_Parameters oldParams = NVNGX_Parameters(API::DX12, true);
    *OutParameters = &oldParams;
    InitNGXParameters(*OutParameters, API::DX12);

    LOG_DEBUG("Returning custom Opti parameters");

    return NVSDK_NGX_Result_Success;
}

/**
 * @brief Allocates a new NVSDK parameter map pre-populated with NGX capabilities and information about available
 * features. The output parameter map may also be used in the same ways as a parameter map allocated with
 * AllocateParameters(). The lifetime of this map is managed by the calling application with DestroyParameters().
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetCapabilityParameters(NVSDK_NGX_Parameter** OutParameters)
{
    LOG_FUNC();

    if (OutParameters == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    // Get native DLSS params if DLSS is enabled and the module is loaded
    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::IsDx12Inited() && NVNGXProxy::D3D12_GetCapabilityParameters() != nullptr)
    {
        LOG_INFO("Calling NVNGXProxy::D3D12_GetCapabilityParameters");
        auto result = NVNGXProxy::D3D12_GetCapabilityParameters()(OutParameters);
        LOG_INFO("Calling NVNGXProxy::D3D12_GetCapabilityParameters result: {0:X}, ptr: {1:X}", (UINT) result,
                 (UINT64) *OutParameters);

        if (result == NVSDK_NGX_Result_Success)
        {
            // Init external NGX table with current configuration and mark as dynamic+external
            InitNGXParameters(*OutParameters, API::DX12);
            SetNGXParamAllocType(*(*OutParameters), NGX_AllocTypes::NVDynamic);
            return NVSDK_NGX_Result_Success;
        }
    }

    // Get custom parameters if using custom backend
    auto& params = *(new NVNGX_Parameters(API::DX12, false));
    InitNGXParameters(&params, API::DX12);
    *OutParameters = &params;

    LOG_DEBUG("Returning custom Opti parameters");

    return NVSDK_NGX_Result_Success;
}

/**
 * @brief Allocates a new parameter map used to provide parameters needed by the DLSS API. The lifetime of this map
 * is managed by the calling application with DestroyParameters().
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_AllocateParameters(NVSDK_NGX_Parameter** OutParameters)
{
    LOG_FUNC();

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::D3D12_AllocateParameters() != nullptr)
    {
        LOG_INFO("Calling NVNGXProxy::D3D12_AllocateParameters");
        auto result = NVNGXProxy::D3D12_AllocateParameters()(OutParameters);
        LOG_INFO("Calling NVNGXProxy::D3D12_AllocateParameters result: {0:X}, ptr: {1:X}", (UINT) result,
                 (UINT64) *OutParameters);

        if (result == NVSDK_NGX_Result_Success)
        {
            SetNGXParamAllocType(*(*OutParameters), NGX_AllocTypes::NVDynamic);
            return result;
        }
    }

    auto* params = new NVNGX_Parameters(API::DX12, false);
    *OutParameters = params;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (InParameters == nullptr)
        return NVSDK_NGX_Result_Fail;

    InitNGXParameters(InParameters, API::DX12);

    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None)
    {
        Nvngx_FG::D3D12_PopulateParameters_Impl(InParameters);
    }

    return NVSDK_NGX_Result_Success;
}

/**
 * @brief Destroys a given input parameter map created with AllocateParameters or GetCapabilityParameters.
 Must not be called on maps returned by GetParameters(). Unsupported tables will not be freed.
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_DestroyParameters(NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (InParameters == nullptr)
        return NVSDK_NGX_Result_Fail;

    const bool isUsingDlss = Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule();
    const bool success = TryDestroyNGXParameters(InParameters, NVNGXProxy::D3D12_DestroyParameters());

    if (isUsingDlss)
        UpscalerInputsDx12::Reset();

    return success ? NVSDK_NGX_Result_Success : NVSDK_NGX_Result_Fail;
}

#pragma endregion

#pragma region DLSS Feature Calls

static Upscaler GetUpscalerBackend()
{
    Upscaler upscaler = Upscaler::XeSS; // Default

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    if (NVNGXProxy::IsDx12Inited() && primaryGpu.dlssCapable)
        upscaler = Upscaler::DLSS;

    if (primaryGpu.fsr4Support != FSR4Support::None)
        upscaler = Upscaler::FFX;

    if (Config::Instance()->Dx12Upscaler.has_value())
        upscaler = Config::Instance()->Dx12Upscaler.value();

    return upscaler;
}

static bool EnsureD3D12Device(ID3D12GraphicsCommandList* cmdList)
{
    if (D3D12Device)
        return true;

    LOG_DEBUG("Get D3D12 device from InCmdList!");

    if (FAILED(cmdList->GetDevice(IID_PPV_ARGS(&D3D12Device))) || !D3D12Device)
    {
        LOG_ERROR("Can't get Dx12Device from InCmdList!");
        return false;
    }

    return true;
}

static NVSDK_NGX_Result TryEvaluateOptiFeature(ID3D12GraphicsCommandList* InCmdList,
                                               const NVSDK_NGX_Handle* InFeatureHandle,
                                               NVSDK_NGX_Parameter* InParameters,
                                               PFN_NVSDK_NGX_ProgressCallback InCallback);

static NVSDK_NGX_Result TryCreateOptiFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                             NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    State& state = State::Instance();
    const Config& cfg = *Config::Instance();

    state.api = DX12;

    const uint32_t handleId = IFeature::GetNextHandleId();
    LOG_INFO("Creating OptiScaler feature, HandleId: {}", handleId);

    if (!EnsureD3D12Device(InCmdList))
    {
        LOG_ERROR("Failed to acquire D3D12 device");
        return NVSDK_NGX_Result_Fail;
    }

    // Determine backend name
    Upscaler upscalerBackend;
    if (InFeatureID == NVSDK_NGX_Feature_SuperSampling)
    {
        upscalerBackend = GetUpscalerBackend();
        LOG_INFO("Creating {} upscaler feature", UpscalerDisplayName(upscalerBackend));
    }
    else
    {
        upscalerBackend = Upscaler::DLSSD;
        LOG_INFO("Creating DLSSD (Ray Reconstruction) feature");
    }

    // Root signature restoration setup
    const bool restoreCompute = cfg.RestoreComputeSignature.value_or_default();
    const bool restoreGraphics = cfg.RestoreGraphicSignature.value_or_default();
    const bool shouldRestoreSigs = restoreCompute || restoreGraphics;

    // To avoid capturing the upscaler creation
    D3D12Hooks::SetRootSignatureTracking(false);

    if (shouldRestoreSigs)
        D3D12Hooks::HookToCommandListLate(InCmdList);

    // Create context entry
    Dx12Contexts[handleId] = {};

    // Retrieve feature implementation
    if (!FeatureProvider_Dx12::GetFeature(upscalerBackend, handleId, InParameters, &Dx12Contexts[handleId].feature))
    {
        LOG_ERROR("Failed to retrieve feature implementation for '{}'", UpscalerDisplayName(upscalerBackend));

        D3D12Hooks::SetRootSignatureTracking(true);

        Dx12Contexts.erase(handleId);
        return NVSDK_NGX_Result_Fail;
    }

    // Assign handle
    if (*OutHandle == nullptr)
        *OutHandle = new NVSDK_NGX_Handle { handleId };
    else
        (*OutHandle)->Id = handleId;

    state.autoExposure.reset();

    IFeature_Dx12* feature = Dx12Contexts[handleId].feature.get();

    // RR already owns both denoising and mode-aware reconstruction. Initialize it from the game's
    // untouched parameter block; creating a second DLSS feature here would upscale an already
    // quality-reduced intermediate instead of allowing RR to reconstruct directly to Output.
    const bool featureInitialized = feature->Init(D3D12Device, InCmdList, InParameters);

    // Initialize feature
    if (featureInitialized)
    {
        state.currentFeature = feature;
        evalCounter = 0;
        UpscalerInputsDx12::Reset();
    }
    else
    {
        LOG_ERROR("Feature '{}' initialization failed falling back to FSR 2.1.2", UpscalerDisplayName(upscalerBackend));
        state.newBackend = Upscaler::FSR21;
        state.changeBackend[handleId] = true;
    }

    // Restore root signatures
    if (shouldRestoreSigs)
        D3D12Hooks::RestoreRoot(InCmdList);

    D3D12Hooks::SetRootSignatureTracking(true);

    if (state.activeFgInput == FGInput::Upscaler)
        state.fgChanged = true;

    return NVSDK_NGX_Result_Success;
}

/**
 * @brief Instantiates a new feature based on the given unique feature ID and param table and
 * provides a handle used to reference the feature elsewhere in the API. Currently supports
 * various TSR and Frame Generation algorithms, including a special case for DLSS-RR passthrough.
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                             NVSDK_NGX_Feature InFeatureID,
                                                             NVSDK_NGX_Parameter* InParameters,
                                                             NVSDK_NGX_Handle** OutHandle)
{
    LOG_FUNC();

    if (!InCmdList)
    {
        LOG_ERROR("InCmdList is null");
        return NVSDK_NGX_Result_Fail;
    }

    if (!OutHandle)
    {
        LOG_ERROR("OutHandle is null");
        return NVSDK_NGX_Result_Fail;
    }

    const State& state = State::Instance();
    const Config& cfg = *Config::Instance();

    // DLSSG replacements passthrough
    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None && Nvngx_FG::isDx12Available() &&
        InFeatureID == NVSDK_NGX_Feature_FrameGeneration)
    {
        LOG_INFO("Passthrough to DLSSG Replacement's CreateFeature for FrameGeneration");

        NVSDK_NGX_Result res = Nvngx_FG::D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, OutHandle);

        if (res == NVSDK_NGX_Result_Success && *OutHandle)
        {
            LOG_INFO("Created modded DLSSG feature with HandleId: {}", (*OutHandle)->Id);
            HandleToFeature.Set((*OutHandle)->Id, InFeatureID);
        }

        LogNgxCreateTrace(InFeatureID, "DLSSG replacement", res, *OutHandle);
        return res;
    }

    // Native DLSS passthrough (exclude SuperSampling and RayReconstruction)
    if (InFeatureID != NVSDK_NGX_Feature_SuperSampling && InFeatureID != NVSDK_NGX_Feature_RayReconstruction)
    {
        if (cfg.DLSSEnabled.value_or_default() && NVNGXProxy::InitDx12(D3D12Device) &&
            NVNGXProxy::D3D12_CreateFeature() != nullptr)
        {
            LOG_INFO("Passthrough to native NGX CreateFeature for feature {}", (int) InFeatureID);

            const bool diagnosticFg = InFeatureID == NVSDK_NGX_Feature_FrameGeneration;
            const auto operation = diagnosticFg ? DlssNr::FgLifecycle::Begin("fg-create-begin") : 0;
            NVSDK_NGX_Result res = NVNGXProxy::D3D12_CreateFeature()(InCmdList, InFeatureID, InParameters, OutHandle);
            if (diagnosticFg)
                DlssNr::FgLifecycle::Created(operation,
                    res == NVSDK_NGX_Result_Success && OutHandle && *OutHandle ? (*OutHandle)->Id : 0,
                    static_cast<uint32_t>(res), res == NVSDK_NGX_Result_Success, InCmdList);
            if (diagnosticFg && res == NVSDK_NGX_Result_Success && OutHandle && *OutHandle)
                DlssNr::PreFg::PublishNativeFgCreated((*OutHandle)->Id);

            if (res == NVSDK_NGX_Result_Success && *OutHandle)
            {
                LOG_INFO("Native CreateFeature success, HandleId: {}", (*OutHandle)->Id);
                HandleToFeature.Set((*OutHandle)->Id, InFeatureID);
            }
            else
            {
                LOG_INFO("Native CreateFeature failed: 0x{:X}", (uint32_t) res);
            }

            LogNgxCreateTrace(InFeatureID, "native NGX passthrough", res, *OutHandle);
            return res;
        }

        if (InFeatureID == NVSDK_NGX_Feature_FrameGeneration)
            DlssNr::FgLifecycle::Created(0, 0, static_cast<uint32_t>(NVSDK_NGX_Result_FAIL_FeatureNotSupported), false, InCmdList);
        LOG_WARN("Native DLSS passthrough not available for feature {}", (int) InFeatureID);
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    }

    // OptiScaler internal handling (SuperSampling or RayReconstruction)
    auto tryResult = TryCreateOptiFeature(InCmdList, InFeatureID, InParameters, OutHandle);

    if (tryResult == NVSDK_NGX_Result_Success)
    {
        if (*OutHandle) HandleToFeature.Set((*OutHandle)->Id, InFeatureID);
        if (InFeatureID == NVSDK_NGX_Feature_RayReconstruction)
        {
            LOG_INFO("DLSS-NR: native mode-aware RR feature created; active reconstruction follows evaluation");
        }
    }

    LogNgxCreateTrace(InFeatureID, "OptiScaler pipeline", tryResult, *OutHandle);
    return tryResult;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    LOG_FUNC();

    if (!InHandle)
        return NVSDK_NGX_Result_Success;

    // Before any feature's resources are freed, drop the exposure scan's references to whatever it
    // captured. The scan AddRef's candidates and never released them; a Streamline/DLSS-D resource it
    // pinned would otherwise be used after its heap is freed here -- the Cyberpunk device-removal.
    // Capture is gated on NR being enabled (not the scan source), so this drops whatever was captured
    // whenever NR is on; a no-op only when NR is off.
    DlssNr::ExposureScan::ReleaseTrackedResources();

    auto handleId = InHandle->Id;

    const auto featureSnapshot = HandleToFeature.Read(handleId);
    const NVSDK_NGX_Feature releasedFeature = featureSnapshot.feature;
    const auto diagnosticInstance = DlssNr::FgLifecycle::Find(handleId);
    const auto nativeFgInstance = DlssNr::PreFg::NativeFgInstance(handleId);
    const bool diagnosticFg = releasedFeature == NVSDK_NGX_Feature_FrameGeneration || diagnosticInstance != 0;
    const auto finishRelease = [&](NVSDK_NGX_Result result) {
        if (HandleToFeature.Released(handleId, featureSnapshot, result == NVSDK_NGX_Result_Success))
        {
            {
                std::lock_guard lock(ngxObservationMutex);
                NrPipelineObservations.erase(handleId);
                NgxEvaluationTraceObservations.erase(handleId);
            }
            if (releasedFeature == NVSDK_NGX_Feature_RayReconstruction &&
                !HandleToFeature.Has(NVSDK_NGX_Feature_RayReconstruction))
                DlssNr::SetNativeRayReconstructionActive(false);
        }
        return result;
    };

    // A replacement can retain the same dimensions and preset. Treat release of either feature
    // that owns the NR seam as a continuity break; unrelated NGX features must not perturb NR.
    if (handleId < DLSS_MOD_ID_OFFSET && IsNrPipelineFeature(releasedFeature))
        DlssNr::NotifyUpscalerRelease();

    // Clean up framegen
    if (State::Instance().currentFG != nullptr && State::Instance().activeFgInput == FGInput::Upscaler)
    {
        State::Instance().fgChanged = true;
        State::Instance().currentFG->DestroyFGContext();
        State::Instance().clearCapturedHudlesses = true;
        UpscalerInputsDx12::Reset();
    }

    if (!shutdown)
        LOG_INFO("releasing feature with id {0}", handleId);

    // OptiScaler handles start after this offset. If it's outside this range, it doesn't belong to OptiScaler.
    if (handleId < DLSS_MOD_ID_OFFSET)
    {
        if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::D3D12_ReleaseFeature() != nullptr)
        {
            if (!shutdown)
                LOG_INFO("calling D3D12_ReleaseFeature for ({0})", handleId);

            // Clean up real DLSS feature
            const auto operation = diagnosticFg ? DlssNr::FgLifecycle::Begin("fg-release-begin", handleId) : 0;
            auto result = NVNGXProxy::D3D12_ReleaseFeature()(InHandle);
            if (diagnosticFg)
                DlssNr::FgLifecycle::Released(operation, handleId, diagnosticInstance,
                    static_cast<uint32_t>(result), result == NVSDK_NGX_Result_Success);
            if (releasedFeature == NVSDK_NGX_Feature_FrameGeneration &&
                result == NVSDK_NGX_Result_Success)
                DlssNr::PreFg::PublishNativeFgReleased(handleId, nativeFgInstance);

            if (!shutdown)
                LOG_INFO("D3D12_ReleaseFeature result for ({0}): {1:X}", handleId, (UINT) result);

            return finishRelease(result);
        }
        else
        {
            if (!shutdown)
                LOG_INFO("D3D12_ReleaseFeature not available for ({0})", handleId);
            if (diagnosticFg)
                DlssNr::FgLifecycle::Released(0, handleId, diagnosticInstance,
                    static_cast<uint32_t>(NVSDK_NGX_Result_FAIL_FeatureNotFound), false);

            return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        }
    }
    // Clean up OptiScaler feature with framegen
    else if (State::Instance().activeFgNvngx != FGNvngxReplacement::None && handleId >= NVNGX_PROVIDER_ID_OFFSET)
    {
        LOG_INFO("D3D12_ReleaseFeature modded DLSSG with HandleId: {0}", handleId);
        return finishRelease(Nvngx_FG::D3D12_ReleaseFeature(InHandle));
    }

    // Remove feature from context map
    if (auto it = Dx12Contexts.find(handleId); it != Dx12Contexts.end())
    {
        auto& entry = it->second;

        if (auto* deviceContext = entry.feature.get())
        {
            // Clear global reference if it matches
            if (deviceContext == State::Instance().currentFeature)
                State::Instance().currentFeature = nullptr;

            // Erase from map (smart pointer reset is implicit on erase)
            Dx12Contexts.erase(it);
        }
    }
    else
    {
        // Fallback Error Handling
        if (!shutdown)
            LOG_ERROR("can't release feature with id {0}!", handleId);
    }

    return finishRelease(NVSDK_NGX_Result_Success);
}

/**
 * @brief Used by the client application to check for feature support.
 * @param Adapter Device the feature is for.
 * @param FeatureDiscoveryInfo Specifies the feature being queried.
 * @param OutSupported Used to indicate whether a feature is supported and its requirements.
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetFeatureRequirements(
    IDXGIAdapter* Adapter, const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
    NVSDK_NGX_FeatureRequirement* OutSupported)
{
    LOG_DEBUG("for ({0})", (int) FeatureDiscoveryInfo->FeatureID);

    const bool isUpscaling = FeatureDiscoveryInfo->FeatureID == NVSDK_NGX_Feature_SuperSampling;
    const bool isFG = FeatureDiscoveryInfo->FeatureID == NVSDK_NGX_Feature_FrameGeneration;
    const bool dlssgAdjacent = Nvngx_FG::isDx12Available() || State::Instance().activeFgInput == FGInput::DLSSG;

    if (isUpscaling || (isFG && dlssgAdjacent))
    {
        if (OutSupported == nullptr)
        {
            static auto tmp = NVSDK_NGX_FeatureRequirement();
            OutSupported = &tmp;
        }

        OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_Supported;
        OutSupported->MinHWArchitecture = 0;

        // Some old windows 10 os version
        strcpy_s(OutSupported->MinOSVersion, "10.0.10240.16384");
        return NVSDK_NGX_Result_Success;
    }

    if (Config::Instance()->DLSSEnabled.value_or_default() && IdentifyGpu::getPrimaryGpu().dlssCapable &&
        NVNGXProxy::NVNGXModule() == nullptr)
    {
        NVNGXProxy::InitNVNGX();
    }

    if (Config::Instance()->DLSSEnabled.value_or_default() && IdentifyGpu::getPrimaryGpu().dlssCapable &&
        NVNGXProxy::D3D12_GetFeatureRequirements() != nullptr)
    {
        LOG_DEBUG("D3D12_GetFeatureRequirements for ({0})", (int) FeatureDiscoveryInfo->FeatureID);
        auto result = NVNGXProxy::D3D12_GetFeatureRequirements()(Adapter, FeatureDiscoveryInfo, OutSupported);
        LOG_DEBUG("D3D12_GetFeatureRequirements result for ({0}): {1:X}", (int) FeatureDiscoveryInfo->FeatureID,
                  (UINT) result);

        return result;
    }
    else
    {
        LOG_DEBUG("D3D12_GetFeatureRequirements not available for ({0})", (int) FeatureDiscoveryInfo->FeatureID);
    }

    OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_AdapterUnsupported;
    return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
}

static NVSDK_NGX_Result TryEvaluateOptiFeature(ID3D12GraphicsCommandList* InCmdList,
                                               const NVSDK_NGX_Handle* InFeatureHandle,
                                               NVSDK_NGX_Parameter* InParameters,
                                               PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    State& state = State::Instance();
    const Config& cfg = *Config::Instance();
    const uint32_t handleId = InFeatureHandle->Id;

    auto ctxIt = Dx12Contexts.find(handleId);

    if (ctxIt == Dx12Contexts.end())
    {
        LOG_WARN("No context found for handle {}", handleId);
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    ContextData<IFeature_Dx12>& ctxData = ctxIt->second;
    IFeature_Dx12* feature = ctxData.feature.get();

    if (feature == nullptr) // Prevent source api name flicker when dlssg is active
        state.setInputApiName = state.currentInputApiName;

    const auto targetApiName =
        !state.setInputApiName.has_value() ? ApiUpscalerInput::DLSS_DX12 : state.setInputApiName.value();

    if (state.currentInputApiName != targetApiName)
        state.currentInputApiName = targetApiName;

    state.setInputApiName.reset();
    evalCounter++;

    // Skip evaluation for the first N frames if configured
    if (cfg.SkipFirstFrames.has_value() && evalCounter < cfg.SkipFirstFrames.value())
        return NVSDK_NGX_Result_Success;

    // Root signature restoration setup
    const bool restoreCompute = cfg.RestoreComputeSignature.value_or_default();
    const bool restoreGraphics = cfg.RestoreGraphicSignature.value_or_default();
    const bool shouldRestoreSigs = restoreCompute || restoreGraphics;

    if (shouldRestoreSigs)
    {
        D3D12Hooks::HookToCommandListLate(InCmdList);

        if (!D3D12Hooks::CanRestoreRootSignature(InCmdList))
        {
            LOG_DEBUG("Skipping upscaling because can't restore root signature");
            return NVSDK_NGX_Result_Success;
        }
    }

    if (InCallback)
        LOG_INFO("Progress callback provided but unused in synchronous OptiScaler path");

    // Resolution change detection (only for upscalers that may require recreation)
    if (feature != nullptr)
    {
        const bool isFFX =
            feature->GetUpscalerType() == Upscaler::FFX || feature->GetUpscalerType() == Upscaler::FFX_on12;
        const bool isFSR31OrLater = isFFX && feature->Version() >= feature_version { 3, 1, 0 };

        // FSR 3.1 supports upscaleSize that doesn't need reinit to change output resolution
        if (!isFSR31OrLater && feature->UpdateOutputResolution(InParameters))
            state.changeBackend[handleId] = true;
    }

    // To avoid capturing potential upscaler change (creation) and then upscaling itself
    D3D12Hooks::SetRootSignatureTracking(false);

    // Backend change or recreation requested
    if (state.changeBackend[handleId])
    {
        UpscalerInputsDx12::Reset();

        auto successfulPhase = FeatureProvider_Dx12::ChangeFeature(state.newBackend, D3D12Device, InCmdList, handleId,
                                                                   InParameters, &ctxData);
        feature = ctxData.feature.get();

        evalCounter = 0;

        if (ctxData.changeBackendCounter != 0 || !successfulPhase)
        {
            D3D12Hooks::SetRootSignatureTracking(true);
            return NVSDK_NGX_Result_Success;
        }
    }

    // Fallback to FSR 2.1.2 if feature failed to initialize and user didn't explicitly request it
    if (!feature->IsInited() && cfg.Dx12Upscaler.value_or_default() != Upscaler::FSR21)
    {
        LOG_WARN("Feature '{}' failed to initialize. Falling back to FSR 2.1.2", feature->Name());
        ImGui::InsertNotification({ ImGuiToastType::Warning, 10000, "Falling back to FSR 2.1.2" });

        state.newBackend = Upscaler::FSR21;
        state.changeBackend[handleId] = true;

        D3D12Hooks::SetRootSignatureTracking(true);

        return NVSDK_NGX_Result_Success;
    }

    state.currentFeature = feature;

    // Prepare upscaling inputs
    UpscalerInputsDx12::UpscaleStart(InCmdList, InParameters, feature);
    FSR3FG::SetUpscalerInputs(InCmdList, InParameters, feature);

    // Evaluate the feature
    bool evalSuccess = false;
    {
        // Resource tracking
        UpscalerInputsDx12::UpscaleEnd(InCmdList, InParameters, feature);

        ScopedSkipHeapCapture skip {};
        evalSuccess = feature->Evaluate(InCmdList, InParameters);
    }

    if (!evalSuccess)
    {
        LOG_ERROR("Feature evaluation failed for '{}'", feature->Name());
        ImGui::InsertNotification({ ImGuiToastType::Error, 10000, "Upscaler failed to run!" });
    }

    // Restore root signatures
    if (shouldRestoreSigs)
        D3D12Hooks::RestoreRoot(InCmdList);

    D3D12Hooks::SetRootSignatureTracking(true);

    return evalSuccess ? NVSDK_NGX_Result_Success : NVSDK_NGX_Result_Fail;
}

/**
 * @brief Per-frame feature execution. Runs a feature (upscaler, framegen, etc.) on a given command list using a
 * preexisting feature instance referenced by a unique handle.
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                               const NVSDK_NGX_Handle* InFeatureHandle,
                                                               NVSDK_NGX_Parameter* InParameters,
                                                               PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    if (!InFeatureHandle)
    {
        LOG_DEBUG("InFeatureHandle is null");
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    if (!InCmdList || !InParameters)
    {
        LOG_ERROR("NGX evaluation requires a command list and parameter block");
        return NVSDK_NGX_Result_Fail;
    }

    const uint32_t handleId = InFeatureHandle->Id;
    LOG_DEBUG("EvaluateFeature - Handle: {}, CmdList: {:p}", handleId, (void*) InCmdList);

    const State& state = State::Instance();
    const Config& cfg = *Config::Instance();

    const auto featureSnapshot = HandleToFeature.Read(handleId);
    if (!featureSnapshot)
    {
        LOG_WARN("EvaluateFeature received untracked handle {}; NR is not attached", handleId);
    }
    const NVSDK_NGX_Feature feature = featureSnapshot.feature;
    const bool isNrPipelineFeature = IsNrPipelineFeature(feature);
    const bool isSuperResolution = feature == NVSDK_NGX_Feature_SuperSampling;
    const bool isRayReconstruction = feature == NVSDK_NGX_Feature_RayReconstruction;
    // Cyberpunk retains its RR handle after switching RR off and begins evaluating a separate
    // Super Resolution handle. Track the feature that owns this frame's reconstruction seam;
    // handle creation alone otherwise leaves Present's RR compatibility gate permanently stale.
    if (isNrPipelineFeature)
        DlssNr::SetNativeRayReconstructionActive(isRayReconstruction);
    const auto traceEvaluation = DlssNr::FrameTrace::Event("ngx-evaluate-enter",
        "handle={} feature={} list={:p} fgOutput={}", handleId, static_cast<unsigned int>(feature),
        static_cast<void*>(InCmdList), static_cast<unsigned int>(state.activeFgOutput));
    DlssNr::FrameTrace::Context traceContext(DlssNr::FrameTrace::nativeObservation, traceEvaluation);
    LogNgxEvaluationTrace(handleId, static_cast<bool>(featureSnapshot), feature);
    LogNrPipelineObservation(handleId, feature, InParameters, cfg.DlssNrRunBeforeSr.value_or_default());
    static size_t evalWithoutFG = 0;
    const bool fgCreated = HandleToFeature.Has(NVSDK_NGX_Feature_FrameGeneration);

    static std::optional<float> lastDlssgCameraNear {};
    static std::optional<float> lastDlssgCameraFar {};
    void* fgBackbuffer = nullptr;
    NVSDK_NGX_Result fgBackbufferResult = NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (feature == NVSDK_NGX_Feature_FrameGeneration)
    {
        if (InParameters)
            fgBackbufferResult = InParameters->Get("DLSSG.Backbuffer", &fgBackbuffer);
        if (DlssNr::FrameTrace::Armed() && InParameters)
        {
            void* traceHudless = nullptr;
            const auto hudlessResult = InParameters->Get("DLSSG.HUDLess", &traceHudless);
            const auto* preFg = DlssNr::PreFg::forwardingFrame;
            NR_FRAME_TRACE("ngx-fg-input", "provider=nvngx handle={} list={:p} backbuffer={:p} "
                "hudless={:p} backbufferResult={} hudlessResult={} realSequence={} providerToken={} "
                "nrSubmitted={} association={} handleInstance={} generation={} claimGeneration={} providerGeneration={}", handleId,
                static_cast<void*>(InCmdList), fgBackbuffer, traceHudless,
                static_cast<unsigned int>(fgBackbufferResult), static_cast<unsigned int>(hudlessResult),
                preFg ? preFg->sequence : 0, preFg ? preFg->key : 0, preFg && preFg->outputSubmitted,
                preFg ? "present-call-scope" : "unknown", DlssNr::FgLifecycle::Find(handleId),
                DlssNr::FgLifecycle::Read().generation, preFg ? preFg->diagnosticClaim.generation : 0,
                preFg ? preFg->providerGeneration : 0);
        }
        int frameCount = 0;
        InParameters->Get("DLSSG.MultiFrameCount", &frameCount);
        float dlssgCameraNear = 0.0f;
        float dlssgCameraFar = 0.0f;
        const bool haveNear = InParameters->Get("DLSSG.CameraNear", &dlssgCameraNear) == NVSDK_NGX_Result_Success;
        const bool haveFar = InParameters->Get("DLSSG.CameraFar", &dlssgCameraFar) == NVSDK_NGX_Result_Success;
        {
            std::lock_guard lock(fgObservationMutex);
            evalWithoutFG = 0;
            if (haveNear) lastDlssgCameraNear = dlssgCameraNear;
            if (haveFar) lastDlssgCameraFar = dlssgCameraFar;
        }
        State::Instance().dlssgDetectedInterpolationCount = frameCount;
        ReflexHooks::setDlssgFrameCount(frameCount);
    }
    else if (fgCreated)
    {
        bool reportDisabled = false;
        {
            std::lock_guard lock(fgObservationMutex);
            if (evalWithoutFG < 6) reportDisabled = ++evalWithoutFG == 6;
        }
        if (reportDisabled)
        {
            // Report FG as disabled
            State::Instance().dlssgDetectedInterpolationCount = 0;
            ReflexHooks::setDlssgFrameCount(0);
        }
    }

    // One NR request/tuning snapshot covers both sides of this upscaler call. A mode edit
    // during native SR must not schedule NR once before SR and again after it.
    const auto nrSettings = isNrPipelineFeature ? TryNrConfigSnapshot(cfg)
                                               : std::optional<NrConfigSnapshot<Config>>{};

    // Native DLSS passthrough
    if (handleId < DLSS_MOD_ID_OFFSET)
    {
        if (cfg.DLSSEnabled.value_or_default() && NVNGXProxy::D3D12_EvaluateFeature() != nullptr)
        {
            LOG_DEBUG("Passthrough to native DLSS EvaluateFeature for handle {}", handleId);

            if (isSuperResolution && nrSettings)
                DlssNr::EvaluateBeforeUpscale(InCmdList, InParameters, nullptr, &*nrSettings, true);

            if (feature == NVSDK_NGX_Feature_FrameGeneration)
            {
                ID3D12Resource* nativeBackbuffer = nullptr;
                if (fgBackbufferResult == NVSDK_NGX_Result_Success && fgBackbuffer)
                {
                    const auto resolved = DlssNr::NativeIdentity::Resolve<ID3D12Resource>(
                        static_cast<IUnknown*>(fgBackbuffer));
                    nativeBackbuffer = resolved.object.Get();
                }
                if (!nativeBackbuffer && DlssNr::PreFg::PendingCompletions())
                {
                    DlssNr::PreFg::RevokeReadiness();
                    NR_FRAME_TRACE("nr-fg-handoff-refused", "reason=missing-native-backbuffer handle={} list={:p}",
                        handleId, static_cast<void*>(InCmdList));
                    return NVSDK_NGX_Result_FAIL_InvalidParameter;
                }
                const auto provider = DlssNr::PreFg::Provider();
                const auto completion = DlssNr::PreFg::ClaimCompletion(nativeBackbuffer, provider.generation, handleId);
                bool handoffFailed = completion.result == DlssNr::PreFg::CompletionClaimResult::Refused;
                if (completion.result == DlssNr::PreFg::CompletionClaimResult::Ready &&
                    !DlssNr::GpuSafety::BindExternalWait(InCmdList, completion.dependency.fence.Get(),
                        completion.dependency.value, completion.dependency.token,
                        completion.dependency.sequence, completion.dependency.status))
                {
                    if (completion.dependency.status) completion.dependency.status->Fail();
                    DlssNr::PreFg::RevokeReadiness();
                    DlssNr::PreFg::RejectCompletion(nativeBackbuffer, provider.generation, handleId,
                        completion.dependency.token, completion.dependency.sequence);
                    handoffFailed = true;
                }
                if (handoffFailed)
                {
                    NR_FRAME_TRACE("nr-fg-handoff-refused", "reason={} handle={} list={:p} backbuffer={:p} "
                        "token={} sequence={}", completion.result == DlssNr::PreFg::CompletionClaimResult::Refused ?
                        "identity-mismatch" : "wait-bind-failed", handleId, static_cast<void*>(InCmdList),
                        static_cast<void*>(nativeBackbuffer), completion.dependency.token,
                        completion.dependency.sequence);
                    return NVSDK_NGX_Result_FAIL_PlatformError;
                }
            }

            NVSDK_NGX_Result result =
                NVNGXProxy::D3D12_EvaluateFeature()(InCmdList, InFeatureHandle, InParameters, InCallback);
            if (feature == NVSDK_NGX_Feature_FrameGeneration && result != NVSDK_NGX_Result_Success)
                DlssNr::PreFg::RevokeReadiness();
            NR_FRAME_TRACE("ngx-native-return", "handle={} feature={} result={} list={:p}", handleId,
                static_cast<unsigned int>(feature), static_cast<unsigned int>(result), static_cast<void*>(InCmdList));

            DlssNr::RestoreAfterUpscale(InParameters);

            LOG_DEBUG("Native DLSS EvaluateFeature result: 0x{:X}", (uint32_t) result);

            // Neural Rendering runs over what the upscaler just wrote, on the same list, so frame
            // generation interpolates from enhanced frames and the model still costs one run per
            // rendered frame. The feature check is the point: frame generation is handed depth and
            // motion vectors too, and its handle can reach here because the branch above does not
            // return, so filtering on the parameter block alone would run the model twice a frame.
            if (result == NVSDK_NGX_Result_Success && isNrPipelineFeature && nrSettings)
                DlssNr::EvaluateAfterUpscale(InCmdList, InParameters, nullptr, isRayReconstruction, &*nrSettings);

            return result;
        }

        LOG_DEBUG("Native DLSS EvaluateFeature not available for handle {}", handleId);
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    // DLSSG replacements passthrough
    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None && handleId >= NVNGX_PROVIDER_ID_OFFSET)
    {
        LOG_DEBUG("Passthrough to DLSSG Replacement's EvaluateFeature for handle {}", handleId);
        return Nvngx_FG::D3D12_EvaluateFeature(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    std::optional<float> cameraNear, cameraFar;
    {
        std::lock_guard lock(fgObservationMutex);
        cameraNear = lastDlssgCameraNear;
        cameraFar = lastDlssgCameraFar;
    }
    if (cameraNear) InParameters->Set("DLSSG.CameraNear", *cameraNear);
    if (cameraFar) InParameters->Set("DLSSG.CameraFar", *cameraFar);

    // OptiScaler internal handling
    const auto nrContext = Dx12Contexts.find(handleId);
    auto* nrFeature = nrContext != Dx12Contexts.end() ? nrContext->second.feature.get() : nullptr;
    const bool authoritativeNativePreSr = nrFeature && DlssNr::NativeTemporalInputs::Authoritative(
        isSuperResolution, nrFeature->GetUpscalerType() == Upscaler::DLSS,
        nrFeature->Api() == API::DX12, nrContext->second.feature->IsWithDx12());
    NR_FRAME_TRACE("nr-native-authority", "handle={} generation={} authoritative={}",
        handleId, featureSnapshot.generation, authoritativeNativePreSr);
    std::optional<DlssNr::NativeTemporalInputs::Metadata> nativeInputs;
    if (authoritativeNativePreSr)
        nativeInputs = {static_cast<unsigned>(nrFeature->GetFeatureFlags()),
                        nrFeature->DisplayWidth(), nrFeature->DisplayHeight(), handleId, featureSnapshot.generation};
    if (isSuperResolution && nrSettings)
        DlssNr::EvaluateBeforeUpscale(InCmdList, InParameters, nullptr, &*nrSettings, authoritativeNativePreSr,
                                     nativeInputs ? &*nativeInputs : nullptr);

    const NVSDK_NGX_Result optiResult =
        TryEvaluateOptiFeature(InCmdList, InFeatureHandle, InParameters, InCallback);
    NR_FRAME_TRACE("ngx-replacement-return", "handle={} feature={} result={} list={:p}", handleId,
        static_cast<unsigned int>(feature), static_cast<unsigned int>(optiResult), static_cast<void*>(InCmdList));

    DlssNr::RestoreAfterUpscale(InParameters);

    // Same pass, for OptiScaler's own upscalers rather than native DLSS.
    if (optiResult == NVSDK_NGX_Result_Success && isNrPipelineFeature && nrSettings)
        DlssNr::EvaluateAfterUpscale(InCmdList, InParameters, nullptr, isRayReconstruction, &*nrSettings,
                                    nativeInputs ? &*nativeInputs : nullptr);

    return optiResult;
}

#pragma endregion

#pragma region DLSS Buffer Size Call

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                                    const NVSDK_NGX_Parameter* InParameters,
                                                                    size_t* OutSizeInBytes)
{
    if (OutSizeInBytes == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None && InFeatureId == NVSDK_NGX_Feature_FrameGeneration)
    {
        return Nvngx_FG::D3D12_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
    }

    LOG_WARN("-> 52428800");
    *OutSizeInBytes = 52428800;
    return NVSDK_NGX_Result_Success;
}

#pragma endregion
