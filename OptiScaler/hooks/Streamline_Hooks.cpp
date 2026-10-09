#include <pch.h>
#include <mfg/ExperimentalMfgRuntime.h>
#include <dlssnr/VulkanPresentGuidesVk.h>
#include <nr/semantic/character/CharacterFgActivity.h>
// NR-FEED-001 BEGIN
#include <inputs/universal_feeder/providers/StreamlineObservationAdapter.h>
// NR-FEED-001 END
#include <dlssnr/FrameTrace.h>
#include <dlssnr/StreamlineSourceScope.h>
#include <dlssnr/VulkanNrStreamline.h>
#include <dlssnr/VulkanNrPresentFrame.h>
#include <dlssnr/DlssNrFeature_Vk.h>
#include <nr/diagnostics/RuntimeProvenance.h>
#include <intrin.h>
#include <magic_enum.hpp>

#include "Streamline_Hooks.h"
#include "Vulkan_Hooks.h"
#include <mfg/MfgControl.h>
#include <mfg/MfgAdaUnlock.h>
namespace {
namespace RP=Neurotic::Diagnostics::RuntimeProvenance;
std::atomic<HMODULE> nrObservedInterposer{nullptr};
std::atomic<uint64_t> nrMfgHookGeneration{1};
std::atomic<uint64_t> nrReplacementInputGeneration{1};
Neurotic::Mfg::MfgRequestJournal nrMfgRequests;
std::atomic<bool> nrFeatureHooks{false},nrDeviceHook{false},nrFgOverrides{false},nrFunctionHook{false};
void NrVkPresentMarker(sl::PCLMarker marker,const sl::FrameToken& frame,sl::Result result,uint64_t provider)
{
    const bool ok=result==sl::Result::eOk&&provider&&provider==DlssNr::VulkanNrStreamlineAdapter().Generation();
    auto& frames=DlssNr::VulkanPublicPresentFrames();
    if(marker==sl::PCLMarker::ePresentStart)frames.Start(provider,static_cast<uint32_t>(frame),GetCurrentThreadId(),ok);
    else if(marker==sl::PCLMarker::ePresentEnd)frames.End(provider,static_cast<uint32_t>(frame),GetCurrentThreadId(),ok);
}
bool NrReserveRuntimeEvent()
{
    static std::atomic<unsigned> budget{0};const auto n=budget.fetch_add(1);
    if(n<512)return true;
    if(n==512)LOG_WARN("NR_RUNTIME_PROVENANCE event_budget_exhausted coverage=incomplete");
    return false;
}
void NrRuntimeEvent(RP::Json event){LOG_INFO("NR_RUNTIME_PROVENANCE {}",event.dump());}
RP::Json NrPreferences(const sl::Preferences& p)
{
    RP::Json features=RP::Json::array(),paths=RP::Json::array();
    if(p.featuresToLoad)for(unsigned i=0;i<(std::min)(p.numFeaturesToLoad,64u);++i)features.push_back(p.featuresToLoad[i]);
    if(p.pathsToPlugins)for(unsigned i=0;i<(std::min)(p.numPathsToPlugins,64u);++i)
        paths.push_back(p.pathsToPlugins[i]?RP::Json(DlssNr::Canonical::Utf8(p.pathsToPlugins[i])):RP::Json(nullptr));
    return {{"features",features},{"feature_count",p.numFeaturesToLoad},{"plugin_paths",paths},{"path_count",p.numPathsToPlugins},
        {"truncated",p.numFeaturesToLoad>64||p.numPathsToPlugins>64},{"flags",static_cast<std::uint64_t>(p.flags)},
        {"render_api",static_cast<unsigned>(p.renderAPI)},{"engine",static_cast<unsigned>(p.engine)},
        {"application_id",p.applicationId},{"log_path",p.pathToLogsAndData?RP::Json(DlssNr::Canonical::Utf8(p.pathToLogsAndData)):RP::Json(nullptr)}};
}
template<class Call,class Detail> auto NrSlCall(const char* stage,const char* api,const void* caller,Call&& call,Detail&& detail)
{
    if(!RP::Enabled())return call();
    return RP::Call([&](RP::Json event){
        if(!NrReserveRuntimeEvent())return;
        event["caller"]=RP::Address(caller);
        const auto module=nrObservedInterposer.load();event["interposer"]=RP::Module(module);
        event["export"]=RP::Address(module?reinterpret_cast<const void*>(GetProcAddress(module,api)):nullptr);
        if(event["phase"]=="return")event["result_name"]=std::string(magic_enum::enum_name(static_cast<sl::Result>(event["result"].get<std::uint64_t>())));
        detail(event);NrRuntimeEvent(std::move(event));
    },stage,api,std::forward<Call>(call));
}
}
// NR-FEED-001 BEGIN
static Neurotic::Contracts::GraphicsApi FeedStreamlineApi(sl::RenderAPI api)
{
    using A = Neurotic::Contracts::GraphicsApi;
    return api == sl::RenderAPI::eD3D11 ? A::D3D11 : api == sl::RenderAPI::eD3D12 ? A::D3D12 :
        api == sl::RenderAPI::eVulkan ? A::Vulkan : A::Other;
}
// NR-FEED-001 END

namespace
{
thread_local StreamlineVkDiagnosticContext g_streamlineVkDiagnosticContext {};
}

StreamlineVkDiagnosticContext& GetStreamlineVkDiagnosticContext()
{
    return g_streamlineVkDiagnosticContext;
}

#include <Util.h>
#include <Config.h>

#include <nvapi/fakenvapi.h>
#include <misc/IdentifyGpu.h>
#include <hooks/Reflex_Hooks.h>
#include <menu/menu_overlay_base.h>
#include <framegen/nvngx/Nvngx_FG.h>
#include <proxies/KernelBase_Proxy.h>
#include <imgui/ImGuiNotify.hpp>

#include <json.hpp>
#include <sl1_reflex.h>
#include <magic_enum.hpp>
#include "detours/detours.h"
#include <dlssnr/StreamlinePreFg.h>
#include <mfg/MfgOptionsSnapshot.h>

static bool IsSL1AndDLSSGActive()
{
    return State::Instance().streamlineVersion.major == 1 && State::Instance().activeFgInput == FGInput::DLSSG &&
           (State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::XeFG);
}

static bool IsSL1AndFGActive()
{
    const auto& state = State::Instance();

    return state.streamlineVersion.major == 1 && state.activeFgInput == FGInput::DLSSG;
}

static void PatchSL1PluginJson(nlohmann::json& configJson)
{
    if (!IsSL1AndFGActive())
        return;

    LOG_DEBUG("Patching SL1 plugin JSON for external FG management");

    if (configJson.contains("/hooks"_json_pointer))
        configJson["hooks"].clear();

    if (configJson.contains("/exclusive_hooks"_json_pointer))
        configJson["exclusive_hooks"].clear();

    if (configJson.contains("/external/feature/tags"_json_pointer))
        configJson["external"]["feature"]["tags"].clear();

    if (configJson.contains("/vsync/supported"_json_pointer))
        configJson["vsync"]["supported"] = true;

    if (configJson.contains("/external/hws/required"_json_pointer))
        configJson["external"]["hws"]["required"] = false;
}

char* StreamlineHooks::trimStreamlineLog(const char* msg)
{
    char* result = (char*) malloc(strlen(msg) + 1);
    if (!result)
        return nullptr;

    strcpy(result, msg);

    size_t length = strlen(result);
    if (length > 0 && result[length - 1] == '\n')
    {
        result[length - 1] = '\0';
    }

    return result;
}

void StreamlineHooks::streamlineLogCallback(sl::LogType type, const char* msg)
{
    if (msg == nullptr)
        return;

    char* trimmed_msg = trimStreamlineLog(msg);
    if (trimmed_msg != nullptr)
    {
        switch (type)
        {
        case sl::LogType::eWarn:
            LOG_WARN("{}", trimmed_msg);
            break;
        case sl::LogType::eInfo:
            LOG_INFO("{}", trimmed_msg);
            break;
        case sl::LogType::eError:
            LOG_ERROR("{}", trimmed_msg);
            break;
        case sl::LogType::eCount:
            LOG_ERROR("{}", trimmed_msg);
            break;
        }

        free(trimmed_msg);
    }

    if (o_logCallback != nullptr)
        o_logCallback(type, msg);
}

sl::Result StreamlineHooks::hkslInit(const sl::Preferences& pref, uint64_t sdkVersion)
{
    LOG_FUNC();
    const auto caller=_ReturnAddress();

    sl::Preferences localPref = pref;

    if (localPref.logMessageCallback != &streamlineLogCallback)
        o_logCallback = localPref.logMessageCallback;
    localPref.logLevel = sl::LogLevel::eCount;
    localPref.logMessageCallback = &streamlineLogCallback;

    // renderAPI is optional so need to be careful, should only matter for Vulkan
    renderApi = localPref.renderAPI;

    State::Instance().slFGInputs.reportEngineType(localPref.engine);

    // Treat engine type set in Streamline as ground truth
    if (localPref.engine == sl::EngineType::eUnreal)
        State::Instance().gameQuirks |= GameQuirk::ForceUnrealEngine;

    std::filesystem::path localSlPath(Config::Instance()->MainDllPath.value());
    localSlPath = localSlPath / L"streamline"; // Hardcoded streamline folder

    auto localSlPathStr = localSlPath.wstring();

    std::vector<const wchar_t*> storage;

    // Replace the SL files to allow for MFG
    if (State::Instance().activeFgInput == FGInput::NvngxFG && std::filesystem::exists(localSlPath / L"sl.common.dll"))
    {
        storage.assign(localPref.pathsToPlugins, localPref.pathsToPlugins + localPref.numPathsToPlugins);

        std::filesystem::path pluginsDir;

        // Find the first path that contains sl.common.dll
        // If storage is empty, look in the exe folder. pathsToPlugins is an optional field
        if (storage.empty())
        {
            std::filesystem::path exeFolder = Util::ExePath().parent_path();
            if (std::filesystem::exists(exeFolder / L"sl.common.dll"))
            {
                pluginsDir = exeFolder;
            }
        }
        else
        {
            for (const wchar_t* pathStr : storage)
            {
                if (!pathStr)
                    continue;

                std::filesystem::path p = pathStr;
                if (std::filesystem::exists(p / L"sl.common.dll"))
                {
                    pluginsDir = p;
                    break;
                }
            }
        }

        std::vector<std::string> missingDlls;
        bool hasNewerPlugin = false;

        // If we found the plugins folder, scan its contents
        if (!pluginsDir.empty() && std::filesystem::exists(pluginsDir))
        {
            for (const auto& entry : std::filesystem::directory_iterator(pluginsDir))
            {
                if (!entry.is_regular_file())
                    continue;

                std::wstring filename = entry.path().filename().wstring();

                std::wstring lowerName = filename;
                to_lower_in_place(lowerName);

                // Skip interposer
                if (lowerName == L"sl.interposer.dll")
                    continue;

                const bool isSlDll = lowerName.starts_with(L"sl.") && lowerName.ends_with(L".dll");
                const bool isNvLowLatency = lowerName == L"nvlowlatencyvk.dll";

                if (isSlDll || isNvLowLatency)
                {
                    std::filesystem::path localDllPath = localSlPath / filename;

                    // Check if localSlPath also has this DLL
                    if (!std::filesystem::exists(localDllPath))
                    {
                        missingDlls.push_back(entry.path().filename().string());
                    }
                    else
                    {
                        // Compare versions
                        version_t pluginVer, pluginProdVer;
                        version_t localVer, localProdVer;

                        bool gotPluginVer = Util::GetFileVersion(entry.path().wstring(), &pluginVer, &pluginProdVer);
                        bool gotLocalVer = Util::GetFileVersion(localDllPath.wstring(), &localVer, &localProdVer);

                        if (gotPluginVer && gotLocalVer)
                        {
                            if (localVer > pluginVer)
                            {
                                hasNewerPlugin = true;
                            }
                        }
                    }
                }
            }
        }

        // Insert local path only if a newer plugin was found
        if (hasNewerPlugin)
        {
            LOG_DEBUG("Making the game use local streamline files");

            storage.insert(storage.begin(), localSlPathStr.c_str());
            localPref.pathsToPlugins = storage.data();
            localPref.numPathsToPlugins = (uint32_t) storage.size();

            if (!missingDlls.empty())
            {
                std::string toastMsg = "You are missing the following dlls from the streamline folder:\n";
                for (const auto& missingDll : missingDlls)
                {
                    toastMsg += "- " + missingDll + "\n";
                }

                ImGui::InsertNotification({ ImGuiToastType::Warning, 20000, toastMsg.c_str() });
            }
        }
    }

    if (State::Instance().activeFgInput == FGInput::DLSSG || State::Instance().activeFgOutput == FGOutput::DLSSG)
    {
        std::vector<sl::Feature> localFeaturesToLoad(pref.featuresToLoad, pref.featuresToLoad + pref.numFeaturesToLoad);
        std::erase(localFeaturesToLoad, sl::kFeatureDLSS_G);

        localPref.featuresToLoad = localFeaturesToLoad.data();
        localPref.numFeaturesToLoad = localFeaturesToLoad.size();

        // return so that localFeaturesToLoad is valid
        return NrSlCall("L2","slInit",caller,[&]{auto r=o_slInit(localPref,sdkVersion); if(r==sl::Result::eOk)++nrReplacementInputGeneration; if(localPref.renderAPI==sl::RenderAPI::eVulkan&&r==sl::Result::eOk){static std::atomic<uint64_t> generation{0x1000000000000000ull};DlssNr::RevokeVulkanPresentTags();DlssNr::VulkanNrStreamlineAdapter().Provider(++generation,true);DlssNr::VulkanNrStreamlineAdapter().LookupOutput([](VkCommandBuffer cb){auto output=DlssNr::SelectedVkNrOutput(cb);return output&&DlssNr::VkNrStreamlineSourceScope::SourceSucceeded(output->use)?output:std::nullopt;});DlssNr::VulkanNrStreamlineAdapter().PrepareOutput(DlssNr::PrepareVkNrFinalColor);} return r;},[&](RP::Json& event){
            event["sdk_version"]=sdkVersion;event["requested"]=NrPreferences(pref);event["effective"]=NrPreferences(localPref);
            event["process"]=RP::Process();event["modules"]=RP::LoadedModules();});
    }

    // bool hookSetTag =
    //     (State::Instance().activeFgInput == FGInput::NvngxFG || State::Instance().activeFgInput == FGInput::DLSSG);

    // if (hookSetTag)
    //     localPref->flags &= ~(sl::PreferenceFlags::eAllowOTA | sl::PreferenceFlags::eLoadDownloadedPlugins);

    // To prevent mixed up OTA situations
    // if (State::Instance().activeFgOutput == FGOutput::DLSSG)
    //{
    //    localPref.flags &= ~sl::PreferenceFlags::eAllowOTA;
    //    localPref.flags &= ~sl::PreferenceFlags::eLoadDownloadedPlugins;
    //}

    return NrSlCall("L2","slInit",caller,[&]{auto r=o_slInit(localPref,sdkVersion); if(r==sl::Result::eOk)++nrReplacementInputGeneration; if(localPref.renderAPI==sl::RenderAPI::eVulkan&&r==sl::Result::eOk){static std::atomic<uint64_t> generation{0x1000000000000000ull};DlssNr::RevokeVulkanPresentTags();DlssNr::VulkanNrStreamlineAdapter().Provider(++generation,true);DlssNr::VulkanNrStreamlineAdapter().LookupOutput([](VkCommandBuffer cb){auto output=DlssNr::SelectedVkNrOutput(cb);return output&&DlssNr::VkNrStreamlineSourceScope::SourceSucceeded(output->use)?output:std::nullopt;});DlssNr::VulkanNrStreamlineAdapter().PrepareOutput(DlssNr::PrepareVkNrFinalColor);} return r;},[&](RP::Json& event){
        event["sdk_version"]=sdkVersion;event["requested"]=NrPreferences(pref);event["effective"]=NrPreferences(localPref);
        event["process"]=RP::Process();event["modules"]=RP::LoadedModules();});
}

sl::Result StreamlineHooks::hkslIsFeatureSupported(sl::Feature feature, const sl::AdapterInfo& adapterInfo)
{
    if (nrFgOverrides && renderApi != sl::RenderAPI::eVulkan && feature == sl::kFeatureDLSS_G)
        return sl::Result::eOk;

    const bool experimentalLane = experimentalFgLane(feature == sl::kFeatureDLSS_G);
    Neurotic::Mfg::Experimental::GameFgScope gameFgScope(experimentalLane);
    if (experimentalLane) Neurotic::Mfg::Experimental::PrepareAtBoundary(true);
    LUID scopedAdapter{};
    if(adapterInfo.deviceLUID && adapterInfo.deviceLUIDSizeInBytes==sizeof(LUID))
        memcpy(&scopedAdapter,adapterInfo.deviceLUID,sizeof(scopedAdapter));
    Neurotic::Mfg::Experimental::ArchitectureScope experimentalScope(experimentalLane,scopedAdapter);
    const auto supportResult = NrSlCall("L3","slIsFeatureSupported",_ReturnAddress(),[&]{return o_slIsFeatureSupported(feature,adapterInfo);},[&](RP::Json& event){
        event["feature"]=feature;event["adapter_luid"]=nullptr;
        if(adapterInfo.deviceLUID&&adapterInfo.deviceLUIDSizeInBytes==sizeof(LUID))
        {LUID id{};memcpy(&id,adapterInfo.deviceLUID,sizeof(id));event["adapter_luid"]={{"high",id.HighPart},{"low",id.LowPart}};}
        event["vk_physical_device"]=reinterpret_cast<std::uintptr_t>(adapterInfo.vkPhysicalDevice);});
    if (experimentalLane && adapterInfo.deviceLUID && adapterInfo.deviceLUIDSizeInBytes == sizeof(LUID)) {
        LUID luid{}; memcpy(&luid, adapterInfo.deviceLUID, sizeof(luid));
        if (Neurotic::Mfg::Experimental::RelaxStreamline(static_cast<int>(supportResult), luid)) return sl::Result::eOk;
    }
    return supportResult;
}

sl::Result StreamlineHooks::hkslIsFeatureLoaded(sl::Feature feature, bool& loaded)
{
    if (nrFgOverrides && renderApi != sl::RenderAPI::eVulkan && feature == sl::kFeatureDLSS_G)
    {
        loaded = true;
        return sl::Result::eOk;
    }

    const auto result = NrSlCall("L2","slIsFeatureLoaded",_ReturnAddress(),[&]{return o_slIsFeatureLoaded(feature,loaded);},[&](RP::Json& e){
        e["feature"]=feature;if(e["phase"]=="return"&&e["result"]==0)e["loaded"]=loaded;});
    if (renderApi == sl::RenderAPI::eVulkan && feature == sl::kFeatureDLSS_G)
        DlssNr::VulkanNrStreamlineAdapter().FeatureLoaded(result == sl::Result::eOk && loaded, result == sl::Result::eOk);
    return result;
}

sl::Result StreamlineHooks::hkslSetFeatureLoaded(sl::Feature feature, bool loaded)
{
    const auto result = o_slSetFeatureLoaded(feature, loaded);
    if(renderApi!=sl::RenderAPI::eVulkan&&feature==sl::kFeatureDLSS_G&&result==sl::Result::eOk&&!loaded)
        Neurotic::Semantic::Character::NativeFgWork().Reset();
    if (renderApi == sl::RenderAPI::eVulkan && feature == sl::kFeatureDLSS_G)
    {
        DlssNr::VulkanNrStreamlineAdapter().FeatureLoaded(loaded, result == sl::Result::eOk);
        LOG_INFO("Vulkan FG feature load observation: loaded={} result={} activity={}", loaded,
            static_cast<int>(result), static_cast<unsigned>(DlssNr::VulkanNrStreamlineAdapter().Activity()));
    }
    return result;
}

sl::Result StreamlineHooks::hkslGetFeatureRequirements(sl::Feature feature, sl::FeatureRequirements& requirements)
{
    if (nrFgOverrides && renderApi != sl::RenderAPI::eVulkan && feature == sl::kFeatureDLSS_G)
        return sl::Result::eOk;

    return NrSlCall("L3","slGetFeatureRequirements",_ReturnAddress(),[&]{return o_slGetFeatureRequirements(feature,requirements);},
        [&](RP::Json& e){e["feature"]=feature;});
}

sl::Result StreamlineHooks::hkslGetFeatureVersion(sl::Feature feature, sl::FeatureVersion& version)
{
    if (nrFgOverrides && renderApi != sl::RenderAPI::eVulkan && feature == sl::kFeatureDLSS_G)
    {
        version.versionSL = { State::Instance().streamlineVersion.major, State::Instance().streamlineVersion.minor,
                              State::Instance().streamlineVersion.patch };
        version.versionNGX = { 4, 2, 0 };

        return sl::Result::eOk;
    }

    return o_slGetFeatureVersion(feature, version);
}

static sl::Result dummy_slDLSSGGetState(const sl::ViewportHandle& viewport, sl::DLSSGState& state,
                                        const sl::DLSSGOptions* options)
{
    state.numFramesActuallyPresented = 1; // TODO: can do better
    state.numFramesToGenerateMax = 1;
    state.bIsVsyncSupportAvailable = sl::Boolean::eTrue;
    state.estimatedVRAMUsageInBytes = 300 * 1024 * 1024;

    return sl::Result::eOk;
}

static sl::Result dummy_slDLSSGSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options)
{
    return sl::Result::eOk;
}

sl::Result StreamlineHooks::hkslGetFeatureFunction(sl::Feature feature, const char* functionName, void*& function)
{
    if (nrFgOverrides && renderApi != sl::RenderAPI::eVulkan && feature == sl::kFeatureDLSS_G)
    {
        if (strcmp(functionName, "slDLSSGSetOptions") == 0)
        {
            function = &dummy_slDLSSGSetOptions;

            return sl::Result::eOk;
        }

        if (strcmp(functionName, "slDLSSGGetState") == 0)
        {
            function = &dummy_slDLSSGGetState;

            return sl::Result::eOk;
        }
    }

    const auto result = NrSlCall("L5","slGetFeatureFunction",_ReturnAddress(),[&]{return o_slGetFeatureFunction(feature,functionName,function);},[&](RP::Json& e){
        e["feature"]=feature;e["function_name"]=functionName?functionName:"UNKNOWN";
        if(e["phase"]=="return"&&e["result"]==0)e["resolved_function"]=RP::Address(function);});
    if (result == sl::Result::eOk && feature == sl::kFeatureDLSS_G && functionName)
    {
        // Steam may detour the original entry points. Preserve its existing
        // exemption at the final feature resolver as well as the plugin one.
        const auto steamOverlay = KernelBaseProxy::GetModuleHandleA_()("gameoverlayrenderer64.dll");
        if (steamOverlay && Util::GetCallerModule(_ReturnAddress()) == steamOverlay)
            return result;
        wrapNativeDlssgFunction(functionName, function);
    }
    return result;
}

sl::Result StreamlineHooks::hkslSetTag(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags,
                                       uint32_t numTags, sl::CommandBuffer* cmdBuffer)
{
    const auto replacementOwner = Sl_Inputs_Dx12::CaptureOwner();
    const auto replacementProvider = nrReplacementInputGeneration.load();
    // NR-FEED-001 BEGIN
    if (Neurotic::Feed::Observing())
        for (uint32_t i = 0; tags && i < numTags && i < 32; ++i)
        {
            Neurotic::Feed::Callback feedTag({"Streamline", FeedStreamlineApi(renderApi), "tag"}, &viewport);
            feedTag.Value("provider.viewport", static_cast<uint32_t>(viewport));
            feedTag.Value("provider.tagCount", numTags);
            Neurotic::Feed::ObserveStreamlineTag(feedTag, tags[i]);
        }
    // NR-FEED-001 END
    if (renderApi == sl::RenderAPI::eD3D11 || renderApi == sl::RenderAPI::eVulkan)
    {
        if(renderApi==sl::RenderAPI::eD3D11)LOG_ERROR("hkslSetTag only supports DX12"); // Legacy tags lack a public frame identity.
        return o_slSetTag(viewport, tags, numTags, cmdBuffer);
    }

    if (renderApi == sl::RenderAPI::eCount)
        LOG_WARN("Incomplete Streamline hooks");

    if (tags == nullptr)
    {
        const auto result = o_slSetTag(viewport, tags, numTags, cmdBuffer);
        State::Instance().slFGInputs.acceptedTags(nullptr, 0, nullptr, 0, (uint32_t)viewport,
            replacementProvider, replacementOwner, result == sl::Result::eOk);
        return result;
    }

    if (State::Instance().activeFgInput == FGInput::DLSSG &&
        State::Instance().gameQuirks[GameQuirk::IgnoreTagsWithoutHudlessForFG])
    {
        bool hasDepth = false;
        bool hasMVs = false;
        bool hasHudless = false;

        for (uint32_t i = 0; i < numTags; i++)
        {
            if (tags[i].resource == nullptr || tags[i].resource->native == nullptr)
                continue;

            if (tags[i].type == sl::kBufferTypeDepth)
                hasDepth = true;

            if (tags[i].type == sl::kBufferTypeMotionVectors)
                hasMVs = true;

            if (tags[i].type == sl::kBufferTypeHUDLessColor)
                hasHudless = true;
        }

        // Try to skip a DLSS call
        if (hasDepth && hasMVs && !hasHudless)
        {
            LOG_DEBUG("Skipping the FG tagging of potential DLSS resources");
            return o_slSetTag(viewport, tags, numTags, cmdBuffer);
        }
    }

    for (uint32_t i = 0; i < numTags; i++)
    {
        const auto typeEnum = (BufferType) tags[i].type;

        if (tags[i].resource == nullptr || tags[i].resource->native == nullptr)
        {
            LOG_TRACE("Resource of type: {} is null, continuing", magic_enum::enum_name(typeEnum));
            continue;
        }

        // Cyberpunk hudless state fix for RDNA 2
        if (State::Instance().gameQuirks & GameQuirk::CyberpunkHudlessState &&
            tags[i].resource->state ==
                (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) &&
            tags[i].type == sl::kBufferTypeHUDLessColor)
        {
            tags[i].resource->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            LOG_TRACE("Changing hudless resource state");
        }

        if (State::Instance().activeFgInput == FGInput::DLSSG &&
            (tags[i].type == sl::kBufferTypeHUDLessColor || tags[i].type == sl::kBufferTypeDepth ||
             tags[i].type == sl::kBufferTypeHiResDepth || tags[i].type == sl::kBufferTypeLinearDepth ||
             tags[i].type == sl::kBufferTypeMotionVectors || tags[i].type == sl::kBufferTypeUIColorAndAlpha ||
             tags[i].type == sl::kBufferTypeBidirectionalDistortionField))
        {
        }
        else if (State::Instance().activeFgInput == FGInput::NvngxFG)
        {
            LOG_TRACE("Tagging resource of type: {}", magic_enum::enum_name(typeEnum));
        }
    }

    auto result = o_slSetTag(viewport, tags, numTags, cmdBuffer);
    if (State::Instance().activeFgInput == FGInput::DLSSG && replacementProvider == nrReplacementInputGeneration.load())
        State::Instance().slFGInputs.acceptedTags(tags, numTags, (ID3D12GraphicsCommandList*)cmdBuffer, 0,
            (uint32_t)viewport, replacementProvider, replacementOwner, result == sl::Result::eOk);
    return result;
}

sl::Result StreamlineHooks::hkslSetTagForFrame(const sl::FrameToken& frame, const sl::ViewportHandle& viewport,
                                               const sl::ResourceTag* resources, uint32_t numResources,
                                               sl::CommandBuffer* cmdBuffer)
{
    const auto replacementOwner = Sl_Inputs_Dx12::CaptureOwner();
    const auto replacementProvider = nrReplacementInputGeneration.load();
    // NR-FEED-001 BEGIN
    if (Neurotic::Feed::Observing())
        for (uint32_t i = 0; resources && i < numResources && i < 32; ++i)
        {
            Neurotic::Feed::Callback feedTag({"Streamline", FeedStreamlineApi(renderApi), "tag-for-frame"}, &viewport);
            feedTag.Value("provider.frame", static_cast<uint32_t>(frame));
            feedTag.Value("provider.viewport", static_cast<uint32_t>(viewport));
            feedTag.Value("provider.tagCount", numResources);
            Neurotic::Feed::ObserveStreamlineTag(feedTag, resources[i]);
        }
    // NR-FEED-001 END
    if (DlssNr::FrameTrace::Armed())
    {
        NR_FRAME_TRACE("sl-tags-enter", "provider=game-streamline frame={} viewport={} list={:p} count={} api={}",
            static_cast<uint32_t>(frame), static_cast<uint32_t>(viewport), static_cast<void*>(cmdBuffer),
            numResources, static_cast<unsigned int>(renderApi));
        // Cap the observation batch; its declared count above makes omissions visible.
        for (uint32_t i = 0; resources && i < numResources && i < 32; ++i)
        {
            const auto& tag = resources[i];
            NR_FRAME_TRACE("sl-tag-input", "frame={} viewport={} index={} type={} resource={:p} list={:p} "
                "state={} lifecycle={} x={} y={} width={} height={}", static_cast<uint32_t>(frame),
                static_cast<uint32_t>(viewport), i, static_cast<unsigned int>(tag.type),
                tag.resource ? tag.resource->native : nullptr, static_cast<void*>(cmdBuffer),
                tag.resource ? tag.resource->state : 0, static_cast<unsigned int>(tag.lifecycle),
                tag.extent.left, tag.extent.top, tag.extent.width, tag.extent.height);
        }
    }
    if(renderApi==sl::RenderAPI::eVulkan){
        auto& adapter=DlssNr::VulkanNrStreamlineAdapter();
        const auto tagProvider=adapter.Generation();
        auto d=adapter.BeforeTags(&frame,viewport,resources?std::span<const sl::ResourceTag>(resources,numResources):std::span<const sl::ResourceTag>{},cmdBuffer);
        const auto result=o_slSetTagForFrame(frame,viewport,d.accepted?d.tags.data():resources,d.accepted?static_cast<uint32_t>(d.tags.size()):numResources,cmdBuffer);
        adapter.TagsReturned(d,result);
        if(tagProvider==adapter.Generation())DlssNr::ObserveVulkanPresentTags(tagProvider,static_cast<uint32_t>(frame),static_cast<uint32_t>(viewport),resources,numResources,result==sl::Result::eOk);
        // Observe the executable public boundary even when replacement FG and
        // the optional detailed frame trace are disabled. Bound per-thread output.
        static thread_local uint64_t calls=0,reports=0;
        static thread_local std::string lastReason;
        const auto n=++calls;
        if(reports<64&&(n<=4||d.reason!=lastReason||n%1024==0)){
            ++reports;lastReason=d.reason;
            LOG_INFO("Vulkan public FG tag: provider={} frame={} viewport={} command={} count={} accepted={} result={} reason=[{}] calls={}",
                tagProvider,static_cast<uint32_t>(frame),static_cast<uint32_t>(viewport),cmdBuffer!=nullptr,
                numResources,d.accepted,static_cast<int>(result),d.reason,n);
        }
        return result;
    }
    // This public observer is installed for native Vulkan as well as replacement
    // FG. Native DirectX tags retain their unmodified forwarding path.
    if(State::Instance().activeFgInput!=FGInput::NvngxFG&&State::Instance().activeFgInput!=FGInput::DLSSG)
        return o_slSetTagForFrame(frame,viewport,resources,numResources,cmdBuffer);
    if (renderApi == sl::RenderAPI::eD3D11)
    {
        LOG_ERROR("hkslSetTagForFrame only supports DX12");
        return o_slSetTagForFrame(frame, viewport, resources, numResources, cmdBuffer);
    }

    if (renderApi == sl::RenderAPI::eCount)
        LOG_WARN("Incomplete Streamline hooks");

    if (resources == nullptr)
    {
        const auto result = o_slSetTagForFrame(frame, viewport, resources, numResources, cmdBuffer);
        State::Instance().slFGInputs.acceptedTags(nullptr, 0, nullptr, (uint32_t)frame, (uint32_t)viewport,
            replacementProvider, replacementOwner, result == sl::Result::eOk);
        return result;
    }

    LOG_DEBUG("frameIndex: {}", static_cast<uint32_t>(frame));

    if (State::Instance().activeFgInput == FGInput::DLSSG &&
        State::Instance().gameQuirks[GameQuirk::IgnoreTagsWithoutHudlessForFG])
    {
        bool hasDepth = false;
        bool hasMVs = false;
        bool hasHudless = false;

        for (uint32_t i = 0; i < numResources; i++)
        {
            if (resources[i].resource == nullptr || resources[i].resource->native == nullptr)
                continue;

            if (resources[i].type == sl::kBufferTypeDepth)
                hasDepth = true;

            if (resources[i].type == sl::kBufferTypeMotionVectors)
                hasMVs = true;

            if (resources[i].type == sl::kBufferTypeHUDLessColor)
                hasHudless = true;
        }

        // Try to skip a DLSS call
        if (hasDepth && hasMVs && !hasHudless)
        {
            LOG_DEBUG("Skipping the FG tagging of potential DLSS resources");
            return o_slSetTagForFrame(frame, viewport, resources, numResources, cmdBuffer);
        }
    }

    for (uint32_t i = 0; i < numResources; i++)
    {
        const auto typeEnum = (BufferType) resources[i].type;

        if (resources[i].resource == nullptr || resources[i].resource->native == nullptr)
        {
            LOG_TRACE("Resource of type: {} is null, continuing", magic_enum::enum_name(typeEnum));
            continue;
        }

        if (State::Instance().activeFgInput == FGInput::DLSSG &&
            (resources[i].type == sl::kBufferTypeHUDLessColor || resources[i].type == sl::kBufferTypeDepth ||
             resources[i].type == sl::kBufferTypeHiResDepth || resources[i].type == sl::kBufferTypeLinearDepth ||
             resources[i].type == sl::kBufferTypeMotionVectors || resources[i].type == sl::kBufferTypeUIColorAndAlpha ||
             resources[i].type == sl::kBufferTypeBidirectionalDistortionField))
        {
        }
        else if (State::Instance().activeFgInput == FGInput::NvngxFG)
        {
            LOG_TRACE("Tagging resource of type: {}", magic_enum::enum_name(typeEnum));
        }
    }

    auto result = o_slSetTagForFrame(frame, viewport, resources, numResources, cmdBuffer);
    if (State::Instance().activeFgInput == FGInput::DLSSG && replacementProvider == nrReplacementInputGeneration.load())
        State::Instance().slFGInputs.acceptedTags(resources, numResources, (ID3D12GraphicsCommandList*)cmdBuffer, (uint32_t)frame,
            (uint32_t)viewport, replacementProvider, replacementOwner, result == sl::Result::eOk);
    NR_FRAME_TRACE("sl-tags-return", "frame={} viewport={} result={}", static_cast<uint32_t>(frame),
        static_cast<uint32_t>(viewport), static_cast<unsigned int>(result));
    return result;
}

sl::Result StreamlineHooks::hkslEvaluateFeature(sl::Feature feature, const sl::FrameToken& frame,
                                                const sl::BaseStructure** inputs, uint32_t numInputs,
                                                sl::CommandBuffer* cmdBuffer)
{
    // Install an empty barrier before virtual token conversion or any callbacks.
    // Unsupported/nested evaluations cannot inherit an enclosing SL observation.
    DlssNr::StreamlineSourceScope sourceFrame;
    DlssNr::PreFg::NativeFrameScope nativeGuideFrame;
    Neurotic::Mfg::Experimental::GameFgScope gameFgScope(experimentalFgLane(feature == sl::kFeatureDLSS_G));
    if (feature == sl::kFeatureDLSS_G && !Neurotic::Mfg::Experimental::AllowFeatureCall())
        return sl::Result::eErrorFeatureNotSupported;
    const auto sourceFrameIndex=static_cast<uint32_t>(frame);
    nativeGuideFrame.Configure(renderApi == sl::RenderAPI::eD3D12 &&
        (feature == sl::kFeatureDLSS || feature == sl::kFeatureDLSS_RR) &&
        frame.structVersion == sl::kStructVersion1, sourceFrameIndex,
        reinterpret_cast<ID3D12GraphicsCommandList*>(cmdBuffer));
    // NR-FEED-001 BEGIN
    Neurotic::Feed::Callback feedEvaluate({"Streamline", FeedStreamlineApi(renderApi), "evaluate"}, &frame);
    feedEvaluate.Value("provider.frame", sourceFrameIndex);feedEvaluate.Value("provider.feature", feature);
    feedEvaluate.Value("provider.inputCount", numInputs);
    // NR-FEED-001 END
    LOG_DEBUG("frameIndex: {}", sourceFrameIndex);

    auto& diagnostic = GetStreamlineVkDiagnosticContext();
    const auto saved = diagnostic;
    diagnostic.feature = static_cast<uint32_t>(feature);
    diagnostic.frame = sourceFrameIndex;
    diagnostic.commandBuffer = reinterpret_cast<uintptr_t>(cmdBuffer);
    diagnostic.viewport = UINT32_MAX;
    diagnostic.active = feature == sl::kFeatureDLSS_RR;
    if (diagnostic.active && inputs != nullptr)
    {
        for (uint32_t i = 0; i < numInputs; ++i)
        {
            if (inputs[i] != nullptr && inputs[i]->structType == sl::ViewportHandle::s_structType)
            {
                diagnostic.viewport = static_cast<uint32_t>(*static_cast<const sl::ViewportHandle*>(inputs[i]));
                break;
            }
        }
    }
    if (renderApi==sl::RenderAPI::eD3D12 && State::Instance().activeFgInput == FGInput::DLSSG && numInputs > 0 && inputs != nullptr)
    {
        for (uint32_t i = 0; i < numInputs; i++)
        {
            if (inputs[i] == nullptr)
                continue;

            if (inputs[i]->structType == sl::ResourceTag::s_structType)
            {
                auto tag = (const sl::ResourceTag*) inputs[i];

                if (tag->type == sl::kBufferTypeHUDLessColor || tag->type == sl::kBufferTypeDepth ||
                    tag->type == sl::kBufferTypeHiResDepth || tag->type == sl::kBufferTypeLinearDepth ||
                    tag->type == sl::kBufferTypeMotionVectors || tag->type == sl::kBufferTypeUIColorAndAlpha ||
                    tag->type == sl::kBufferTypeBidirectionalDistortionField)
                {
                    // Feature-local evaluation tags are not global FG inputs.
                }
            }
        }
    }

    std::optional<uint32_t> sourceViewport;
    bool uniqueViewport=true;
    const bool selectedSource=renderApi==sl::RenderAPI::eD3D12 && feature==sl::kFeatureDLSS &&
        (Config::Instance()->DlssNrNativeProtocol.value_or_default() ||
         Config::Instance()->DlssNrAlternateFrame.value_or_default()) && frame.structVersion==sl::kStructVersion1;
    if(selectedSource && inputs && numInputs>0 && numInputs<=16)
        for(uint32_t i=0;i<numInputs;++i)
            if(inputs[i] && inputs[i]->structType==sl::ViewportHandle::s_structType)
            {
                if(sourceViewport || inputs[i]->structVersion!=sl::kStructVersion1)
                {uniqueViewport=false;break;}
                sourceViewport=static_cast<uint32_t>(*static_cast<const sl::ViewportHandle*>(inputs[i]));
            }
    // Values come from this public invocation; no address/counter or inferred
    // viewport is an identity. A token changed during forwarding setup is unknown.
    sourceFrame.Configure(selectedSource && uniqueViewport && static_cast<uint32_t>(frame)==sourceFrameIndex,
        sourceFrameIndex,sourceViewport,reinterpret_cast<ID3D12GraphicsCommandList*>(cmdBuffer));
    std::optional<DlssNr::VkNrPublicFrame> vkPublicFrame;
    if(renderApi==sl::RenderAPI::eVulkan&&(feature==sl::kFeatureDLSS||feature==sl::kFeatureDLSS_RR)&&frame.structVersion==1&&cmdBuffer&&inputs&&numInputs<=16){
        std::optional<uint32_t> viewport;bool unique=true;
        for(uint32_t i=0;i<numInputs;++i)if(inputs[i]&&inputs[i]->structType==sl::ViewportHandle::s_structType){
            if(viewport||inputs[i]->structVersion!=1){unique=false;break;}viewport=static_cast<uint32_t>(*static_cast<const sl::ViewportHandle*>(inputs[i]));}
        const auto generation=DlssNr::VulkanNrStreamlineAdapter().Generation();
        if(unique&&viewport&&generation&&static_cast<uint32_t>(frame)==sourceFrameIndex)vkPublicFrame=DlssNr::VkNrPublicFrame{generation,sourceFrameIndex,*viewport,reinterpret_cast<VkCommandBuffer>(cmdBuffer)};
    }
    DlssNr::VkNrStreamlineSourceScope vkSource(vkPublicFrame);
    auto result = o_slEvaluateFeature(feature, frame, inputs, numInputs, cmdBuffer);
    if (feature == sl::kFeatureDLSS_G && renderApi == sl::RenderAPI::eD3D12)
    {
        Neurotic::Mfg::Experimental::ObserveEvaluation(result == sl::Result::eOk);
        if (result != sl::Result::eOk && cmdBuffer && Neurotic::Mfg::Experimental::Requested())
        {
            ID3D12Device* device = nullptr;
            if (SUCCEEDED(static_cast<ID3D12GraphicsCommandList*>(cmdBuffer)->GetDevice(IID_PPV_ARGS(&device))))
            {
                if (FAILED(device->GetDeviceRemovedReason())) Neurotic::Mfg::Experimental::DeviceRemoved();
                device->Release();
            }
        }
    }
    vkSource.Complete(result==sl::Result::eOk);
    sourceFrame.Complete(static_cast<uint32_t>(result),result==sl::Result::eOk);
    diagnostic = saved;
    return result;
}

sl::Result StreamlineHooks::hkslAllocateResources(sl::CommandBuffer* cmdBuffer, sl::Feature feature,
                                                  const sl::ViewportHandle& viewport)
{
    LOG_FUNC();
    Neurotic::Mfg::Experimental::GameFgScope gameFgScope(experimentalFgLane(feature == sl::kFeatureDLSS_G));
    if (feature == sl::kFeatureDLSS_G) {
        prepareExperimentalMfgCapabilities();
        if (!Neurotic::Mfg::Experimental::AllowFeatureCall()) return sl::Result::eErrorFeatureNotSupported;
    }
    auto result = o_slAllocateResources(cmdBuffer, feature, viewport);
    return result;
}

sl::Result StreamlineHooks::hkslGetNativeInterface(void* proxyInterface, void** baseInterface)
{
    LOG_FUNC();
    auto result = o_slGetNativeInterface(proxyInterface, baseInterface);
    return result;
}

sl::Result StreamlineHooks::hkslSetD3DDevice(void* d3dDevice)
{
    LOG_FUNC();
    // slSetD3DDevice starts the plugins and queries NGX capabilities inside
    // this call. The adapter must be observed before that startup begins.
    if (renderApi == sl::RenderAPI::eD3D12 && d3dDevice)
    {
        ID3D12Device* nativeDevice = nullptr;
        if (SUCCEEDED(static_cast<IUnknown*>(d3dDevice)->QueryInterface(IID_PPV_ARGS(&nativeDevice))))
        {
            Neurotic::Mfg::ObserveAdaD3D12Adapter(nativeDevice->GetAdapterLuid());
            Neurotic::Mfg::Experimental::ObserveDevice(nativeDevice);
            nativeDevice->Release();
        }
    }
    prepareExperimentalMfgCapabilities();
    const auto result = NrSlCall("L4","slSetD3DDevice",_ReturnAddress(),[&]{return o_slSetD3DDevice(d3dDevice);},
        [&](RP::Json& e){e["device_identity"]=reinterpret_cast<std::uintptr_t>(d3dDevice);});
    return result;
}

bool StreamlineHooks::experimentalFgLane(bool fg) noexcept
{
    const auto& state = State::Instance();
    return fg && renderApi == sl::RenderAPI::eD3D12 && state.activeFgInput == FGInput::NoFG &&
        state.activeFgOutput == FGOutput::NoFG && state.activeFgNvngx == FGNvngxReplacement::None;
}

bool StreamlineHooks::prepareExperimentalMfgCapabilities() noexcept
{
    if (!Neurotic::Mfg::Experimental::Requested()) return false;
    return Neurotic::Mfg::Experimental::PrepareAtBoundary(experimentalFgLane(true));
}

bool StreamlineHooks::prepareNativeMfgCapabilities(HMODULE enteredProvider, ID3D12GraphicsCommandList* command)
{
    const auto& state = State::Instance();
    if (!Config::Instance()->FGDLSSGNativeMfgExperimental.value_or_default() ||
        renderApi != sl::RenderAPI::eD3D12 || state.activeFgInput != FGInput::NoFG ||
        state.activeFgOutput != FGOutput::NoFG || state.activeFgNvngx != FGNvngxReplacement::None) return false;
    const auto gpu = IdentifyGpu::getPrimaryGpu();
    if (gpu.nvidiaArchInfo.architecture_id < NV_GPU_ARCHITECTURE_AD100 ||
        gpu.nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_GB200) return false;
    LUID enteredAdapter {};
    bool adapterVerified = false;
    if (enteredProvider)
    {
        ID3D12Device* device = nullptr;
        if (command && SUCCEEDED(command->GetDevice(IID_PPV_ARGS(&device))))
        {
            enteredAdapter = device->GetAdapterLuid();
            adapterVerified = true;
            device->Release();
        }
    }
    // No game options have been passed yet. Patch qualification is independent
    // of their layout; later supported option versions reuse this publication.
    return Neurotic::Mfg::TryPublishAdaMfg(mfgSelectedWrapper.load(),
        nrMfgHookGeneration.load(), 5, gpu.luid, enteredProvider, adapterVerified ? &enteredAdapter : nullptr);
}

void StreamlineHooks::streamlineLogCallback_sl1(sl1::LogType type, const char* msg)
{
    if (msg == nullptr)
        return;

    char* trimmed_msg = trimStreamlineLog(msg);

    if (trimmed_msg != nullptr)
    {
        switch (type)
        {
        case sl1::LogType::eLogTypeWarn:
            LOG_WARN("{}", trimmed_msg);
            break;
        case sl1::LogType::eLogTypeInfo:
            LOG_INFO("{}", trimmed_msg);
            break;
        case sl1::LogType::eLogTypeError:
            LOG_ERROR("{}", trimmed_msg);
            break;
        case sl1::LogType::eLogTypeCount:
            LOG_ERROR("{}", trimmed_msg);
            break;
        }

        free(trimmed_msg);
    }

    if (o_logCallback_sl1)
        o_logCallback_sl1(type, msg);
}

bool StreamlineHooks::hkslInit_sl1(const sl1::Preferences& pref, int applicationId)
{
    LOG_FUNC();

    sl1::Preferences localPref = pref;

    if (localPref.logMessageCallback != &streamlineLogCallback_sl1)
        o_logCallback_sl1 = localPref.logMessageCallback;
    localPref.logLevel = sl1::LogLevel::eLogLevelCount;
    localPref.logMessageCallback = &streamlineLogCallback_sl1;
    return o_slInit_sl1(localPref, applicationId);
}

bool StreamlineHooks::hkslSetTag_sl1(const sl1::Resource* resource, sl1::BufferType tag, uint32_t id,
                                     const sl1::Extent* extent)
{
    // NR-FEED-001 BEGIN
    Neurotic::Feed::Callback feedTag({"Streamline.v1", FeedStreamlineApi(renderApi), "tag"}, resource);
    feedTag.Value("provider.viewport", id);feedTag.Value("tag.type", tag);
    // Native SL1 resource layout stays with its existing owner; no guessed lifetime mapping.
    feedTag.Resource("tag.resource", Neurotic::Contracts::SemanticKind::Other, resource);
    // NR-FEED-001 END
    if (IsSL1AndFGActive())
        State::Instance().s_sl1FGInputs.setTag(resource, tag, id, extent);

    return o_slSetTag_sl1(resource, tag, id, extent);
}

bool StreamlineHooks::hkslSetConstants_sl1(const sl1::Constants& values, uint32_t frameIndex, uint32_t id)
{
    // NR-FEED-001 BEGIN
    Neurotic::Feed::Callback feedConstants({"Streamline.v1", FeedStreamlineApi(renderApi), "constants"}, &values);
    feedConstants.Value("provider.frame", frameIndex);feedConstants.Value("provider.viewport", id);
    Neurotic::Feed::ObserveStreamlineConstants(feedConstants, values);
    // NR-FEED-001 END
    std::scoped_lock lock(setConstantsMutex);

    LOG_TRACE("SL1 slSetConstants frameIndex: {}, id: {}", frameIndex, id);

    if (IsSL1AndFGActive())
        State::Instance().s_sl1FGInputs.setConstants(values, frameIndex, id);

    return o_slSetConstants_interposer_sl1(values, frameIndex, id);
}

bool StreamlineHooks::hkslEvaluateFeature_sl1(sl1::CommandBuffer* cmdBuffer, sl1::Feature feature, uint32_t frameIndex,
                                              uint32_t id)
{
    LOG_TRACE("SL1 slEvaluateFeature feature: {}, frameIndex: {}, id: {}", magic_enum::enum_name(feature), frameIndex,
              id);

    if (IsSL1AndFGActive() && feature == sl1::Feature::eFeatureReflex)
    {
        const auto marker = (sl1::ReflexMarker) id;

        if (marker == sl1::ReflexMarker::eReflexMarkerRenderSubmitStart)
        {
            State::Instance().s_sl1FGInputs.evaluateState();
            State::Instance().s_sl1FGInputs.evaluateFeature(cmdBuffer, feature, frameIndex, id);
        }
        else if (marker == sl1::ReflexMarker::eReflexMarkerPresentStart)
        {
            State::Instance().s_sl1FGInputs.markPresent(frameIndex);
        }
    }

    return o_slEvaluateFeature_sl1(cmdBuffer, feature, frameIndex, id);
}

void StreamlineHooks::hookSystemCaps(sl::param::IParameters* params)
{
    if (State::Instance().streamlineVersion.major > 1)
    {
        if (!systemCaps)
            sl::param::getPointerParam(params, sl::param::common::kSystemCaps, &systemCaps);
    }
    else if (State::Instance().streamlineVersion.major == 1)
    {
        // This should be Streamline 1.5 as previous versions don't even have slOnPluginLoad
        if (!systemCapsSl15)
        {
            LOG_TRACE(
                "Attempting to get system caps for Streamline v1, this could fail depending on the exact version");
            sl::param::getPointerParam(params, sl::param::common::kSystemCaps, &systemCapsSl15);
        }
    }
}

uint32_t StreamlineHooks::getSystemCapsArch(SystemCaps* altSystemCaps)
{
    uint32_t highestArch = 0;

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    if (!fakenvapi::isUsingAsMainNvapi() && primaryGpu.vendorId == VendorId::Nvidia)
    {
        if (State::Instance().streamlineVersion.major > 1)
        {
            auto caps = altSystemCaps != nullptr ? altSystemCaps : systemCaps;
            if (caps)
            {
                for (auto& adapter : caps->adapters)
                {
                    if (adapter.architecture > highestArch)
                        highestArch = adapter.architecture;
                }
            }
        }
        else if (State::Instance().streamlineVersion.major == 1)
        {
            if (systemCapsSl15)
            {
                for (uint32_t i = 0; i < systemCapsSl15->gpuCount; i++)
                {
                    if (systemCapsSl15->architecture[i] > highestArch)
                        highestArch = systemCapsSl15->architecture[i];
                }
            }
        }
    }

    // By default spoof Pascal, gets Reflex but not DLSSD
    // Could be problematic if not using fakenvapi but nvapi might not be initialized yet
    if (highestArch == 0)
        highestArch = NV_GPU_ARCHITECTURE_GP100;

    return highestArch;
}

void StreamlineHooks::setArch(uint32_t arch, SystemCaps* altSystemCaps)
{
    auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    // altSystemCaps has to be sl2+
    if (State::Instance().streamlineVersion.major > 1 || altSystemCaps)
    {
        // Assumes that altCaps are always for SL2+
        auto caps = altSystemCaps != nullptr ? altSystemCaps : systemCaps;
        if (caps)
        {
            for (uint32_t i = 0; i < caps->gpuCount; i++)
            {
                caps->adapters[i].architecture = arch;
                caps->adapters[i].vendor = VendorId::Nvidia;
            }

            if (fakenvapi::isUsingAsMainNvapi() || primaryGpu.vendorId != VendorId::Nvidia)
                caps->driverVersionMajor = 999;

            caps->hwsSupported = true;
        }
    }
    else if (State::Instance().streamlineVersion.major == 1)
    {
        if (systemCapsSl15)
        {
            for (uint32_t i = 0; i < systemCapsSl15->gpuCount; i++)
                systemCapsSl15->architecture[i] = arch;

            if (fakenvapi::isUsingAsMainNvapi() || primaryGpu.vendorId != VendorId::Nvidia)
                systemCapsSl15->driverVersionMajor = 999;

            systemCapsSl15->hwSchedulingEnabled = true;
        }
    }
}

// Spoof arch based on feature and current arch
void StreamlineHooks::spoofArch(uint32_t currentArch, sl::Feature feature, SystemCaps* altSystemCaps)
{
    constexpr uint32_t maxArch = 0xFFFFFFFF;

    // Don't change arch for DLSS/DLSSD with turing and above
    if (feature == sl::kFeatureDLSS)
    {
        if (currentArch < NV_GPU_ARCHITECTURE_TU100)
            return setArch(maxArch, altSystemCaps);
    }

    // Don't spoof DLSSD at all
    else if (feature == sl::kFeatureDLSS_RR)
    {
        return;
    }

    // Don't change arch for DLSSG with ada and above
    else if (feature == sl::kFeatureDLSS_G)
    {
        if (State::Instance().activeFgNvngx != FGNvngxReplacement::None)
        {
            if (!Nvngx_FG::isDx12Available() && !Nvngx_FG::isVulkanAvailable())
                return setArch(0);
        }

        if (currentArch < NV_GPU_ARCHITECTURE_AD100)
            return setArch(maxArch, altSystemCaps);
    }

    else if (feature == sl::kFeatureReflex || feature == sl::kFeaturePCL)
    {
        if (fakenvapi::isUsingAsMainNvapi())
            return setArch(maxArch, altSystemCaps);
    }
}

bool StreamlineHooks::hkdlss_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                            const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    uint32_t currentArch = 0;
    if (Config::Instance()->StreamlineSpoofing.value_or_default())
    {
        hookSystemCaps(params);
        currentArch = getSystemCapsArch();
        spoofArch(currentArch, sl::kFeatureDLSS);
    }

    auto result = o_dlss_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (Config::Instance()->StreamlineSpoofing.value_or_default())
        setArch(currentArch);

    nlohmann::json configJson = nlohmann::json::parse(*pluginJSON);

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    if (primaryGpu.vendorId != VendorId::Nvidia || !primaryGpu.dlssCapable)
    {
        if (configJson.contains("/external/vk/instance/extensions"_json_pointer))
            configJson["external"]["vk"]["instance"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/extensions"_json_pointer))
            configJson["external"]["vk"]["device"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/1.2_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.2_features"].clear();

        if (configJson.contains("/external/vk/device/1.3_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.3_features"].clear();
    }

    PatchSL1PluginJson(configJson);

    config = configJson.dump();

    *pluginJSON = config.c_str();

    return result;
}

sl::Result StreamlineHooks::hkslDLSSGetOptimalSettings(const sl::DLSSOptions& options,
                                                       sl::DLSSOptimalSettings& settings)
{
    static bool modesBroken = false;

    auto localOptions = options;

    if (localOptions.mode == sl::DLSSMode::eOff)
        modesBroken = true;

    if (modesBroken)
    {
        if (localOptions.mode == sl::DLSSMode::eMaxPerformance)
            localOptions.mode = sl::DLSSMode::eUltraPerformance;
        else if (localOptions.mode == sl::DLSSMode::eBalanced)
            localOptions.mode = sl::DLSSMode::eMaxPerformance;
        else if (localOptions.mode == sl::DLSSMode::eMaxQuality)
            localOptions.mode = sl::DLSSMode::eBalanced;
        else if (localOptions.mode == sl::DLSSMode::eUltraQuality)
            localOptions.mode = sl::DLSSMode::eMaxQuality;
        else if (localOptions.mode == sl::DLSSMode::eUltraPerformance)
            localOptions.mode = sl::DLSSMode::eDLAA;
    }

    return o_slDLSSGetOptimalSettings(localOptions, settings);
}

bool StreamlineHooks::hkdlssg_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                             const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    bool shouldSpoofArch =
        Config::Instance()->StreamlineSpoofing.value_or_default() &&
        (State::Instance().activeFgInput == FGInput::NvngxFG || State::Instance().activeFgInput == FGInput::DLSSG);

    uint32_t currentArch = 0;
    if (shouldSpoofArch)
    {
        hookSystemCaps(params);
        currentArch = getSystemCapsArch();
        spoofArch(currentArch, sl::kFeatureDLSS_G);
    }

    auto result = o_dlssg_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (shouldSpoofArch)
        setArch(currentArch);

    nlohmann::json configJson = nlohmann::json::parse(*pluginJSON);

    // Kill the DLSSG streamline swapchain hooks
    if (State::Instance().activeFgInput == FGInput::DLSSG || State::Instance().activeFgOutput == FGOutput::DLSSG)
    {
        if (configJson.contains("/hooks"_json_pointer))
            configJson["hooks"].clear();

        if (configJson.contains("/exclusive_hooks"_json_pointer))
            configJson["exclusive_hooks"].clear();

        if (configJson.contains("/external/feature/tags"_json_pointer))
            configJson["external"]["feature"]["tags"].clear(); // We handle the DLSSG resources

        if (configJson.contains("/external/vk/device/queues/compute/count"_json_pointer))
            configJson["external"]["vk"]["device"]["queues"]["compute"]["count"] = 0;

        if (configJson.contains("/external/vk/device/queues/graphics/count"_json_pointer))
            configJson["external"]["vk"]["device"]["queues"]["graphics"]["count"] = 0;

        if (configJson.contains("/external/vk/device/1.2_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.2_features"].clear();

        if (configJson.contains("/external/vk/device/1.3_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.3_features"].clear();
    }

    if (State::Instance().activeFgInput == FGInput::DLSSG || State::Instance().activeFgInput == FGInput::NvngxFG)
    {
        if (configJson.contains("/vsync/supported"_json_pointer))
            configJson["vsync"]["supported"] = true; // disable eVSyncOffRequired

        if (configJson.contains("/external/hws/required"_json_pointer))
            configJson["external"]["hws"]["required"] = false; // disable eHardwareSchedulingRequired

        // if (configJson.contains("/external/vk/opticalflow/supported"_json_pointer))
        //     configJson["external"]["vk"]["opticalflow"]["supported"] = true;
    }

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    if (primaryGpu.vendorId != VendorId::Nvidia || !primaryGpu.dlssCapable)
    {
        if (configJson.contains("/external/vk/instance/extensions"_json_pointer))
            configJson["external"]["vk"]["instance"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/extensions"_json_pointer))
            configJson["external"]["vk"]["device"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/1.2_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.2_features"].clear();

        if (configJson.contains("/external/vk/device/1.3_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.3_features"].clear();
    }

    PatchSL1PluginJson(configJson);

    config = configJson.dump();

    *pluginJSON = config.c_str();

    return result;
}

const char* StreamlineHooks::hkdlssg_slGetPluginJSONConfig_sl1()
{
    static std::string patchedConfig;

    const char* originalConfig = o_dlssg_slGetPluginJSONConfig_sl1();

    if (originalConfig == nullptr)
        return originalConfig;

    try
    {
        auto configJson = nlohmann::json::parse(originalConfig);

        LOG_DEBUG("SL1 DLSSG JSON before patch: {}", configJson.dump());

        PatchSL1PluginJson(configJson);
        // RemoveSL1DLSSGHookEntriesRecursive(configJson);

        patchedConfig = configJson.dump();

        LOG_DEBUG("SL1 DLSSG JSON after patch: {}", patchedConfig);

        return patchedConfig.c_str();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Failed to patch SL1 DLSSG JSON config: {}", e.what());
        return originalConfig;
    }
}

bool StreamlineHooks::hklocal_dlssg_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                                   const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    bool shouldSpoofArch = Config::Instance()->StreamlineSpoofing.value_or_default();

    uint32_t currentArch = 0;
    SystemCaps* localSystemCaps = nullptr;
    if (shouldSpoofArch)
    {
        sl::param::getPointerParam(params, sl::param::common::kSystemCaps, &localSystemCaps);

        if (localSystemCaps)
        {
            currentArch = getSystemCapsArch(localSystemCaps);
            spoofArch(currentArch, sl::kFeatureDLSS_G, localSystemCaps);
        }
    }

    auto result = o_local_dlssg_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (shouldSpoofArch && localSystemCaps)
        setArch(currentArch, localSystemCaps);

    nlohmann::json configJson = nlohmann::json::parse(*pluginJSON);

    if (configJson.contains("/external/hws/required"_json_pointer))
        configJson["external"]["hws"]["required"] = false; // disable eHardwareSchedulingRequired

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    if (primaryGpu.vendorId != VendorId::Nvidia || !primaryGpu.dlssCapable)
    {
        if (configJson.contains("/external/vk/instance/extensions"_json_pointer))
            configJson["external"]["vk"]["instance"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/extensions"_json_pointer))
            configJson["external"]["vk"]["device"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/1.2_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.2_features"].clear();

        if (configJson.contains("/external/vk/device/1.3_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.3_features"].clear();
    }

    config = configJson.dump();

    *pluginJSON = config.c_str();

    return result;
}

sl::Result StreamlineHooks::hkslSetConstants(const sl::Constants& values, const sl::FrameToken& frame,
                                             const sl::ViewportHandle& viewport)
{
    const auto replacementOwner = Sl_Inputs_Dx12::CaptureOwner();
    const auto replacementProvider = nrReplacementInputGeneration.load();
    // NR-FEED-001 BEGIN
    Neurotic::Feed::Callback feedConstants({"Streamline", FeedStreamlineApi(renderApi), "constants"}, &viewport);
    feedConstants.Value("provider.frame", static_cast<uint32_t>(frame));
    feedConstants.Value("provider.viewport", static_cast<uint32_t>(viewport));
    Neurotic::Feed::ObserveStreamlineConstants(feedConstants, values);
    // NR-FEED-001 END
    LOG_TRACE("called with frameIndex: {}, viewport: {}", (unsigned int) frame, (unsigned int) viewport);

    const auto result = o_slSetConstants(values, frame, viewport);
    if (result == sl::Result::eOk && renderApi == sl::RenderAPI::eD3D12 && replacementProvider == nrReplacementInputGeneration.load())
        State::Instance().slFGInputs.setConstants(values, (uint32_t)frame, (uint32_t)viewport, replacementProvider, replacementOwner);
    return result;
}

bool StreamlineHooks::hkcommon_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                              const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    auto result = o_common_slOnPluginLoad(params, loaderJSON, pluginJSON);

    nlohmann::json configJson = nlohmann::json::parse(*pluginJSON);

    auto& slVersion = State::Instance().streamlineVersion;

    // Grab a version of the potentially updated sl.common
    // Opti assumes that all plugins will have this version
    configJson.at("version").at("major").get_to(slVersion.major);
    configJson.at("version").at("minor").get_to(slVersion.minor);
    configJson.at("version").at("build").get_to(slVersion.patch);

    // Completely disables Streamline hooks
    // if (true)
    //    configJson["hooks"].clear();
    //    configJson["exclusive_hooks"].clear();
    //}

    PatchSL1PluginJson(configJson);

    config = configJson.dump();

    *pluginJSON = config.c_str();

    return result;
}

sl::Result StreamlineHooks::hkslDLSSGSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options)
{
    Neurotic::Mfg::Experimental::GameFgScope gameFgScope(experimentalFgLane(true));
    Neurotic::Mfg::MfgSetterScope setterScope(nrMfgRequests);
    const auto captured = Neurotic::Mfg::CaptureOptions(options);
    const auto hookGeneration = nrMfgHookGeneration.load();
    const auto providerGeneration = renderApi == sl::RenderAPI::eVulkan ?
        DlssNr::VulkanNrStreamlineAdapter().Generation() : 0;
    const auto currentInvocation = [&] {
        return hookGeneration == nrMfgHookGeneration.load() &&
            (renderApi != sl::RenderAPI::eVulkan ||
             providerGeneration == DlssNr::VulkanNrStreamlineAdapter().Generation());
    };
    auto& state = State::Instance();
    const auto publish = [&](const sl::DLSSGOptions& observed, sl::Result result) {
        if (!currentInvocation()) return;
        if (captured.supported && result == sl::Result::eOk)
        {
            state.dlssgLastSetMode = observed.mode;
            Neurotic::Semantic::Character::NativeFgWork().Requested(observed.mode!=sl::DLSSGMode::eOff);
        }
        if (renderApi == sl::RenderAPI::eVulkan)
            DlssNr::VulkanNrStreamlineAdapter().Options(static_cast<uint32_t>(viewport), observed,
                captured.supported && result == sl::Result::eOk);
    };
    {
        std::lock_guard lock(lastDlssgOptionsMutex);
        lastDlssgOptionsReplayable = false;
    }
    if (!captured.supported)
    {
        const auto experimental = Neurotic::Mfg::Experimental::ReadSnapshot();
        if (experimental.backendLoaded || experimental.stage == Neurotic::Mfg::Experimental::Stage::Poisoned)
        {
            Neurotic::Mfg::Experimental::RefuseRequest(Neurotic::Mfg::Experimental::Reason::UnsupportedAbi);
            return sl::Result::eErrorFeatureNotSupported;
        }
        // Unknown layouts remain opaque. A known layout with borrowed pointers
        // can be changed during this call, but cannot back a later UI replay.
        const auto result = o_slDLSSGSetOptions(viewport, options);
        publish(options, result);
        return result;
    }
    sl::DLSSGOptions newOptions = captured.value;
    // Only this synchronous forwarding object borrows the caller's pointers.
    // The stored scalar snapshot stays pointer-free and is replayable only
    // when the original game request was pointer-free too.
    newOptions.structVersion = options.structVersion;
    newOptions.onErrorCallback = options.onErrorCallback;
    newOptions.next = options.next;
    bool optionsModified = false;

    // Disable game's DLSSG when we are trying to create our own instance of DLSSG
    if (state.activeFgInput != FGInput::DLSSG && state.activeFgOutput == FGOutput::DLSSG)
    {
        newOptions.mode = sl::DLSSGMode::eOff;
        if (!Neurotic::Mfg::Experimental::AllowFeatureCall(newOptions.mode == sl::DLSSGMode::eOff))
            return sl::Result::eErrorFeatureNotSupported;
        const auto result = o_slDLSSGSetOptions(viewport, newOptions);
        publish(newOptions, result);
        return result;
    }

    const auto dlssgPotentiallyActive = newOptions.mode == sl::DLSSGMode::eOn ||
                                        newOptions.mode == sl::DLSSGMode::eAuto ||
                                        newOptions.mode == sl::DLSSGMode::eDynamic;

    bool enableDynamicMode = renderApi!=sl::RenderAPI::eVulkan && Config::Instance()->FGDLSSGOverrideForceDMFG.value_or_default() &&
                             state.dlssgGameDMFGSupported && dlssgPotentiallyActive;

    if (enableDynamicMode)
    {
        newOptions.mode = sl::DLSSGMode::eDynamic;
        optionsModified = true;
    }

    if (newOptions.mode == sl::DLSSGMode::eDynamic && Config::Instance()->FGDLSSGFramerateTargetDMFG.has_value())
    {
        newOptions.dynamicTargetFrameRate = Config::Instance()->FGDLSSGFramerateTargetDMFG.value();
        optionsModified = true;
    }

    // The Vulkan overlay is composed at physical Present, after native FG.
    // Menu visibility must not change the provider mode, ratio or Reflex count.

    LOG_TRACE("DLSSG Modified Mode: {}", magic_enum::enum_name(newOptions.mode));

    if (dlssgPotentiallyActive && state.streamlineVersion >= feature_version { 2, 7, 1 })
    {
        // Won't take effect with Dynamic
        if (Config::Instance()->FGDLSSGOverrideInterpolationCount.has_value() &&
            !(Config::Instance()->FGDLSSGNativeMfgExperimental.value_or_default() &&
              state.swapchainApi == API::DX12))
        {
            auto overrideCount = Config::Instance()->FGDLSSGOverrideInterpolationCount.value();
            if (overrideCount != 0)
            {
                if (state.dlssgMfgMax.has_value() && overrideCount > state.dlssgMfgMax.value())
                    overrideCount = state.dlssgMfgMax.value();
                newOptions.numFramesToGenerate = overrideCount;
                optionsModified = true;
            }
            else if (!enableDynamicMode)
            {
                newOptions.mode = sl::DLSSGMode::eOff;
                optionsModified = true;
            }
        }
    }

    prepareExperimentalMfgCapabilities();
    const auto experimental = Neurotic::Mfg::Experimental::ReadSnapshot();
    if (experimental.family != Neurotic::Mfg::Experimental::Family::None && experimental.requested &&
        renderApi == sl::RenderAPI::eD3D12 && state.activeFgInput == FGInput::NoFG &&
        state.activeFgOutput == FGOutput::NoFG && state.activeFgNvngx == FGNvngxReplacement::None)
    {
        namespace EM = Neurotic::Mfg::Experimental;
        // Restore the original request before the one experimental policy owner.
        // A refused override forwards the game request; it never clamps saved intent.
        newOptions = captured.value;
        EM::RestoreBorrowedOptions(newOptions, options);
        optionsModified = false;
        const auto& configured = Config::Instance()->FGDLSSGOverrideInterpolationCount;
        const auto mode = configured.value_or(-1) == 0 ? EM::Mode::Off :
            options.mode == sl::DLSSGMode::eDynamic || enableDynamicMode ? EM::Mode::Dynamic :
            !configured.has_value() ? EM::Mode::Default : EM::Mode::Fixed;
        const auto decision = EM::Decide({mode, configured.value_or(0)},
            {experimental.prepared, experimental.prepared, static_cast<uint32_t>(captured.callerVersion),
             experimental.ceiling ? std::optional<uint32_t>(experimental.ceiling) : std::nullopt});
        if (mode == EM::Mode::Dynamic) {
            EM::RefuseRequest(EM::Reason::DynamicNotQualified);
            return sl::Result::eErrorFeatureNotSupported;
        }
        if (decision.overrideRequest && (decision.off || dlssgPotentiallyActive)) {
            if (decision.off) newOptions.mode = sl::DLSSGMode::eOff;
            else newOptions.numFramesToGenerate = decision.generated;
            optionsModified = true;
        }
    }

    uint64_t mfgAttempt = 0;
    bool nativeRecoveryRequest = false;
    auto vulkanSelection = Neurotic::Mfg::MfgSelection::Game;
    auto mfgDecisionStatus = Neurotic::Mfg::MfgDecisionStatus::PassThrough;
    if (Config::Instance()->FGDLSSGNativeMfgExperimental.value_or_default() &&
        state.swapchainApi == API::DX12)
    {
        const auto gpu = IdentifyGpu::getPrimaryGpu();
        const bool ada = gpu.nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_AD100 &&
            gpu.nvidiaArchInfo.architecture_id < NV_GPU_ARCHITECTURE_GB200;
        nativeRecoveryRequest = ada && renderApi == sl::RenderAPI::eD3D12 &&
            state.activeFgInput == FGInput::NoFG && state.activeFgOutput == FGOutput::NoFG &&
            state.activeFgNvngx == FGNvngxReplacement::None &&
            experimental.family == Neurotic::Mfg::Experimental::Family::None;
        const auto& configured = Config::Instance()->FGDLSSGOverrideInterpolationCount;
        const auto selection = configured.has_value() && !enableDynamicMode &&
            newOptions.mode != sl::DLSSGMode::eDynamic && ada ?
            Neurotic::Mfg::SelectionFromStoredGenerated(configured.value()) :
            Neurotic::Mfg::MfgSelection::Game;
        const auto generation = nrMfgHookGeneration.load();
        const auto selectedWrapper = mfgSelectedWrapper.load();
        const bool qualified = ada && dlssgPotentiallyActive &&
            state.activeFgInput != FGInput::DLSSG && state.activeFgOutput != FGOutput::DLSSG &&
            selectedWrapper && Neurotic::Mfg::TryPublishAdaMfg(selectedWrapper, generation,
                static_cast<uint32_t>(captured.callerVersion), gpu.luid);
        const auto publication = Neurotic::Mfg::AdaMfgSnapshot();
        const auto qualifiedMax = qualified && publication.generation == generation &&
            publication.status == Neurotic::Mfg::MfgRuntimeStatus::Published ? publication.maxGenerated : 0u;
        const auto decision = Neurotic::Mfg::ResolveMfgSelection(selection,
            dlssgPotentiallyActive, options.numFramesToGenerate, qualified,
            generation, generation, qualifiedMax, nrMfgRequests.HighRatioRefusal().blocked);
        mfgDecisionStatus = decision.status;
        if (decision.status == Neurotic::Mfg::MfgDecisionStatus::Fixed)
        {
            newOptions.numFramesToGenerate = decision.generated;
            optionsModified = true;
        }
        else if (decision.status == Neurotic::Mfg::MfgDecisionStatus::SelectedOff)
        {
            newOptions.mode = sl::DLSSGMode::eOff;
            optionsModified = true;
        }
        mfgAttempt = nrMfgRequests.Begin(static_cast<uint32_t>(viewport), generation,
            static_cast<uint32_t>(captured.callerVersion), dlssgPotentiallyActive,
            options.numFramesToGenerate, selection, decision);
    }

    if (renderApi == sl::RenderAPI::eVulkan)
    {
        const auto& configured = Config::Instance()->FGDLSSGOverrideInterpolationCount;
        const auto selected = configured.has_value() ?
            Neurotic::Mfg::SelectionFromStoredGenerated(configured.value()) :
            Neurotic::Mfg::MfgSelection::Game;
        vulkanSelection = selected;
        // Publish what was actually forwarded. Off never falls back to On on
        // rejection; the game's next call can retry with fresh borrowed data.
        const auto status = selected == Neurotic::Mfg::MfgSelection::Off ?
            Neurotic::Mfg::MfgDecisionStatus::SelectedOff :
            Neurotic::Mfg::MfgDecisionStatus::PassThrough;
        mfgAttempt = nrMfgRequests.Begin(static_cast<uint32_t>(viewport), hookGeneration,
            static_cast<uint32_t>(captured.callerVersion), dlssgPotentiallyActive,
            options.numFramesToGenerate, selected,
            {status, newOptions.mode != sl::DLSSGMode::eOff, newOptions.numFramesToGenerate, hookGeneration});
    }

    // Admission follows the actual outgoing mode, never saved intent. A route
    // change or Dynamic override must not borrow the ordinary Off exception.
    const auto& forwardedOptions = optionsModified ? newOptions : options;
    if (!Neurotic::Mfg::Experimental::AllowFeatureCall(forwardedOptions.mode == sl::DLSSGMode::eOff))
        return sl::Result::eErrorFeatureNotSupported;
    const auto diagnosticOperation = DlssNr::FgLifecycle::BeginOptions();
    auto result = o_slDLSSGSetOptions(viewport, forwardedOptions);
    Neurotic::Mfg::Experimental::ObserveOptions(static_cast<uint32_t>(captured.callerVersion), captured.value.numFramesToGenerate, forwardedOptions.numFramesToGenerate, static_cast<int>(result));
    if (!currentInvocation()) return result;
    // Game-controlled ratios can be rejected after a late unlock too. Observe
    // the actual current native request without changing pass-through/fallback.
    // Unknown layouts, replacement routes and RTX20/30 admission stay separate.
    const bool forwardedEnabled = forwardedOptions.mode == sl::DLSSGMode::eOn ||
        forwardedOptions.mode == sl::DLSSGMode::eAuto || forwardedOptions.mode == sl::DLSSGMode::eDynamic;
    if (mfgAttempt && nativeRecoveryRequest && forwardedEnabled &&
        result != sl::Result::eOk)
        nrMfgRequests.RejectHighRatio(forwardedOptions.numFramesToGenerate, static_cast<int>(result));
    if (mfgAttempt && Neurotic::Mfg::ShouldFallbackMfgOptions(mfgDecisionStatus,
        newOptions.numFramesToGenerate, captured.value.numFramesToGenerate,
        result == sl::Result::eOk))
    {
        const auto overrideResult = result;
        result = o_slDLSSGSetOptions(viewport, options);
        if (!currentInvocation()) return result;
        nrMfgRequests.CompleteFallback(mfgAttempt, static_cast<int>(overrideResult),
            static_cast<int>(result), captured.value.mode != sl::DLSSGMode::eOff,
            captured.value.numFramesToGenerate);
        newOptions = captured.value;
        LOG_WARN("Native Ada MFG override rejected ({}); restored game DLSS-G request ({})",
            static_cast<int>(overrideResult), static_cast<int>(result));
    }
    else if (mfgAttempt) nrMfgRequests.Complete(mfgAttempt, static_cast<int>(result));
    if (renderApi == sl::RenderAPI::eVulkan)
    {
        // Log changes, not per-frame setters. This includes borrowed calls so
        // the next game return can distinguish a pending UI choice from an
        // accepted or rejected provider override.
        const uint64_t signature = (uint64_t(static_cast<uint32_t>(result)) << 32) |
            (uint64_t(vulkanSelection) << 24) | (uint64_t(newOptions.mode) << 20) |
            (uint64_t(newOptions.numFramesToGenerate & 0xff) << 12) |
            (uint64_t(captured.callerVersion & 0xf) << 8) | (uint64_t(options.mode) << 4) |
            (uint64_t(captured.hasCallback) << 1) | uint64_t(captured.hasExtensions);
        static std::atomic<uint64_t> lastOverrideSignature { UINT64_MAX };
        if (lastOverrideSignature.exchange(signature) != signature)
            LOG_INFO("Vulkan DLSSG override: selected={} gameMode={} forwardedMode={} generated={} result={} abi={} callback={} extensions={} replayable={}",
                static_cast<unsigned>(vulkanSelection), static_cast<unsigned>(options.mode),
                static_cast<unsigned>(newOptions.mode), newOptions.numFramesToGenerate,
                static_cast<int>(result), captured.callerVersion, captured.hasCallback,
                captured.hasExtensions, captured.replayable);
    }
    publish(newOptions, result);
    if (result == sl::Result::eOk && captured.replayable)
    {
        std::lock_guard lock(lastDlssgOptionsMutex);
        lastDlssgViewport = viewport;
        lastDlssgOptions = captured.value;
        lastDlssgOptionsReplayable = true;
        lastDlssgOptionsHookGeneration = hookGeneration;
    }
    if (renderApi == sl::RenderAPI::eD3D12 && result == sl::Result::eOk)
        DlssNr::PreFg::PublishProvider(newOptions.mode != sl::DLSSGMode::eOff,
            static_cast<uint32_t>(viewport) == 0 && newOptions.mode == sl::DLSSGMode::eOn &&
            newOptions.numFramesToGenerate >= 1 && newOptions.numFramesToGenerate <= 5 &&
            newOptions.queueParallelismMode == sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue &&
            newOptions.enableUserInterfaceRecomposition != sl::Boolean::eTrue &&
            static_cast<uint32_t>(newOptions.flags & sl::DLSSGFlags::eShowOnlyInterpolatedFrame) == 0,
            newOptions.numFramesToGenerate);
    if (DlssNr::FgLifecycle::Enabled())
        DlssNr::FgLifecycle::Options(diagnosticOperation, static_cast<uint32_t>(viewport),
        static_cast<int>(newOptions.mode), newOptions.numFramesToGenerate,
        static_cast<unsigned>(newOptions.queueParallelismMode), static_cast<unsigned>(newOptions.flags),
        static_cast<unsigned>(newOptions.enableUserInterfaceRecomposition), static_cast<int>(result),
        DlssNr::PreFg::Provider().generation);
    return result;
}

sl::Result StreamlineHooks::hkslDLSSGGetState(const sl::ViewportHandle& viewport, sl::DLSSGState& state,
                                              const sl::DLSSGOptions* options)
{
    Neurotic::Mfg::Experimental::GameFgScope gameFgScope(experimentalFgLane(true));
    prepareExperimentalMfgCapabilities();
    if (Config::Instance()->FGDLSSGNativeMfgExperimental.value_or_default())
    {
        const auto publication = Neurotic::Mfg::AdaMfgSnapshot();
        // A newly resolved game function changes the hook generation. Renew
        // an owned publication before the game asks for its menu capacity,
        // even if it has not enabled FG or called SetOptions yet.
        if (publication.status != Neurotic::Mfg::MfgRuntimeStatus::Published ||
            publication.generation != nrMfgHookGeneration.load())
            prepareNativeMfgCapabilities();
    }
    const auto originalStructVersion = state.structVersion;
    const auto hookGeneration = nrMfgHookGeneration.load();
    const auto providerGeneration = renderApi == sl::RenderAPI::eVulkan ?
        DlssNr::VulkanNrStreamlineAdapter().Generation() : 0;
    const auto result = o_slDLSSGGetState(viewport, state, options);
    if (hookGeneration != nrMfgHookGeneration.load() ||
        (renderApi == sl::RenderAPI::eVulkan &&
         providerGeneration != DlssNr::VulkanNrStreamlineAdapter().Generation())) return result;
    const bool knownState = originalStructVersion >= 1 && originalStructVersion <= 4 &&
        state.structVersion == originalStructVersion && state.structType == sl::DLSSGState::s_structType;
    const bool observed = result == sl::Result::eOk && knownState;
    DlssNr::FgLifecycle::Completion(static_cast<uint32_t>(viewport), static_cast<int>(result),
        originalStructVersion, observed && originalStructVersion >= 3 ? state.inputsProcessingCompletionFence : nullptr,
        observed && originalStructVersion >= 3 ? state.lastPresentInputsProcessingCompletionFenceValue : 0,
        observed ? state.numFramesActuallyPresented : 0);
    if (renderApi == sl::RenderAPI::eVulkan)
        DlssNr::VulkanNrStreamlineAdapter().State(static_cast<uint32_t>(viewport), state, observed);
    // Consume the game's actual query. Additional queries would reset its
    // presented-frame counter and lack the game's present-thread ownership.
    Neurotic::Mfg::Experimental::ObserveCapacity(observed && originalStructVersion >= 2 ? state.numFramesToGenerateMax : 0);
    if (!observed) return result;
    if (originalStructVersion >= 4)
        State::Instance().dlssgGameDMFGSupported = renderApi != sl::RenderAPI::eVulkan &&
            state.bIsDynamicMFGSupported == sl::eTrue;

    auto& optiState = State::Instance();
    if (result == sl::Result::eOk)
        nrMfgRequests.Observe(static_cast<uint32_t>(viewport), nrMfgHookGeneration.load(),
            originalStructVersion >= 2 ? std::optional<uint32_t>(state.numFramesToGenerateMax) : std::nullopt,
            state.numFramesActuallyPresented);


    // Cache raw capability before any explicit experimental advertisement.
    if (originalStructVersion >= 2 && !optiState.dlssgMfgMax.has_value() &&
        state.numFramesToGenerateMax > 0 && state.numFramesToGenerateMax < 6)
    {
        optiState.dlssgMfgMax = state.numFramesToGenerateMax;
        LOG_TRACE("Saving original numFramesToGenerateMax: {}", optiState.dlssgMfgMax.value());
    }

    if (result == sl::Result::eOk && originalStructVersion >= 2 &&
        experimentalFgLane(true))
    {
        const auto qualified = Neurotic::Mfg::Experimental::ReadSnapshot();
        if (qualified.requested && qualified.family != Neurotic::Mfg::Experimental::Family::None &&
            qualified.prepared && (qualified.ceiling == 3 || qualified.ceiling == 5))
            state.numFramesToGenerateMax = Neurotic::Mfg::AdvertisedMfgGeneratedMax(
                state.numFramesToGenerateMax, true, true, qualified.ceiling);
    }

    if (result == sl::Result::eOk && originalStructVersion >= 2 &&
        Config::Instance()->FGDLSSGNativeMfgExperimental.value_or_default() &&
        optiState.swapchainApi == API::DX12 &&
        optiState.activeFgInput != FGInput::DLSSG && optiState.activeFgOutput != FGOutput::DLSSG &&
        Neurotic::Mfg::HasOwnedAdaMfg(mfgSelectedWrapper.load(), nrMfgHookGeneration.load()))
    {
        const auto publication = Neurotic::Mfg::AdaMfgSnapshot();
        if (publication.status == Neurotic::Mfg::MfgRuntimeStatus::Published &&
            publication.generation == nrMfgHookGeneration.load())
            state.numFramesToGenerateMax = Neurotic::Mfg::AdvertisedMfgGeneratedMax(
                state.numFramesToGenerateMax, true, true, publication.maxGenerated,
                nrMfgRequests.HighRatioRefusal().blocked);
    }

    if (originalStructVersion >= 4 && !State::Instance().dlssgGameDMFGSupported)
    {
        Config::Instance()->FGDLSSGOverrideForceDMFG.set_volatile_value(false);
    }

    if (renderApi != sl::RenderAPI::eVulkan && optiState.activeFgInput == FGInput::DLSSG)
    {
        auto fg = optiState.currentFG;

        if (fg != nullptr)
        {
            if (options != nullptr && options->flags & sl::DLSSGFlags::eRequestVRAMEstimate)
                state.estimatedVRAMUsageInBytes = static_cast<uint64_t>(256 * 1024) * 1024;

            if (fg->IsActive() && !fg->IsPaused())
            {
                state.numFramesActuallyPresented = fg->GetInterpolatedFrameCount() + 1;
            }
            else
            {
                state.numFramesActuallyPresented = 1;
            }
        }
        else
        {
            state.numFramesActuallyPresented = 1;
        }

        if (originalStructVersion >= 2) state.numFramesToGenerateMax = 1;

        LOG_DEBUG("Status: {}, numFramesActuallyPresented: {}", magic_enum::enum_name(state.status),
                  state.numFramesActuallyPresented);
    }

    return result;
}

bool StreamlineHooks::hkreflex_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                              const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    uint32_t currentArch = 0;
    if (Config::Instance()->StreamlineSpoofing.value_or_default())
    {
        hookSystemCaps(params);
        currentArch = getSystemCapsArch();
        spoofArch(currentArch, sl::kFeatureReflex);
    }

    auto result = o_reflex_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (Config::Instance()->StreamlineSpoofing.value_or_default())
        setArch(currentArch);

    nlohmann::json configJson = nlohmann::json::parse(*pluginJSON);

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    if (primaryGpu.vendorId != VendorId::Nvidia || !primaryGpu.dlssCapable)
    {
        if (configJson.contains("/external/vk/instance/extensions"_json_pointer))
            configJson["external"]["vk"]["instance"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/extensions"_json_pointer))
            configJson["external"]["vk"]["device"]["extensions"].clear();

        if (configJson.contains("/external/vk/device/1.2_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.2_features"].clear();

        if (configJson.contains("/external/vk/device/1.3_features"_json_pointer))
            configJson["external"]["vk"]["device"]["1.3_features"].clear();
    }

    PatchSL1PluginJson(configJson);

    config = configJson.dump();

    *pluginJSON = config.c_str();

    return result;
}

sl::Result StreamlineHooks::hkslReflexSetOptions(const sl::ReflexOptions& options)
{
    reflexGamesLastMode = options.mode;

    sl::ReflexOptions newOptions = options;

    if (Config::Instance()->FN_ForceReflex == ForceReflex::ForceEnable)
        newOptions.mode = sl::ReflexMode::eLowLatencyWithBoost;

    // Will cause a pink screen when used with DLSSG
    // if (Config::Instance()->FN_ForceReflex == 1)
    //     newOptions.mode = sl::ReflexMode::eOff;

    return o_slReflexSetOptions(newOptions);
}

sl::Result StreamlineHooks::hkslReflexSleep(const sl::FrameToken& frame)
{
    // if (State::Instance().activeFgOutput == FGOutput::DLSSG && StreamlineProxy::IsD3D12Inited() &&
    //     Config::Instance()->FGDLSSGUseGamesReflexMarkers.value_or_default())
    //{
    //     return StreamlineProxy::ReflexSleep()(frame);
    // }

    return o_slReflexSleep(frame);
}

void* StreamlineHooks::hkdlss_slGetPluginFunction(const char* functionName)
{
    LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_dlss_slOnPluginLoad = (PFN_slOnPluginLoad) o_dlss_slGetPluginFunction(functionName);
        return &hkdlss_slOnPluginLoad;
    }

    if (strcmp(functionName, "slDLSSGetOptimalSettings") == 0 &&
        State::Instance().gameQuirks & GameQuirk::PregmataFixDLSSModes)
    {
        o_slDLSSGetOptimalSettings = (decltype(&slDLSSGetOptimalSettings)) o_dlss_slGetPluginFunction(functionName);
        return &hkslDLSSGetOptimalSettings;
    }

    return o_dlss_slGetPluginFunction(functionName);
}

void* StreamlineHooks::hkdlssg_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_dlssg_slOnPluginLoad = (PFN_slOnPluginLoad) o_dlssg_slGetPluginFunction(functionName);
        bindNativeMfgWrapper(reinterpret_cast<const void*>(o_dlssg_slOnPluginLoad));
        return &hkdlssg_slOnPluginLoad;
    }

    if (strcmp(functionName, "slDLSSGSetOptions") == 0)
    {
        const auto next = (decltype(&slDLSSGSetOptions)) o_dlssg_slGetPluginFunction(functionName);
        if (next != o_slDLSSGSetOptions)
        {
            std::lock_guard lock(lastDlssgOptionsMutex);
            lastDlssgOptionsReplayable = false;
            const auto generation = nrMfgHookGeneration.fetch_add(1) + 1;
            nrMfgRequests.Invalidate(generation);
            Neurotic::Semantic::Character::NativeFgWork().Reset();
            Neurotic::Mfg::InvalidateAdaMfg(generation);
        }
        o_slDLSSGSetOptions = next;

        // Give steam overlay the original as it seems to be hooking it
        auto steamOverlay = KernelBaseProxy::GetModuleHandleA_()("gameoverlayrenderer64.dll");
        if (steamOverlay != nullptr)
        {
            if (HMODULE callerModule = Util::GetCallerModule(_ReturnAddress()); callerModule == steamOverlay)
                return o_slDLSSGSetOptions;
        }

        return &hkslDLSSGSetOptions;
    }

    if (strcmp(functionName, "slDLSSGGetState") == 0)
    {
        o_slDLSSGGetState = (decltype(&slDLSSGGetState)) o_dlssg_slGetPluginFunction(functionName);

        // Give steam overlay the original as it seems to be hooking it
        auto steamOverlay = KernelBaseProxy::GetModuleHandleA_()("gameoverlayrenderer64.dll");
        if (steamOverlay != nullptr)
        {
            if (HMODULE callerModule = Util::GetCallerModule(_ReturnAddress()); callerModule == steamOverlay)
                return o_slDLSSGGetState;
        }

        return &hkslDLSSGGetState;
    }

    if (strcmp(functionName, "slGetPluginJSONConfig") == 0 && IsSL1AndDLSSGActive())
    {
        o_dlssg_slGetPluginJSONConfig_sl1 =
            reinterpret_cast<PFN_slGetPluginJSONConfig_sl1>(o_dlssg_slGetPluginFunction(functionName));

        if (o_dlssg_slGetPluginJSONConfig_sl1 != nullptr)
        {
            LOG_WARN("Hooking SL1 DLSSG slGetPluginJSONConfig");
            return &hkdlssg_slGetPluginJSONConfig_sl1;
        }
    }

    // Ensure that we have those DLSSG calls
    if (!o_slDLSSGSetOptions)
        o_slDLSSGSetOptions = (decltype(&slDLSSGSetOptions)) o_dlssg_slGetPluginFunction("slDLSSGSetOptions");

    if (!o_slDLSSGGetState)
        o_slDLSSGGetState = (decltype(&slDLSSGGetState)) o_dlssg_slGetPluginFunction("slDLSSGGetState");

    return o_dlssg_slGetPluginFunction(functionName);
}

void* StreamlineHooks::hklocal_dlssg_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0 && State::Instance().activeFgNvngx != FGNvngxReplacement::None)
    {
        o_local_dlssg_slOnPluginLoad = (PFN_slOnPluginLoad) o_local_dlssg_slGetPluginFunction(functionName);
        return &hklocal_dlssg_slOnPluginLoad;
    }

    return o_local_dlssg_slGetPluginFunction(functionName);
}

bool StreamlineHooks::hkreflex_slSetConstants_sl1(const void* data, uint32_t frameIndex, uint32_t id)
{
    // Streamline v1's version of slReflexSetOptions + slPCLSetMarker
    static sl1::ReflexConstants constants {};
    constants = *(const sl1::ReflexConstants*) data;

    reflexGamesLastMode = (sl::ReflexMode) constants.mode;

    LOG_DEBUG("mode: {}, frameIndex: {}, id: {}", (uint32_t) constants.mode, frameIndex, id);

    if (Config::Instance()->FN_ForceReflex == ForceReflex::ForceEnable)
        constants.mode = sl1::ReflexMode::eReflexModeLowLatencyWithBoost;

    // Will cause a pink screen when used with DLSSG
    // else if (Config::Instance()->FN_ForceReflex == 1)
    //     constants.mode = sl1::ReflexMode::eReflexModeOff;

    return o_reflex_slSetConstants_sl1(&constants, frameIndex, id);
}

void* StreamlineHooks::hkreflex_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slSetConstants") == 0 && State::Instance().streamlineVersion.major == 1)
    {
        o_reflex_slSetConstants_sl1 = (PFN_slSetConstants_sl1) o_reflex_slGetPluginFunction(functionName);
        return &hkreflex_slSetConstants_sl1;
    }

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_reflex_slOnPluginLoad = (PFN_slOnPluginLoad) o_reflex_slGetPluginFunction(functionName);
        return &hkreflex_slOnPluginLoad;
    }

    if (strcmp(functionName, "slReflexSetOptions") == 0)
    {
        o_slReflexSetOptions = (decltype(&slReflexSetOptions)) o_reflex_slGetPluginFunction(functionName);
        return &hkslReflexSetOptions;
    }

    if (strcmp(functionName, "slReflexSleep") == 0)
    {
        o_slReflexSleep = (decltype(&slReflexSleep)) o_reflex_slGetPluginFunction(functionName);
        return &hkslReflexSleep;
    }

    // TODO: Hopefully a game doesn't call both, maybe separate
    if (strcmp(functionName, "slReflexSetMarker") == 0 &&
        (State::Instance().gameQuirks & GameQuirk::FixSlSimulationMarkers ||
         State::Instance().activeFgInput == FGInput::DLSSG))
    {
        o_slPCLSetMarker = (decltype(&slPCLSetMarker)) o_reflex_slGetPluginFunction(functionName);
        return &hkslPCLSetMarker;
    }

    return o_reflex_slGetPluginFunction(functionName);
}

sl::Result StreamlineHooks::hkslPCLSetMarker(sl::PCLMarker marker, const sl::FrameToken& frame)
{
    const auto nrProvider=DlssNr::VulkanNrStreamlineAdapter().Generation();
    if (marker == sl::PCLMarker::ePresentStart || marker == sl::PCLMarker::ePresentEnd)
        NR_FRAME_TRACE("nr-pcl", "phase=enter marker={} frame={} path=existing-hook",
            static_cast<unsigned int>(marker), static_cast<uint32_t>(frame));
    // if (State::Instance().activeFgOutput == FGOutput::DLSSG && StreamlineProxy::IsD3D12Inited() &&
    //     Config::Instance()->FGDLSSGUseGamesReflexMarkers.value_or_default())
    //{
    //     return StreamlineProxy::PCLSetMarker()(marker, frame);
    // }

    // HACK for broken games
    if (State::Instance().gameQuirks & GameQuirk::FixSlSimulationMarkers)
    {
        static uint64_t last_simulation_end_id = 0;
        if (marker == sl::PCLMarker::eSimulationEnd)
        {
            last_simulation_end_id = frame;
        }

        if (marker == sl::PCLMarker::eSimulationStart && last_simulation_end_id >= frame && o_slGetNewFrameToken)
        {
            const uint64_t correction_offset = last_simulation_end_id - frame + 1;
            uint32_t newFrameId = static_cast<uint32_t>(frame + correction_offset);

            sl::FrameToken* newFramePointer {};
            auto result = o_slGetNewFrameToken(newFramePointer, &newFrameId);

            LOG_WARN("Simulation start marker sent after end marker, offset: {}", correction_offset);

            result = o_slPCLSetMarker(marker, *newFramePointer);
            return result;
        }
    }

    if (State::Instance().activeFgInput == FGInput::DLSSG)
    {
        if (State::Instance().streamlineVersion.major == 1)
        {
            if (marker == sl::PCLMarker::eRenderSubmitStart)
            {
                State::Instance().s_sl1FGInputs.evaluateState();
            }
            else if (marker == sl::PCLMarker::ePresentStart)
            {
                State::Instance().s_sl1FGInputs.markPresent(frame);
            }
        }
        else
        {
            if (marker == sl::PCLMarker::eRenderSubmitStart)
            {
                State::Instance().slFGInputs.evaluateState();
            }
            else if (marker == sl::PCLMarker::ePresentStart)
            {
                State::Instance().slFGInputs.markPresent(frame);
            }
        }
    }

    const auto markerResult = o_slPCLSetMarker(marker, frame);
    NrVkPresentMarker(marker,frame,markerResult,nrProvider);
    if (marker == sl::PCLMarker::ePresentStart)
    {
        if (markerResult == sl::Result::eOk) DlssNr::PreFg::PresentStart(static_cast<uint32_t>(frame));
        else DlssNr::PreFg::PresentMarkerFailed();
    }
    else if (marker == sl::PCLMarker::ePresentEnd)
    {
        if (markerResult == sl::Result::eOk) DlssNr::PreFg::PresentEnd(static_cast<uint32_t>(frame));
        else DlssNr::PreFg::PresentMarkerFailed();
    }
    if (marker == sl::PCLMarker::ePresentStart || marker == sl::PCLMarker::ePresentEnd)
        NR_FRAME_TRACE("nr-pcl", "phase=return marker={} frame={} result={} path=existing-hook",
            static_cast<unsigned int>(marker), static_cast<uint32_t>(frame), static_cast<unsigned int>(markerResult));
    return markerResult;
}

bool StreamlineHooks::hkpcl_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                           const char** pluginJSON)
{
    LOG_FUNC();

    uint32_t currentArch = 0;
    if (Config::Instance()->StreamlineSpoofing.value_or_default())
    {
        hookSystemCaps(params);
        currentArch = getSystemCapsArch();
        spoofArch(currentArch, sl::kFeaturePCL);
    }

    auto result = o_pcl_slOnPluginLoad(params, loaderJSON, pluginJSON);

    if (Config::Instance()->StreamlineSpoofing.value_or_default())
        setArch(currentArch);

    return result;
}

namespace
{
std::atomic<decltype(&slPCLSetMarker)> associationPclMarker {nullptr};
sl::Result AssociationPclMarker(sl::PCLMarker marker, const sl::FrameToken& frame)
{
    const auto nrProvider=DlssNr::VulkanNrStreamlineAdapter().Generation();
    const bool presentMarker = marker == sl::PCLMarker::ePresentStart || marker == sl::PCLMarker::ePresentEnd;
    if (presentMarker)
        NR_FRAME_TRACE("nr-pcl", "phase=enter marker={} frame={} path=observer",
            static_cast<unsigned int>(marker), static_cast<uint32_t>(frame));
    const auto result = associationPclMarker.load(std::memory_order_acquire)(marker, frame);
    NrVkPresentMarker(marker,frame,result,nrProvider);
    if (marker == sl::PCLMarker::ePresentStart)
    {
        if (result == sl::Result::eOk) DlssNr::PreFg::PresentStart(static_cast<uint32_t>(frame));
        else DlssNr::PreFg::PresentMarkerFailed();
    }
    else if (marker == sl::PCLMarker::ePresentEnd)
    {
        if (result == sl::Result::eOk) DlssNr::PreFg::PresentEnd(static_cast<uint32_t>(frame));
        else DlssNr::PreFg::PresentMarkerFailed();
    }
    if (presentMarker)
        NR_FRAME_TRACE("nr-pcl", "phase=return marker={} frame={} result={} path=observer",
            static_cast<unsigned int>(marker), static_cast<uint32_t>(frame), static_cast<unsigned int>(result));
    return result;
}
}

void* StreamlineHooks::hkpcl_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slPCLSetMarker") == 0 &&
        (State::Instance().gameQuirks & GameQuirk::FixSlSimulationMarkers ||
         State::Instance().activeFgInput == FGInput::DLSSG))
    {
        o_slPCLSetMarker = (decltype(&slPCLSetMarker)) o_pcl_slGetPluginFunction(functionName);
        return &hkslPCLSetMarker;
    }

    // The native provider's successful Present markers are the authoritative frame
    // identity. Preserve the exact function/result; existing provider hooks above win.
    if (strcmp(functionName, "slPCLSetMarker") == 0)
    {
        const auto original = reinterpret_cast<decltype(&slPCLSetMarker)>(o_pcl_slGetPluginFunction(functionName));
        if (!original) return nullptr;
        associationPclMarker.store(original, std::memory_order_release);
        return reinterpret_cast<void*>(&AssociationPclMarker);
    }

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_pcl_slOnPluginLoad = (PFN_slOnPluginLoad) o_pcl_slGetPluginFunction(functionName);
        return &hkpcl_slOnPluginLoad;
    }

    return o_pcl_slGetPluginFunction(functionName);
}

bool StreamlineHooks::hk_setVoid(void* self, const char* key, void** value)
{
    // LOG_DEBUG("{}", key);

    if (strcmp(key, sl::param::common::kSystemCaps) == 0)
    {
        LOG_TRACE("Attempting to change system caps for Streamline v1, this could fail depending on the exact version");

        // SystemCapsSl15 is not entirely correct for Streamline 1.3
        // But we here only use the beginning that matches + extra
        auto caps = (SystemCapsSl15*) value;

        if (caps)
        {
            caps->gpuCount = 1;
            caps->architecture[0] = UINT_MAX;
            caps->driverVersionMajor = 999;

            // HAGS
            *((char*) value + 56) = (char) 0x01;
        }
    }

    return o_setVoid(self, key, value);
}

void StreamlineHooks::hkcommon_slSetParameters_sl1(void* params)
{
    LOG_FUNC();

    if (o_setVoid == nullptr && params)
    {
        void** vtable = *(void***) params;

        // It's flipped, 0 -> set void*, 7 -> get void*
        o_setVoid = (PFN_setVoid) vtable[0];

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_setVoid != nullptr)
            DetourAttach(&(PVOID&) o_setVoid, hk_setVoid);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook setVoid: {:X}", detourResult);
            o_setVoid = nullptr;
        }
    }

    o_common_slSetParameters_sl1(params);
}

void* StreamlineHooks::hkcommon_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_common_slOnPluginLoad = (PFN_slOnPluginLoad) o_common_slGetPluginFunction(functionName);
        return &hkcommon_slOnPluginLoad;
    }

    // Used around Streamline v1.3, as 1.5 doesn't seem to have it anymore
    if (strcmp(functionName, "slSetParameters") == 0)
    {
        o_common_slSetParameters_sl1 = (PFN_slSetParameters_sl1) o_common_slGetPluginFunction(functionName);
        return &hkcommon_slSetParameters_sl1;
    }

    return o_common_slGetPluginFunction(functionName);
}

void StreamlineHooks::updateForceReflex()
{
    // Not needed for Streamline v1 as slSetConstants is sent every frame
    if (o_slReflexSetOptions)
    {
        sl::ReflexOptions options;

        auto forceReflex = Config::Instance()->FN_ForceReflex.value_or_default();

        if (forceReflex == ForceReflex::ForceEnable)
            options.mode = sl::ReflexMode::eLowLatencyWithBoost;
        else if (forceReflex == ForceReflex::ForceDisable)
            options.mode = sl::ReflexMode::eOff;
        else if (forceReflex == ForceReflex::InGame)
            options.mode = reflexGamesLastMode;

        auto result = o_slReflexSetOptions(options);
        if (result != sl::Result::eOk)
        {
            LOG_WARN("Failed to update Reflex mode with error code: {} ({:X})", magic_enum::enum_name(result),
                     (UINT) result);
        }
    }
}

void StreamlineHooks::updateDlssgOptions()
{
    sl::ViewportHandle viewport {};
    sl::DLSSGOptions options {};
    {
        std::lock_guard lock(lastDlssgOptionsMutex);
        if (!o_slDLSSGSetOptions || !lastDlssgOptionsReplayable)
        {
            LOG_INFO("DLSSG override queued for the game's next settings call; no safely replayable options");
            return;
        }
        if (lastDlssgOptionsHookGeneration != nrMfgHookGeneration.load()) return;
        viewport = lastDlssgViewport;
        options = lastDlssgOptions;
    }
    if (o_slDLSSGSetOptions)
    {
        LOG_FUNC();
        hkslDLSSGSetOptions(viewport, options);
    }
}

void StreamlineHooks::bindNativeMfgWrapper(const void* function)
{
    HMODULE owner = nullptr;
    if (!function || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(function), &owner) || !owner ||
        !Neurotic::Mfg::IsAdaMfgModule(owner)) return;
    Neurotic::Mfg::Experimental::ObserveWrapper(owner);
    if (mfgSelectedWrapper.exchange(owner) == owner) return;
    const auto generation = nrMfgHookGeneration.fetch_add(1) + 1;
    nrMfgRequests.Invalidate(generation);
    Neurotic::Semantic::Character::NativeFgWork().Reset();
    Neurotic::Mfg::InvalidateAdaMfg(generation);
}

void StreamlineHooks::wrapNativeDlssgFunction(const char* name, void*& function)
{
    if (!function) return;
    if (std::strcmp(name, "slDLSSGSetOptions") == 0)
    {
        if (function == reinterpret_cast<void*>(&hkslDLSSGSetOptions)) return;
        // Passive observation has no companion query dependency. In particular,
        // resolving SetOptions must not require resolving or polling GetState.
        bindNativeMfgWrapper(function);
        const auto next = reinterpret_cast<decltype(o_slDLSSGSetOptions)>(function);
        if (next != o_slDLSSGSetOptions)
        {
            std::lock_guard lock(lastDlssgOptionsMutex);
            lastDlssgOptionsReplayable = false;
            const auto generation = nrMfgHookGeneration.fetch_add(1) + 1;
            nrMfgRequests.Invalidate(generation);
            Neurotic::Semantic::Character::NativeFgWork().Reset();
            Neurotic::Mfg::InvalidateAdaMfg(generation);
        }
        o_slDLSSGSetOptions = next;
        function = reinterpret_cast<void*>(&hkslDLSSGSetOptions);
    }
    else if (std::strcmp(name, "slDLSSGGetState") == 0)
    {
        if (function == reinterpret_cast<void*>(&hkslDLSSGGetState)) return;
        bindNativeMfgWrapper(function);
        o_slDLSSGGetState = reinterpret_cast<decltype(o_slDLSSGGetState)>(function);
        function = reinterpret_cast<void*>(&hkslDLSSGGetState);
    }
}

Neurotic::Mfg::MfgRequestReceipt StreamlineHooks::mfgRequestReceipt() noexcept
{
    return nrMfgRequests.Current();
}

Neurotic::Mfg::MfgHighRatioRefusal StreamlineHooks::mfgHighRatioRefusal() noexcept
{
    return nrMfgRequests.HighRatioRefusal();
}

// SL INTERPOSER

bool StreamlineHooks::PrepareVulkanFullFrame(uint64_t provider,uint64_t frame,uint32_t viewport)
{
    auto& adapter=DlssNr::VulkanNrStreamlineAdapter();
    if(!provider||provider!=adapter.Generation()||frame>UINT32_MAX||viewport==UINT32_MAX||
       !adapter.Reason(viewport).empty()||!o_slGetNewFrameToken||!o_slSetTagForFrame)return false;
    const uint32_t id=static_cast<uint32_t>(frame);sl::FrameToken* token=nullptr;
    if(o_slGetNewFrameToken(token,&id)!=sl::Result::eOk||!token||token->structVersion!=1||
       static_cast<uint32_t>(*token)!=id)return false;
    sl::ResourceTag tags[]={
        {nullptr,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr,sl::kBufferTypeUIColorAndAlpha,sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr,sl::kBufferTypeUIAlpha,sl::ResourceLifecycle::eValidUntilPresent}};
    // Original entry avoids our observation hooks. Optional tags stay cleared for
    // this frame: CPU Present return does not end asynchronous provider use.
    return provider==adapter.Generation()&&o_slSetTagForFrame(*token,sl::ViewportHandle(viewport),tags,3,nullptr)==sl::Result::eOk;
}

void StreamlineHooks::unhookInterposer()
{
    VulkanHooks::UnhookApplicationInterposer();
    LOG_FUNC();
    DlssNr::PreFg::Streamline::Uninstall();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if(nrFeatureHooks)
    {
        if(o_slIsFeatureSupported)DetourDetach(&(PVOID&)o_slIsFeatureSupported,hkslIsFeatureSupported);
        if(o_slIsFeatureLoaded)DetourDetach(&(PVOID&)o_slIsFeatureLoaded,hkslIsFeatureLoaded);
        if(o_slGetFeatureRequirements)DetourDetach(&(PVOID&)o_slGetFeatureRequirements,hkslGetFeatureRequirements);
        if(o_slGetFeatureVersion)DetourDetach(&(PVOID&)o_slGetFeatureVersion,hkslGetFeatureVersion);
    }
    if(nrFunctionHook&&o_slGetFeatureFunction)DetourDetach(&(PVOID&)o_slGetFeatureFunction,hkslGetFeatureFunction);
    if(nrDeviceHook&&o_slSetD3DDevice)DetourDetach(&(PVOID&)o_slSetD3DDevice,hkslSetD3DDevice);
    if(o_slSetFeatureLoaded)DetourDetach(&(PVOID&)o_slSetFeatureLoaded,hkslSetFeatureLoaded);

    if (o_slSetTag)
        DetourDetach(&(PVOID&) o_slSetTag, hkslSetTag);

    if (o_slSetTagForFrame)
        DetourDetach(&(PVOID&) o_slSetTagForFrame, hkslSetTagForFrame);

    if (o_slSetConstants)
        DetourDetach(&(PVOID&) o_slSetConstants, hkslSetConstants);

    if (o_slEvaluateFeature)
        DetourDetach(&(PVOID&) o_slEvaluateFeature, hkslEvaluateFeature);
    if (o_slAllocateResources && Neurotic::Mfg::Experimental::Requested())
        DetourDetach(&(PVOID&) o_slAllocateResources, hkslAllocateResources);

    if (o_slInit)
        DetourDetach(&(PVOID&) o_slInit, hkslInit);

    if (o_slInit_sl1)
        DetourDetach(&(PVOID&) o_slInit_sl1, hkslInit_sl1);

    if (o_slSetTag_sl1)
        DetourDetach(&(PVOID&) o_slSetTag_sl1, hkslSetTag_sl1);

    if (o_slSetConstants_interposer_sl1)
        DetourDetach(&(PVOID&) o_slSetConstants_interposer_sl1, hkslSetConstants_sl1);

    if (o_slEvaluateFeature_sl1)
        DetourDetach(&(PVOID&) o_slEvaluateFeature_sl1, hkslEvaluateFeature_sl1);

    // if (o_logCallback)
    //     DetourDetach(&(PVOID&) o_logCallback, streamlineLogCallback);
    // else if (o_logCallback_sl1)
    //     DetourDetach(&(PVOID&) o_logCallback_sl1, streamlineLogCallback);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("DetourTransactionCommit error: {:X}", detourResult);
    }
    else
    {
        nrFeatureHooks=nrDeviceHook=nrFgOverrides=nrFunctionHook=false;nrObservedInterposer=nullptr;
        o_slInit = nullptr;
        o_slSetFeatureLoaded = nullptr;
        o_slInit_sl1 = nullptr;
        o_slSetTag = nullptr;
        o_slSetTagForFrame = nullptr;
        o_slEvaluateFeature = nullptr;
        o_slSetConstants = nullptr;
        o_slSetTag_sl1 = nullptr;
        o_slSetConstants_interposer_sl1 = nullptr;
        o_slEvaluateFeature_sl1 = nullptr;
        o_logCallback = nullptr;
        o_logCallback_sl1 = nullptr;
    }
}

// Call it just after sl.interposer's load or if sl.interposer is already loaded
void StreamlineHooks::hookInterposer(HMODULE slInterposer)
{
    LOG_FUNC();

    if (!slInterposer)
    {
        LOG_WARN("Streamline module in NULL");
        return;
    }

    // Interposer needs this or it might end in an infinite loop calling itself
    static HMODULE last_slInterposer = nullptr;

    if (last_slInterposer == slInterposer)
        return;

    last_slInterposer = slInterposer;

    // Looks like when reading DLL version load methods are called
    // To prevent loops disabling checks for sl.interposer.dll
    auto owner = State::GetOwner();
    State::DisableChecks(owner, "sl.interposer");

    if (o_slSetTag || o_slInit || o_slInit_sl1 || o_slSetTag_sl1 || o_slSetConstants_interposer_sl1 ||
        o_slEvaluateFeature_sl1)
        unhookInterposer();

    {
        char dllPath[MAX_PATH];
        GetModuleFileNameA(slInterposer, dllPath, MAX_PATH);

        LOG_TRACE("slInterposer path: {}", dllPath);

        version_t sl_version;
        Util::GetFileVersion(string_to_wstring(dllPath), &sl_version);

        State::Instance().streamlineVersion.major = sl_version.major;
        State::Instance().streamlineVersion.minor = sl_version.minor;
        State::Instance().streamlineVersion.patch = sl_version.patch;

        LOG_INFO("Streamline version: {}.{}.{}", sl_version.major, sl_version.minor, sl_version.patch);

        if (sl_version.major >= 2)
        {
            o_slSetTag =
                reinterpret_cast<decltype(&slSetTag)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetTag"));
            o_slSetTagForFrame = reinterpret_cast<decltype(&slSetTagForFrame)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetTagForFrame"));
            o_slInit = reinterpret_cast<decltype(&slInit)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slInit"));
            o_slEvaluateFeature = reinterpret_cast<decltype(&slEvaluateFeature)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slEvaluateFeature"));
            o_slAllocateResources = reinterpret_cast<decltype(&slAllocateResources)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slAllocateResources"));
            o_slSetConstants = reinterpret_cast<decltype(&slSetConstants)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetConstants"));
            o_slGetNativeInterface = reinterpret_cast<decltype(&slGetNativeInterface)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetNativeInterface"));
            o_slSetD3DDevice = reinterpret_cast<decltype(&slSetD3DDevice)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetD3DDevice"));
            o_slGetNewFrameToken = reinterpret_cast<decltype(&slGetNewFrameToken)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetNewFrameToken")); // Not hooked

            // For making the game think DLSSG is loaded and supported
            // but making SL not actually load the plugin
            o_slIsFeatureSupported = reinterpret_cast<decltype(&slIsFeatureSupported)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slIsFeatureSupported"));
            o_slIsFeatureLoaded = reinterpret_cast<decltype(&slIsFeatureLoaded)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slIsFeatureLoaded"));
            o_slSetFeatureLoaded = reinterpret_cast<decltype(&slSetFeatureLoaded)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetFeatureLoaded"));
            o_slGetFeatureRequirements = reinterpret_cast<decltype(&slGetFeatureRequirements)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetFeatureRequirements"));
            o_slGetFeatureVersion = reinterpret_cast<decltype(&slGetFeatureVersion)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetFeatureVersion"));
            o_slGetFeatureFunction = reinterpret_cast<decltype(&slGetFeatureFunction)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetFeatureFunction"));

            if (o_slInit != nullptr)
            {
                const bool observeProvenance=RP::Enabled();
                const bool fgOverrides=State::Instance().activeFgInput==FGInput::DLSSG;
                LOG_TRACE("Hooking v2");
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                DetourAttach(&(PVOID&) o_slInit, hkslInit);
                // Public FG unload is authoritative even when replacement FG and
                // optional provenance logging are disabled.
                if (o_slSetFeatureLoaded != nullptr)
                    DetourAttach(&(PVOID&) o_slSetFeatureLoaded, hkslSetFeatureLoaded);

                if (o_slEvaluateFeature != nullptr)
                    DetourAttach(&(PVOID&) o_slEvaluateFeature, hkslEvaluateFeature);

                // Native game FG uses these public calls too. The later pre-FG
                // ledger hook chains through this observer, so Vulkan tags and
                // option/state functions must not depend on replacement FG.
                if (o_slSetTagForFrame != nullptr)
                    DetourAttach(&(PVOID&) o_slSetTagForFrame, hkslSetTagForFrame);
                if (o_slGetFeatureFunction != nullptr)
                    DetourAttach(&(PVOID&) o_slGetFeatureFunction, hkslGetFeatureFunction);

                if (State::Instance().activeFgInput == FGInput::NvngxFG ||
                    State::Instance().activeFgInput == FGInput::DLSSG)
                {
                    if (o_slSetTag != nullptr)
                        DetourAttach(&(PVOID&) o_slSetTag, hkslSetTag);

                    if (o_slSetConstants != nullptr)
                        DetourAttach(&(PVOID&) o_slSetConstants, hkslSetConstants);
                }

                if (fgOverrides || observeProvenance)
                {
                    if (o_slIsFeatureSupported != nullptr)
                        DetourAttach(&(PVOID&) o_slIsFeatureSupported, hkslIsFeatureSupported);

                    if (o_slIsFeatureLoaded != nullptr)
                        DetourAttach(&(PVOID&) o_slIsFeatureLoaded, hkslIsFeatureLoaded);

                    if (o_slGetFeatureRequirements != nullptr)
                        DetourAttach(&(PVOID&) o_slGetFeatureRequirements, hkslGetFeatureRequirements);

                    if (o_slGetFeatureVersion != nullptr)
                        DetourAttach(&(PVOID&) o_slGetFeatureVersion, hkslGetFeatureVersion);

                }

                if (o_slAllocateResources != nullptr && Neurotic::Mfg::Experimental::Requested())
                    DetourAttach(&(PVOID&) o_slAllocateResources, hkslAllocateResources);

                // if (o_slGetNativeInterface != nullptr)
                //     DetourAttach(&(PVOID&) o_slGetNativeInterface, hkslGetNativeInterface);

                if (o_slSetD3DDevice != nullptr)
                    DetourAttach(&(PVOID&) o_slSetD3DDevice, hkslSetD3DDevice);

                // Publish hook-visible policy before patched entry points are exposed.
                const auto previousOverrides=nrFgOverrides.exchange(fgOverrides);
                const auto previousInterposer=nrObservedInterposer.exchange(slInterposer);
                auto detourResult = DetourTransactionCommit();
                if (detourResult != NO_ERROR)
                {
                    nrFgOverrides=previousOverrides;nrObservedInterposer=previousInterposer;
                    LOG_ERROR("Failed to hook sl.interposer v2: {:X}", detourResult);
                    o_slSetTag = nullptr;
                    o_slSetTagForFrame = nullptr;
                    o_slInit = nullptr;
                    o_slEvaluateFeature = nullptr;
                    o_slAllocateResources = nullptr;
                    o_slSetConstants = nullptr;
                    o_slGetNativeInterface = nullptr;
                    o_slSetD3DDevice = nullptr;
                    o_slIsFeatureSupported = nullptr;
                    o_slIsFeatureLoaded = nullptr;
                    o_slSetFeatureLoaded = nullptr;
                    o_slGetFeatureRequirements = nullptr;
                    o_slGetFeatureVersion = nullptr;
                    o_slGetFeatureFunction = nullptr;
                }
                else
                {
                    nrFeatureHooks=fgOverrides||observeProvenance;
                    nrFunctionHook=o_slGetFeatureFunction!=nullptr;
                    nrDeviceHook=o_slSetD3DDevice!=nullptr;
                    if(observeProvenance)try
                    {
                        RP::LastError preserve;
                        if(NrReserveRuntimeEvent())NrRuntimeEvent({{"schema","NeuRotic.RuntimeProvenance/1"},{"stage","L0"},{"api","hookInterposer"},
                            {"phase","observed"},{"process",RP::Process()},{"interposer",RP::Module(slInterposer)},
                            {"neurotic",RP::Address(reinterpret_cast<const void*>(&NrPreferences))}});
                    }catch(...){}
                    DlssNr::PreFg::Streamline::Install(slInterposer);
                    VulkanHooks::HookApplicationInterposer(slInterposer);
                }
            }
        }
        else if (sl_version.major == 1)
        {
            o_slInit_sl1 =
                reinterpret_cast<decltype(&sl1::slInit)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slInit"));
            o_slSetTag_sl1 = reinterpret_cast<decltype(&sl1::slSetTag)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetTag"));
            o_slSetConstants_interposer_sl1 = reinterpret_cast<decltype(&sl1::slSetConstants)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetConstants"));
            o_slEvaluateFeature_sl1 = reinterpret_cast<decltype(&sl1::slEvaluateFeature)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slEvaluateFeature"));

            LOG_INFO("SL1 exports - slInit: {}, slSetTag: {}, slSetConstants: {}, slEvaluateFeature: {}",
                     o_slInit_sl1 != nullptr, o_slSetTag_sl1 != nullptr, o_slSetConstants_interposer_sl1 != nullptr,
                     o_slEvaluateFeature_sl1 != nullptr);

            if (o_slInit_sl1 || o_slSetTag_sl1 || o_slSetConstants_interposer_sl1 || o_slEvaluateFeature_sl1)
            {
                LOG_TRACE("Hooking v1");
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (o_slInit_sl1)
                    DetourAttach(&(PVOID&) o_slInit_sl1, hkslInit_sl1);

                if (IsSL1AndFGActive())
                {
                    if (o_slSetTag_sl1)
                        DetourAttach(&(PVOID&) o_slSetTag_sl1, hkslSetTag_sl1);

                    if (o_slSetConstants_interposer_sl1)
                        DetourAttach(&(PVOID&) o_slSetConstants_interposer_sl1, hkslSetConstants_sl1);

                    if (o_slEvaluateFeature_sl1)
                        DetourAttach(&(PVOID&) o_slEvaluateFeature_sl1, hkslEvaluateFeature_sl1);
                }

                auto detourResult = DetourTransactionCommit();
                if (detourResult != NO_ERROR)
                {
                    LOG_ERROR("Failed to hook sl.interposer v1: {:X}", detourResult);
                    o_slInit_sl1 = nullptr;
                    o_slSetTag_sl1 = nullptr;
                    o_slSetConstants_interposer_sl1 = nullptr;
                    o_slEvaluateFeature_sl1 = nullptr;
                }
            }
        }
    }

    State::EnableChecks(owner);
}

// SL DLSS

void StreamlineHooks::unhookDlss()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_dlss_slGetPluginFunction)
        DetourDetach(&(PVOID&) o_dlss_slGetPluginFunction, hkdlss_slGetPluginFunction);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook DLSS: {:X}", detourResult);
    }
    else
    {
        o_dlss_slGetPluginFunction = nullptr;
    }
}

void StreamlineHooks::hookDlss(HMODULE slDlss)
{
    LOG_FUNC();

    if (!slDlss)
    {
        LOG_WARN("Dlss module in NULL");
        return;
    }

    if (o_dlss_slGetPluginFunction)
        unhookDlss();

    o_dlss_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slDlss, "slGetPluginFunction"));

    if (o_dlss_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.dlss");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_dlss_slGetPluginFunction, hkdlss_slGetPluginFunction);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook DLSS: {:X}", detourResult);
            o_dlss_slGetPluginFunction = nullptr;
        }
    }
}

// SL DLSSG

void StreamlineHooks::unhookDlssg()
{
    LOG_FUNC();
    mfgSelectedWrapper = nullptr;
    const auto generation = nrMfgHookGeneration.fetch_add(1) + 1;
    nrMfgRequests.Invalidate(generation);
    Neurotic::Semantic::Character::NativeFgWork().Reset();
    Neurotic::Mfg::InvalidateAdaMfg(generation);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_dlssg_slGetPluginFunction)
        DetourDetach(&(PVOID&) o_dlssg_slGetPluginFunction, hkdlssg_slGetPluginFunction);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook DLSSG: {:X}", detourResult);
        o_dlssg_slGetPluginFunction = nullptr;
    }
}

void StreamlineHooks::hookDlssg(HMODULE slDlssg)
{
    LOG_FUNC();

    if (!slDlssg)
    {
        LOG_WARN("Dlssg module in NULL");
        return;
    }

    if (o_dlssg_slGetPluginFunction)
        unhookDlssg();

    mfgSelectedWrapper = slDlssg;
    const auto generation = nrMfgHookGeneration.fetch_add(1) + 1;
    nrMfgRequests.Invalidate(generation);
    Neurotic::Semantic::Character::NativeFgWork().Reset();
    Neurotic::Mfg::InvalidateAdaMfg(generation);

    o_dlssg_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slDlssg, "slGetPluginFunction"));

    if (o_dlssg_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.dlssg");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_dlssg_slGetPluginFunction, hkdlssg_slGetPluginFunction);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook DLSSG: {:X}", detourResult);
            o_dlssg_slGetPluginFunction = nullptr;
        }
    }
}

// Local SL DLSSG

void StreamlineHooks::unhookLocalDlssg()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_local_dlssg_slGetPluginFunction)
    {
        DetourDetach(&(PVOID&) o_local_dlssg_slGetPluginFunction, hklocal_dlssg_slGetPluginFunction);
        o_local_dlssg_slGetPluginFunction = nullptr;
    }

    DetourTransactionCommit();
}

void StreamlineHooks::hookLocalDlssg(HMODULE slDlssg)
{
    LOG_FUNC();

    if (!slDlssg)
    {
        LOG_WARN("Dlssg module in NULL");
        return;
    }

    if (o_local_dlssg_slGetPluginFunction)
        unhookLocalDlssg();

    o_local_dlssg_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slDlssg, "slGetPluginFunction"));

    if (o_local_dlssg_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in local sl.dlssg");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_local_dlssg_slGetPluginFunction, hklocal_dlssg_slGetPluginFunction);

        DetourTransactionCommit();
    }
}

// SL REFLEX

void StreamlineHooks::unhookReflex()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_reflex_slGetPluginFunction)
        DetourDetach(&(PVOID&) o_reflex_slGetPluginFunction, hkreflex_slGetPluginFunction);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook Reflex: {:X}", detourResult);
    }
    else
    {
        o_reflex_slGetPluginFunction = nullptr;
    }
}

void StreamlineHooks::hookReflex(HMODULE slReflex)
{
    LOG_FUNC();

    if (!slReflex)
    {
        LOG_WARN("Reflex module in NULL");
        return;
    }

    if (o_reflex_slGetPluginFunction)
        unhookReflex();

    o_reflex_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slReflex, "slGetPluginFunction"));

    if (o_reflex_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.reflex");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_reflex_slGetPluginFunction, hkreflex_slGetPluginFunction);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook Reflex: {:X}", detourResult);
            o_reflex_slGetPluginFunction = nullptr;
        }
    }
}

// SL PCL

void StreamlineHooks::unhookPcl()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_pcl_slGetPluginFunction)
        DetourDetach(&(PVOID&) o_pcl_slGetPluginFunction, hkpcl_slGetPluginFunction);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook PCL: {:X}", detourResult);
    }
    else
    {
        o_pcl_slGetPluginFunction = nullptr;
    }
}

void StreamlineHooks::hookPcl(HMODULE slPcl)
{
    LOG_FUNC();

    if (!slPcl)
    {
        LOG_WARN("Pcl module in NULL");
        return;
    }

    if (o_pcl_slGetPluginFunction)
        unhookPcl();

    o_pcl_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slPcl, "slGetPluginFunction"));

    if (o_pcl_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.pcl");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_pcl_slGetPluginFunction, hkpcl_slGetPluginFunction);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook PCL: {:X}", detourResult);
            o_pcl_slGetPluginFunction = nullptr;
        }
    }
}

// SL COMMON

void StreamlineHooks::unhookCommon()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_common_slGetPluginFunction)
        DetourDetach(&(PVOID&) o_common_slGetPluginFunction, hkcommon_slGetPluginFunction);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook Common: {:X}", detourResult);
    }
    else
    {
        systemCaps = nullptr;
        systemCapsSl15 = nullptr;
        o_common_slGetPluginFunction = nullptr;
    }
}

void StreamlineHooks::hookCommon(HMODULE slCommon)
{
    LOG_FUNC();

    if (!slCommon)
    {
        LOG_WARN("Common module in NULL");
        return;
    }

    if (o_common_slGetPluginFunction)
        unhookCommon();

    o_common_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slCommon, "slGetPluginFunction"));

    if (o_common_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.common");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_common_slGetPluginFunction, hkcommon_slGetPluginFunction);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook Common: {:X}", detourResult);
            o_common_slGetPluginFunction = nullptr;
        }
    }
}

bool StreamlineHooks::isInterposerHooked() { return o_slInit != nullptr || o_slInit_sl1 != nullptr; }

bool StreamlineHooks::isDlssHooked() { return o_dlss_slGetPluginFunction != nullptr; }

bool StreamlineHooks::isDlssgHooked() { return o_dlssg_slGetPluginFunction != nullptr; }

bool StreamlineHooks::isLocalDlssgHooked() { return o_local_dlssg_slGetPluginFunction != nullptr; }

bool StreamlineHooks::isCommonHooked() { return o_common_slGetPluginFunction != nullptr; }

bool StreamlineHooks::isPclHooked() { return o_pcl_slGetPluginFunction != nullptr; }

bool StreamlineHooks::isReflexHooked() { return o_reflex_slGetPluginFunction != nullptr; }
