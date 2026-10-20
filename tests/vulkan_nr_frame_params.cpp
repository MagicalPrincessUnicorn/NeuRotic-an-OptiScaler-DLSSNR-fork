#include "../OptiScaler/dlssnr/VulkanNrFrameParams.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <variant>

using namespace DlssNr;
static void Check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// Use the actual SDK virtual interface, not an interchangeable pointer/integer mock.
struct StrictParams final : NVSDK_NGX_Parameter
{
    using Value = std::variant<unsigned long long, float, double, unsigned int, int,
                               ID3D11Resource*, ID3D12Resource*, void*>;
    std::map<std::string, Value> values;
    std::map<std::string, size_t> writes;
    enum class Fault { None, Missing, WrongType, Mismatch } fault = Fault::None;
    std::string faultKey;
    template<class T> void Store(const char* key, T value)
    {
        values[key] = value;
        ++writes[key];
    }
    template<class T> NVSDK_NGX_Result Read(const char* key, T* out) const
    {
        if (key == faultKey && (fault == Fault::Missing || fault == Fault::WrongType))
            return NVSDK_NGX_Result_FAIL_UnsupportedParameter;
        const auto it = values.find(key);
        if (it == values.end() || !std::holds_alternative<T>(it->second))
            return NVSDK_NGX_Result_FAIL_UnsupportedParameter;
        *out = std::get<T>(it->second);
        if (key == faultKey && fault == Fault::Mismatch)
        {
            if constexpr (std::is_pointer_v<T>) *out = reinterpret_cast<T>(uintptr_t(0xDEAD));
            else *out += T(1);
        }
        return NVSDK_NGX_Result_Success;
    }
#define PARAM_TYPE(T) \
    void Set(const char* key, T value) override { Store(key, value); } \
    NVSDK_NGX_Result Get(const char* key, T* out) const override { return Read(key, out); }
    PARAM_TYPE(unsigned long long)
    PARAM_TYPE(float)
    PARAM_TYPE(double)
    PARAM_TYPE(unsigned int)
    PARAM_TYPE(int)
    PARAM_TYPE(ID3D11Resource*)
    PARAM_TYPE(ID3D12Resource*)
    PARAM_TYPE(void*)
#undef PARAM_TYPE
    void Reset() override { values.clear(); writes.clear(); }
};

static NVSDK_NGX_Resource_VK Image(uint32_t width, uint32_t height, bool writable)
{
    NVSDK_NGX_Resource_VK image{};
    image.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    image.ReadWrite = writable;
    auto& info = image.Resource.ImageViewInfo;
    info.Image = (VkImage) uintptr_t(0x1234);
    info.ImageView = (VkImageView) uintptr_t(0x5678);
    info.Width = width; info.Height = height;
    info.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
    info.SubresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    return image;
}

struct Fixture
{
    StrictParams params, gameParams;
    VkTuning::Settings applied{0, 0, .5f, 1.25f, .75f, -1.f, true};
    NVSDK_NGX_Resource_VK color = Image(3440, 1440, false), depth = Image(2293, 960, false),
        motion = Image(2293, 960, false), output = Image(3440, 1440, true);
    VkFrame::Inputs frame{&color, &depth, &motion, &output, 3440, 1440, 2293, 960, true, true};
    VkFrame::Failure failure;
    unsigned calls = 0;
    Fixture()
    {
        Check(VkTuning::Prepare(static_cast<NVSDK_NGX_Parameter*>(&params), applied, 3440, 1440), "prepare creation");
        gameParams.Set("RR.Output", static_cast<void*>(&output));
        gameParams.Set("OutWidth", 2293u);
    }
    bool Run()
    {
        return VkFrame::PrepareAndEvaluate(static_cast<NVSDK_NGX_Parameter*>(&params), frame, applied,
                                          failure, [&] { ++calls; });
    }
};

static void TypedResourcesAndPreservation()
{
    Fixture f;
    const auto creation = f.params.values;
    const auto creationWrites = f.params.writes;
    const auto game = f.gameParams.values;
    const auto gameWrites = f.gameParams.writes;
    NVSDK_NGX_Parameter* params = &f.params;
    for (const auto& b : f.frame.Bindings())
    {
        // This is the previous four resource writes: same bits, wrong NGX value type.
        params->Set(b.key, (unsigned long long) (uintptr_t) b.resource);
        VkFrame::Failure failure;
        Check(!VkFrame::ReadEquals(params, b.key, static_cast<void*>(b.resource), failure),
              "old integer setter must fail pointer readback");
    }
    Check(f.Run() && f.calls == 1, "corrected pointer preparation permits model call");
    for (const auto& b : f.frame.Bindings())
    {
        Check(std::holds_alternative<void*>(f.params.values.at(b.key)), "resource stored as void pointer");
        Check(std::get<void*>(f.params.values.at(b.key)) == b.resource, "exact resource identity retained");
        Check(std::get<unsigned int>(f.params.values.at(std::string(b.key) + "SubrectWidth")) == b.width,
              "resource-specific subrect width");
    }
    for (const auto& [key, value] : creation)
    {
        Check(f.params.values.at(key) == value, "creation settings unchanged");
        Check(f.params.writes.at(key) == creationWrites.at(key), "creation settings never rewritten");
    }
    Check(f.gameParams.values == game && f.gameParams.writes == gameWrites, "RR parameters untouched");
    Check(std::get<unsigned int>(f.params.values.at("DLSSNR.Reset")) == 1u, "initial reset retained");
    Check(std::get<float>(f.params.values.at("DLSSNR.MVecScaleX")) == 1.f, "existing motion scale retained");
    f.frame.reset = false;
    Check(f.Run() && f.calls == 2 && std::get<unsigned int>(f.params.values.at("DLSSNR.Reset")) == 0u,
          "per-frame reset update");
}

static void DynamicGuidesOnRetainedParameters()
{
    Fixture f;
    const auto creation=f.params.values;
    const auto creationWrites=f.params.writes;
    for(const auto extent:{VkExtent2D{2139,1204},{1921,1081},{1996,1123}}) {
        auto depth=Image(extent.width,extent.height,false), motion=depth;
        f.frame.depth=&depth;f.frame.motion=&motion;
        VkNrTemporalMetadata t;t.color={0,0,3440,1440};
        t.depth=t.motion={0,0,extent.width,extent.height};
        t.motionScaleX=3440.f/extent.width;t.motionScaleY=1440.f/extent.height;
        f.frame.temporal=t;f.frame.reset=false;
        Check(f.Run(),"changing live guides permits inference on retained parameters");
        Check(std::get<unsigned int>(f.params.values.at("DLSSNR.DepthSubrectWidth"))==extent.width&&
              std::get<unsigned int>(f.params.values.at("DLSSNR.MVecSubrectHeight"))==extent.height&&
              std::get<float>(f.params.values.at("DLSSNR.MVecScaleX"))==t.motionScaleX&&
              std::get<void*>(f.params.values.at("DLSSNR.Depth"))==&depth,
              "each inference receives current guide resource, dimensions and motion scale");
        for(const auto& [key,value]:creation)
            Check(f.params.values.at(key)==value&&f.params.writes.at(key)==creationWrites.at(key),
                  "dynamic guides do not rewrite model creation parameters");
        ++f.frame.temporal->depth.width;
        const auto calls=f.calls;
        Check(!f.Run()&&f.calls==calls,"retained model does not permit out-of-bounds current guides");
    }
}
static void ReadbackFaults()
{
    for (int index = 0; index < 4; ++index)
        for (auto fault : {StrictParams::Fault::Missing, StrictParams::Fault::WrongType, StrictParams::Fault::Mismatch})
        {
            Fixture f;
            f.params.faultKey = f.frame.Bindings()[index].key;
            f.params.fault = fault;
            Check(!f.Run() && f.calls == 0, "bad pointer readback blocks evaluation");
            Check(f.failure.key == f.params.faultKey, "bad resource key identified");
            if (fault == StrictParams::Fault::Mismatch)
                Check(f.failure.expectedPointer != f.failure.observedPointer, "pointer mismatch evidence");
        }
    for (const char* key : {"DLSSNR.Intensity", "DLSSNR.Width", "DLSSNR.Reset", "DLSSNR.DepthInverted",
                           "DLSSNR.ColorSubrectWidth", "DLSSNR.MVecSubrectHeight", "DLSSNR.MVecScaleY"})
    {
        Fixture f;
        f.params.faultKey = key; f.params.fault = StrictParams::Fault::Mismatch;
        Check(!f.Run() && !f.calls && f.failure.key == key, "scalar mismatch blocks evaluation");
    }
    Fixture absent;
    Check(!VkFrame::PrepareAndEvaluate<NVSDK_NGX_Parameter>(nullptr, absent.frame, absent.applied, absent.failure,
                                                         [&] { ++absent.calls; }) && !absent.calls,
          "missing parameter block blocks evaluation");
}

static void ProjectedGuideBindings()
{
    for(const bool low:{true,false})
    {
        Fixture f;
        f.color=Image(200,160,false);f.output=Image(200,160,true);
        f.depth=Image(104,86,false);f.motion=Image(low?102u:202u,low?83u:163u,false);
        f.frame.width=200;f.frame.height=160;
        Check(VkTuning::Prepare(static_cast<NVSDK_NGX_Parameter*>(&f.params),f.applied,200,160),"prepare projected frame creation");
        f.gameParams.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,100u);
        f.gameParams.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,80u);
        f.gameParams.Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X,4u);
        f.gameParams.Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y,6u);
        f.gameParams.Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X,2u);
        f.gameParams.Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y,3u);
        f.gameParams.Set(NVSDK_NGX_Parameter_MV_Scale_X,2.f);
        f.gameParams.Set(NVSDK_NGX_Parameter_MV_Scale_Y,-3.f);
        f.gameParams.Set(NVSDK_NGX_Parameter_Jitter_Offset_X,.5f);
        f.gameParams.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,-.25f);
        f.frame.temporal=VkFrame::ObserveTemporal(static_cast<NVSDK_NGX_Parameter*>(&f.gameParams),
            low?NVSDK_NGX_DLSS_Feature_Flags_MVLowRes:0u,7,11,{200,160});
        Check(f.frame.temporal&&f.Run()&&f.calls==1,"observed low/high guides reach exactly one model call");
        const auto value=[&](const char* key){return std::get<unsigned int>(f.params.values.at(key));};
        Check(value("DLSSNR.DepthSubrectBaseX")==4&&value("DLSSNR.DepthSubrectBaseY")==6&&
            value("DLSSNR.DepthSubrectWidth")==100&&value("DLSSNR.DepthSubrectHeight")==80&&
            value("DLSSNR.MVecSubrectBaseX")==2&&value("DLSSNR.MVecSubrectBaseY")==3&&
            value("DLSSNR.MVecSubrectWidth")==unsigned(low?100:200)&&
            value("DLSSNR.MVecSubrectHeight")==unsigned(low?80:160),
            "independent observed origins and motion resolution reach actual SDK parameter writes");
        const auto writes=f.params.writes;
        --f.motion.Resource.ImageViewInfo.Width;
        Check(!f.Run()&&f.calls==1&&f.params.writes==writes,
            "strict Vulkan origin plus extent overflow refuses before writes, without D3D compose clamp");
    }
}

static void WrapperFaults()
{
    Fixture active;
    VkNrTemporalMetadata area;area.color={8,12,1706,960};
    const auto activeRect=VkFrame::SourceRect(&active.color,area,true,active.failure);
    Check(activeRect&&activeRect->x==8&&activeRect->y==12&&activeRect->width==1706&&activeRect->height==960,
        "Pre-SR encodes the active render subrect instead of its larger allocation");
    Check(VkFrame::SourceRect(&active.color,area,false,active.failure)->width==3440,
        "post-SR continues using full output");
    area.color.x=UINT32_MAX;
    Check(!VkFrame::SourceRect(&active.color,area,true,active.failure),"out-of-allocation active input refuses before sampling");
    for (int index = 0; index < 4; ++index)
        for (int defect = 0; defect < 8; ++defect)
        {
            Fixture f;
            auto binding = f.frame.Bindings()[index];
            auto& resource = *binding.resource;
            auto& image = resource.Resource.ImageViewInfo;
            switch (defect)
            {
            case 0: resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_BUFFER; break;
            case 1: image.Image = VK_NULL_HANDLE; break;
            case 2: image.ImageView = VK_NULL_HANDLE; break;
            case 3: image.Width = 0; break;
            case 4: image.Height = 0; break;
            case 5: image.Width = binding.width - 1; break;
            case 6: image.SubresourceRange.layerCount = 0; break;
            case 7: image.Format = VK_FORMAT_UNDEFINED; break;
            }
            const auto writes = f.params.writes;
            Check(!f.Run() && !f.calls && f.failure.key == binding.key, "invalid wrapper blocks evaluation");
            Check(f.params.writes == writes, "all wrappers validated before parameter writes");
        }
    for (int index = 0; index < 4; ++index)
    {
        Fixture f;
        switch(index) { case 0: f.frame.color=nullptr; break; case 1: f.frame.depth=nullptr; break;
                        case 2: f.frame.motion=nullptr; break; case 3: f.frame.output=nullptr; break; }
        Check(!f.Run() && !f.calls, "null wrapper blocks evaluation");
    }
    Fixture readOnly;
    readOnly.output.ReadWrite = false;
    Check(!readOnly.Run() && !readOnly.calls, "read-only model output rejected");
    Fixture rect;
    Check(!VkFrame::ValidateRect("overflow", &rect.color, UINT32_MAX, 0, 2, 2, false, rect.failure),
          "overflowing subrect rejected");
    Check(!VkFrame::ValidateRect("height", &rect.color, 0, 1440, 2, 2, false, rect.failure),
          "subrect base beyond remaining height rejected");
    Check(!VkFrame::ValidateRect("empty", &rect.color, 0, 0, 0, 2, false, rect.failure), "empty subrect rejected");
}

static void TemporalMetadata()
{
    Fixture f;
    VkNrTemporalMetadata metadata;
    metadata.color = {0,0,3440,1440};
    metadata.depth = {4,6,2280,944}; metadata.motion = {8,12,2272,936};
    metadata.motionScaleX = .3f; metadata.motionScaleY = -.4f;
    metadata.jitterX = .5f; metadata.jitterY = -.75f;
    f.frame.temporal = metadata;
    Check(f.Run(), "typed temporal metadata permits evaluation");
    Check(std::get<unsigned int>(f.params.values.at("DLSSNR.DepthSubrectBaseX")) == 4,
          "active depth origin reaches model");
    Check(std::get<unsigned int>(f.params.values.at("DLSSNR.MVecSubrectBaseY")) == 12,
          "active motion origin reaches model");
    Check(std::get<unsigned int>(f.params.values.at("DLSSNR.MVecSubrectWidth")) == 2272,
          "independent active motion raster reaches model");
    Check(std::get<float>(f.params.values.at("DLSSNR.MVecScaleX")) == .3f &&
          std::get<float>(f.params.values.at("DLSSNR.MVecScaleY")) == -.4f, "actual per-axis motion scale");
    Check(std::get<float>(f.params.values.at("Jitter.Offset.X")) == .5f &&
          std::get<float>(f.params.values.at("Jitter.Offset.Y")) == -.75f, "jitter typed and preserved");
    Fixture invalid;
    invalid.frame.temporal = metadata;
    invalid.frame.temporal->depth.x = UINT32_MAX;
    const auto writes = invalid.params.writes;
    Check(!invalid.Run() && !invalid.calls && invalid.params.writes == writes,
          "invalid active region fails before writes or model call");
}

static void ObservedMetadata()
{
    StrictParams game;
    const auto quality = VkFrame::ObserveSrQuality(&game,static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxPerf));
    Check(quality && *quality == static_cast<uint32_t>(NVSDK_NGX_PerfQuality_Value_MaxPerf),
        "separate evaluation block uses authenticated selected feature creation quality");
    Check(!VkFrame::ObserveSrQuality(&game,{}),"unknown SR creation quality is not invented");
    game.Set(NVSDK_NGX_Parameter_PerfQualityValue,static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality));
    Check(VkFrame::ObserveSrQuality(&game,{})==static_cast<uint32_t>(NVSDK_NGX_PerfQuality_Value_MaxQuality),"SDK signed creation quality observed");
    Check(!VkFrame::ObserveSrQuality(&game,-1),"invalid authenticated SR quality is rejected");
    game.Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, 8u);
    game.Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, 12u);
    game.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, 100u);
    game.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, 80u);
    game.Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, 4u);
    game.Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, 6u);
    game.Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, 2u);
    game.Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, 3u);
    game.Set(NVSDK_NGX_Parameter_MV_Scale_X, 2.f);
    game.Set(NVSDK_NGX_Parameter_MV_Scale_Y, -3.f);
    Check(!VkFrame::ObserveTemporal(&game, 0, 7, 11, {200,160}), "missing jitter does not mint metadata");
    game.Set(NVSDK_NGX_Parameter_Jitter_Offset_X, .5f);
    game.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, -.25f);
    const auto before = game.values;
    const auto low = VkFrame::ObserveTemporal(&game, NVSDK_NGX_DLSS_Feature_Flags_MVLowRes, 7, 11, {200,160});
    Check(low.has_value(), "complete game observations produce temporal contract");
    Check(low->color.x == 8 && low->motion.width == 100 && low->depth.height == 80,
          "observed low-resolution guide rectangles");
    Check(low->featureGeneration == 7 && low->frameToken == 11 && low->motionScaleY == -3.f,
          "actual producer identity and scales retained");
    const auto high = VkFrame::ObserveTemporal(&game, 0, 7, 11, {200,160});
    Check(high && high->motion.width == 200 && high->motion.height == 160,
          "full-resolution motion respects creation flags");
    Check(game.values == before, "metadata observation cannot mutate game parameters");
    for (const char* key : {NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,
        NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,
        NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X,
        NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y,
        NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X,
        NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y}) game.values.erase(key);
    const auto omitted = game.values;
    const auto legacy = VkFrame::ObserveTemporal(&game, NVSDK_NGX_DLSS_Feature_Flags_MVLowRes, 7, 12, {200,160});
    Check(legacy && legacy->color.x == 0 && legacy->depth.y == 0 && legacy->motion.x == 0 &&
        legacy->color.width == 100 && legacy->frameToken == 12,
        "SDK optional zero origins allow current render metadata without fabricated dimensions");
    Check(game.values == omitted, "optional SDK defaults do not write game parameters");
    game.values.erase(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width);
    Check(!VkFrame::ObserveTemporal(&game, 0, 7, 12, {200,160}), "missing render dimensions still refuse Match");
}

static std::string Read(const char* path)
{
    std::ifstream file(path); Check(file.good(), path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void SourceGuards()
{
    const auto source = Read("OptiScaler/dlssnr/DlssNrFeature_Vk.cpp");
    const auto start = source.find("static void EvaluateAfterUpscaleVkInternal(");
    const auto end = source.find("VkRecordResult EvaluateAfterUpscaleVk(", start);
    Check(start != std::string::npos && end != std::string::npos, "evaluation boundaries found");
    const auto evaluation = source.substr(start, end - start);
    Check(evaluation.find("++ActiveVk().frames;") != std::string::npos, "successful NR counter retained");
    for (const char* trace : {"phaseLog", "phasePending", "logResources", "VK-NR requested",
                              "VK-NR applied slot", "VK-NR compose recorded"})
        Check(source.find(trace) == std::string::npos, "verbose NR tracing removed");
    for (const char* path : {"OptiScaler/inputs/NVNGX_DLSS_Vk.cpp", "OptiScaler/hooks/Streamline_Hooks.cpp",
                             "OptiScaler/hooks/Vulkan_Hooks.cpp"})
    {
        const auto hooks = Read(path);
        Check(hooks.find("VK-RR-DIAG") == std::string::npos, "temporary hook tracing removed");
        Check(hooks.find("VK-RR-ROUTE") == std::string::npos, "temporary route tracing removed");
        Check(hooks.find("VK-NATIVE") == std::string::npos, "temporary native tracing removed");
    }
    for (const char* forbidden : {"vkDeviceWaitIdle(", "g_vkRuntime.Exports().release(", "DestroyImage(",
                                  "DestroyParameters(", "(unsigned long long) (uintptr_t)"})
        Check(evaluation.find(forbidden) == std::string::npos, forbidden);
    const auto gate = evaluation.find("VkFrame::PrepareAndEvaluate(modelParams, frame, activeModel.settings");
    const auto capturedExport = evaluation.find("const auto evaluate = g_vkRuntime.Exports().evaluate;");
    const auto modelCall = evaluation.find("evaluated = evaluate((void*)cmdBuffer,evaluationFeature,modelParams);");
    Check(gate != std::string::npos && capturedExport < gate && modelCall != std::string::npos && modelCall > gate,
          "production model call uses tested gate and applied tuning");
    Check(evaluation.find("lock.unlock();",gate) < modelCall && evaluation.find("lock.lock();",modelCall) != std::string::npos,
          "control lock released for vendor recording then reacquired");
    const auto initCall = evaluation.find("g_vkRuntime.Exports().init(snippet");
    const auto factory = source.substr(source.find("VkNrGenerationCreation CreateVkResources("),
        source.find("void Transition(") - source.find("VkNrGenerationCreation CreateVkResources("));
    const auto createCall = factory.find("create(command,r.capabilityParams,&r.feature)");
    Check(createCall != std::string::npos && evaluation.rfind("lock.unlock();",initCall) != std::string::npos &&
          factory.rfind("lock->unlock();",createCall) > factory.find("VkTuning::Prepare(r.capabilityParams") &&
          factory.find("lock->lock();",createCall) != std::string::npos,
          "model initialization and creation release control lock without releasing lifecycle serialization");
    Check(evaluation.find("VkFrame::ValidateImage(\"RR.Output\"") <
          evaluation.find("const uint32_t width = colour->Resource.ImageViewInfo.Width"),
          "game wrapper validated before dimension union read");
    Check(evaluation.find("TryNrConfigSnapshot") == std::string::npos,
          "recorder consumes the supplied snapshot rather than recapturing controls");
    const auto adapter = Read("OptiScaler/inputs/NVNGX_DLSS_Vk.cpp");
    const auto capture = adapter.find("auto nrSettings = TryNrConfigSnapshot");
    const auto selectedEvaluate = adapter.find("deviceContext->Evaluate(InCmdList, InParameters)", capture);
    Check(capture != std::string::npos && selectedEvaluate != std::string::npos && capture < selectedEvaluate,
          "snapshot_does_not_mix_route_and_resolution: capture precedes selected evaluation");
    Check(adapter.find("&*nrSettings, nrTemporal ? &*nrTemporal : nullptr", selectedEvaluate) != std::string::npos,
          "selected adapter forwards the captured configuration and metadata");
}

static void NativeInputObservations()
{
    StrictParams params;NrPreflightSignals::NativeInputs report;
    params.Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.25f);
    params.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,-0.5f);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_X,1920.0f);
    params.Set(NVSDK_NGX_Parameter_MV_Scale_Y,1080.0f);
    params.Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,2.0f);
    params.Set(NVSDK_NGX_Parameter_ExposureTexture,reinterpret_cast<void*>(uintptr_t(9)));
    params.writes.clear();
    VkFrame::ObserveNativeInputs(report,&params);
    Check(report.observations==1&&report.jitter&&report.jitter->x==0.25f&&report.jitter->y==-0.5f&&
        report.motionScale&&report.motionScale->x==1920.0f&&report.motionScale->y==1080.0f&&
        report.preExposure==2.0f&&report.exposureTexture&&params.writes.empty(),
        "Vulkan Signal Status reads actual raw inputs without changing game parameters");
    params.values.erase(NVSDK_NGX_Parameter_MV_Scale_Y);
    params.values.erase(NVSDK_NGX_Parameter_DLSS_Pre_Exposure);
    params.Set(NVSDK_NGX_Parameter_Jitter_Offset_X,std::numeric_limits<float>::quiet_NaN());
    VkFrame::ObserveNativeInputs(report,&params);
    Check(report.observations==2&&!report.jitter&&report.jitterSupplied&&!report.motionScale&&
        report.motionScaleSupplied&&!report.preExposure&&!report.preExposureSupplied,
        "missing or invalid current inputs replace old observations instead of fabricating defaults");
}

int main()
{
    NativeInputObservations();
    TypedResourcesAndPreservation(); DynamicGuidesOnRetainedParameters(); ProjectedGuideBindings(); ReadbackFaults(); WrapperFaults(); SourceGuards(); TemporalMetadata(); ObservedMetadata();
    std::puts("PASS: SDK pointer overloads, legacy integer rejection, readback faults, wrapper/bounds guards,");
    std::puts("      evaluation exclusion, immutable tuning, untouched RR parameters and retirement guards.");
    std::puts("LIMIT: CPU tests do not validate NVIDIA model consumption, GPU completion or game stability.");
}
