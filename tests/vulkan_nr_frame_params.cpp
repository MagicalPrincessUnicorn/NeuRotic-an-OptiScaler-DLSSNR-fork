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

static void WrapperFaults()
{
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

static std::string Read(const char* path)
{
    std::ifstream file(path); Check(file.good(), path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void SourceGuards()
{
    const auto source = Read("OptiScaler/dlssnr/DlssNrFeature_Vk.cpp");
    const auto start = source.find("void EvaluateAfterUpscaleVk(");
    const auto end = source.find("static void ShutdownVkLocked(bool deviceAlive)\n{", start);
    Check(start != std::string::npos && end != std::string::npos, "evaluation boundaries found");
    const auto evaluation = source.substr(start, end - start);
    Check(evaluation.find("++g_vk.frames;") != std::string::npos, "successful NR counter retained");
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
    for (const char* forbidden : {"vkDeviceWaitIdle(", "g_vk.release(", "DestroyImage(",
                                  "DestroyParameters(", "(unsigned long long) (uintptr_t)"})
        Check(evaluation.find(forbidden) == std::string::npos, forbidden);
    const auto gate = evaluation.find("VkFrame::PrepareAndEvaluate(modelParams, frame, activeModel.settings");
    const auto modelCall = evaluation.find("g_vk.evaluate(");
    Check(gate != std::string::npos && modelCall != std::string::npos && modelCall > gate,
          "production model call uses tested gate and applied tuning");
    Check(evaluation.find("VkFrame::ValidateImage(\"RR.Output\"") <
          evaluation.find("const uint32_t width = colour->Resource.ImageViewInfo.Width"),
          "game wrapper validated before dimension union read");
}

int main()
{
    TypedResourcesAndPreservation(); ReadbackFaults(); WrapperFaults(); SourceGuards();
    std::puts("PASS: SDK pointer overloads, legacy integer rejection, readback faults, wrapper/bounds guards,");
    std::puts("      evaluation exclusion, immutable tuning, untouched RR parameters and retirement guards.");
    std::puts("LIMIT: CPU tests do not validate NVIDIA model consumption, GPU completion or game stability.");
}
