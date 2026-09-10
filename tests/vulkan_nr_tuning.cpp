#include "../OptiScaler/dlssnr/VulkanNrTuning.h"
#include "../OptiScaler/inputs/VulkanNativeFeatures.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <variant>

using namespace DlssNr::VkTuning;
static void Check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct Params
{
    std::unordered_map<std::string, std::variant<float, uint32_t>> values;
    std::string failKey;
    bool wrongValue = false;
    template<class T> void Set(const char* key, T value) { values[key] = value; }
    template<class T> unsigned Get(const char* key, T* out)
    {
        if (failKey == key && !wrongValue) return 0;
        auto it = values.find(key);
        if (it == values.end() || !std::holds_alternative<T>(it->second)) return 0;
        *out = std::get<T>(it->second);
        if (failKey == key && wrongValue) *out += T(1);
        return 1;
    }
};
static void Parameters()
{
    Params p;
    Settings s{3, 2, .25f, .5f, 1.75f, -1.f, false};
    Check(Prepare(&p, s, 3440, 1440), "typed parameter preparation");
    Check(std::get<float>(p.values.at("DLSSNR.Intensity")) == .25f, "float overload");
    Check(std::get<uint32_t>(p.values.at("DLSSNR.Width")) == 3440, "extent overload");
    const char* failed = nullptr;
    p.failKey = "DLSSNR.LocalToneStrength";
    Check(!Prepare(&p, s, 3440, 1440, &failed) && std::string(failed) == p.failKey, "Get failure is named");
    p.wrongValue = true;
    Check(!Prepare(&p, s, 3440, 1440), "roundtrip mismatch rejected");
    Check(!Prepare<Params>(nullptr, s, 1, 1), "missing block rejected");
    s.intensity = std::numeric_limits<float>::quiet_NaN();
    Check(!Prepare(&p, s, 1, 1), "nonfinite tuning rejected");
}
static void Configurations()
{
    Cache<Params> cache;
    Settings keys[9]{};
    keys[1].preset = 1; keys[2].style = 1; keys[3].intensity = .5f;
    keys[4].structure = .5f; keys[5].tone = .5f; keys[6].skin = .5f;
    keys[7].mask = false; keys[8].preset = 2;
    Params blocks[8]; int features[8]{}; size_t creates = 0;
    auto create = [&](auto& e) {
        e.params = &blocks[creates]; e.feature = &features[creates]; ++creates;
        return Prepare(e.params, e.settings, 3440, 1440);
    };
    for (int i = 0; i < 8; ++i)
    {
        Check(cache.Select(keys[i], create) == Result::Applied, "all eight distinct configurations");
        Check(cache.changed && cache.active == i, "switch requests history reset");
        Check(cache.Select(keys[i], create) == Result::Applied && !cache.changed, "same key does not reset/recreate");
    }
    Check(creates == 8 && cache.size == 8, "initial config counted and eight creations only");
    Check(cache.Select(keys[8], create) == Result::Full && cache.active == 7 && !cache.changed,
          "ninth request preserves active feature");
    for (int i = 0; i < 8; ++i)
        Check(cache.Select(keys[i], create) == Result::Applied && cache.active == i, "cached entries remain selectable");
    Check(creates == 8, "reselection does not allocate");
    for (int i = 0; i < 8; ++i)
        Check(cache.entries[i].params == &blocks[i] && cache.entries[i].feature == &features[i] &&
              cache.entries[i].settings == keys[i], "immutable settings and distinct owned objects");

    Cache<Params> failing;
    Check(failing.Select(keys[0], [&](auto& e) {
        e.params = &blocks[0]; e.feature = &features[0]; return true;
    }) == Result::Applied, "initial successful model");
    size_t attempts = 0;
    auto reject = [&](auto& e) { ++attempts; e.feature = &features[0]; return false; };
    Check(failing.Select(keys[1], reject) == Result::Failed && failing.active == 0 && !failing.changed,
          "creation failure retains applied model");
    Check(failing.entries[1].feature == &features[0], "failed-create resource retained for shutdown");
    Check(failing.Select(keys[1], reject) == Result::Failed && attempts == 1, "failed request not retried per frame");
    Check(failing.Select(keys[0], reject) == Result::Applied && attempts == 1, "old config still selectable");
    auto invalid = keys[0]; invalid.style = 100;
    Check(failing.Select(invalid, reject) == Result::Invalid && failing.active == 0, "invalid config preserves active");
    Cache<Params> firstFailure;
    Check(firstFailure.Select(keys[0], reject) == Result::Failed && firstFailure.active == -1,
          "first failure never activates NR");
}
static void NativeFeatures()
{
    VulkanNativeFeatures::Registry features;
    Check(features.Observe(77).generation == 0, "unknown feature stays uncorrelated");
    const auto first = features.Register(1, 11, 42);
    Check(features.Observe(1).type == 11, "FG identity retained");
    features.Release(1, false);
    Check(features.Observe(1).generation == first.generation, "failed release keeps identity");
    features.Release(1, true);
    Check(features.Observe(1).generation == 0, "successful release removes identity");
    const auto reused = features.Register(1, 99, 43);
    Check(reused.generation > first.generation && features.Observe(1).type == 99, "handle reuse fresh identity");
    const auto replaced = features.Register(1, 11, 44);
    Check(replaced.generation > reused.generation && features.Observe(1).evaluations == 1, "re-register resets generation/counter");
    features.Clear();
    Check(features.Observe(1).generation == 0, "device shutdown clears identities");
}
static std::string Read(const char* path)
{
    std::ifstream file(path); Check(file.good(), path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static std::string Section(const std::string& text, const char* from, const char* to)
{
    const auto start = text.find(from); Check(start != std::string::npos, from);
    const auto end = text.find(to, start + std::string(from).size()); Check(end != std::string::npos, to);
    return text.substr(start, end - start);
}
static void StaticSafety()
{
    const auto source = Read("OptiScaler/dlssnr/DlssNrFeature_Vk.cpp");
    const auto evaluation = Section(source, "void EvaluateAfterUpscaleVk(", "static void ShutdownVkLocked(bool deviceAlive)\n{");
    for (const char* forbidden : {"vkDeviceWaitIdle(", "g_vk.release(", "DestroyImage(",
                               "DestroyParameters(", "superUp.reset(", "superDown.reset("})
        Check(evaluation.find(forbidden) == std::string::npos, forbidden);
    Check(evaluation.find("output or working-size contract changed; restart required") != std::string::npos,
          "physical change bypass retained");
    Check(evaluation.find("g_vk.models.changed)\n    {\n        g_vk.reset = true;") != std::string::npos,
          "cache switching wired to history reset");
    const auto shutdown = Section(source, "static void ShutdownVkLocked(bool deviceAlive)\n{", "void ShutdownVk(bool deviceAlive)");
    Check(shutdown.find("const int result = g_vk.release ? g_vk.release(entry.feature) : -1;") != std::string::npos &&
          shutdown.find("shutdown release failed") != std::string::npos, "shutdown checks release result");
    const auto native = Read("OptiScaler/inputs/NVNGX_DLSS_Vk.cpp");
    const auto passThrough = Section(native, "// SR/RR are owned contexts below.", "else");
    Check(passThrough.find("EvaluateAfterUpscaleVk") == std::string::npos &&
          passThrough.find("return result;") != std::string::npos, "native FG/unknown returns without NR");
    Check(native.find("RegisterNativeFeature(result, OutHandle, InFeatureID, InDevice)") != std::string::npos,
          "CreateFeature1 tracks native identity");
    Check(native.find("RegisterNativeFeature(result, OutHandle, InFeatureID, vkDevice)") != std::string::npos,
          "legacy CreateFeature tracks native identity");
    const auto forwarder = Read("OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp");
    const auto v2 = Section(forwarder, "int dlssnr_vk_create_v2(", "__declspec(dllexport) void dlssnr_vk_release(");
    Check(v2.find("setFloat") == std::string::npos && v2.find("setUInt") == std::string::npos &&
          v2.find("volatile int result") != std::string::npos, "versioned forwarder does not overwrite typed tuning");
    std::puts("Static checks passed (source guards, not Vulkan runtime validation).");
}
int main()
{
    Parameters(); Configurations(); NativeFeatures(); StaticSafety();
    std::puts("PASS: typed tuning, eight-slot cache, failure preservation, native identities and safety guards");
}
