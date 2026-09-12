#include "../OptiScaler/NrConfigSnapshot.h"
#include "../OptiScaler/dlssnr/DlssNr_StageUi.h"

#include <barrier>
#include <array>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAIL line " << __LINE__ << ": " #condition << '\n'; std::abort(); } } while (false)

static_assert(!std::is_base_of_v<std::optional<float>, NrOptional<float>>);
static_assert(!std::is_reference_v<decltype(std::declval<NrOptional<std::string>>().value())>);
static_assert(!std::is_reference_v<decltype(*std::declval<NrOptional<std::string>>())>);
static_assert(!std::is_convertible_v<NrOptional<float>&, std::optional<float>&>);
static_assert(!std::is_copy_constructible_v<NrConfigSynchronization::Transaction>);

// CPU-only source with the production NR field types/defaults. Production Config does not
// need Windows/NGX/ImGui initialization to test the actual wrapper and snapshot template.
constexpr int UnboundKey = -1;
enum class Scaler : uint32_t { Lanczos3 = 4 };
struct TestConfig
{
    struct Layer
    {
        CustomOptional<float> workingScale {1.0f}, intensity {1.0f}, localStructure {1.0f}, localTone {1.0f},
            skinStructure {-1.0f}, transferStrength {1.0f}, colourStrength {1.0f}, maxRatio {2.0f};
        CustomOptional<Scaler> scalingDownscaler {Scaler::Lanczos3};
        CustomOptional<uint32_t> transfer {1}, preset {0}, style {0}, reversibleMode {0};
        CustomOptional<bool> autoMask {true}, applyModel {true};
    };
    struct ExtraLayers
    {
        std::array<Layer, 8> values;
        auto CopyForSnapshot(const NrConfigSynchronization::Transaction&) const { return values; }
    };
    NrConfigSnapshot<TestConfig> GetDlssNrConfigSnapshot() const;
    NrOptional<bool> DlssNrEnabled { false };
    NrOptional<bool> DlssNrMultipassEnabled { false };
    NrOptional<DlssNr::BasicMultipass::Profile> DlssNrBasicMultipass { {} };
    NrOptional<bool> DlssNrSecondLayer { false };
    NrOptional<float> DlssNrSecondLayerWorkingScale { 1.0f };
    NrOptional<Scaler> DlssNrSecondLayerScalingDownscaler { Scaler::Lanczos3 };
    NrOptional<uint32_t> DlssNrSecondLayerTransfer { 1 };
    NrOptional<uint32_t> DlssNrSecondLayerPreset { 0 };
    NrOptional<float> DlssNrSecondLayerIntensity { 1.0f };
    NrOptional<uint32_t> DlssNrSecondLayerStyle { 0 };
    NrOptional<float> DlssNrSecondLayerLocalStructure { 1.0f };
    NrOptional<float> DlssNrSecondLayerLocalTone { 1.0f };
    NrOptional<float> DlssNrSecondLayerSkinStructure { -1.0f };
    NrOptional<bool> DlssNrSecondLayerAutoMask { true };
    NrOptional<float> DlssNrSecondLayerTransferStrength { 1.0f };
    NrOptional<float> DlssNrSecondLayerColourStrength { 1.0f };
    NrOptional<float> DlssNrSecondLayerMaxRatio { 2.0f };
    NrOptional<uint32_t> DlssNrSecondLayerReversibleMode { 0 };
    NrOptional<bool> DlssNrSecondLayerApplyModel { true };
    ExtraLayers DlssNrExtraLayers;
    NrOptional<uint32_t> DlssNrRoute { 2 };
    NrOptional<bool> DlssNrUiManualResolution { false };
    NrOptional<float> DlssNrUiManualScale { 1.0f };
    NrOptional<uint32_t> DlssNrUiAfterMethod { 0 };
    NrOptional<uint32_t> DlssNrPresentResolution { 1 };
    NrOptional<uint32_t> DlssNrPresentCustomScale { 0 };
    NrOptional<uint32_t> DlssNrEnhancedResolution { 1 };
    NrOptional<uint32_t> DlssNrEnhancedCustomScale { 0 };
    NrOptional<bool> DlssNrRunBeforeSr { false }; // experimental: run NR before DLSS SR
    NrOptional<int32_t> DlssNrRenderingMode { 1 };
    NrOptional<bool> DlssNrPreDlaa { false }; // v10: private native-resolution DLAA resolve before NR, then re-jitter before SR
    NrOptional<int> DlssNrToggleKey { UnboundKey };
    NrOptional<uint32_t> DlssNrPreset { 0 };
    NrOptional<float> DlssNrIntensity { 1.0f };
    NrOptional<uint32_t> DlssNrStyle { 0 };
    NrOptional<float> DlssNrLocalStructure { 1.0f };
    NrOptional<float> DlssNrLocalTone { 1.0f };
    NrOptional<float> DlssNrSkinStructure { -1.0f };
    NrOptional<bool> DlssNrAutoMask { true };
    NrOptional<float> DlssNrTransferStrength { 1.0f };
    NrOptional<float> DlssNrColourStrength { 1.0f };
    NrOptional<uint32_t> DlssNrReversibleMode { 0 };
    NrOptional<bool> DlssNrApplyModel { true };
    NrOptional<bool> DlssNrHoldFrame { false };
    NrOptional<float> DlssNrMaxRatio { 2.0f };
    NrOptional<uint32_t> DlssNrTransfer { 1 };
    NrOptional<bool> DlssNrWhitePointFromExposure { true };
    NrOptional<bool> DlssNrProbeD3D11 { false };
    NrOptional<uint32_t> DlssNrDebugView { 0 };
    NrOptional<uint32_t> DlssNrCompare { 0 };
    NrOptional<float> DlssNrCompareSplit { 0.5f };
    NrOptional<float> DlssNrCompareZoom { 1.0f };
    NrOptional<bool> DlssNrCompareSwap { false };
    NrOptional<bool> DlssNrCompareTags { false };
    NrOptional<float> DlssNrTagScale { 1.5f };
    NrOptional<float> DlssNrWorkingScale { 1.0f };
    NrOptional<Scaler> DlssNrScalingDownscaler { Scaler::Lanczos3 };
    NrOptional<bool> DlssNrProxyProbe { false };
    NrOptional<bool> DlssNrUseProxy { false };
    NrOptional<bool> DlssNrScanExposure { false };
    NrOptional<uint32_t> DlssNrWhitePointSource { 1 };
    NrOptional<bool> DlssNrScanMeter { false };
    NrOptional<float> DlssNrScanAnchorValue { 0.0f };       // legacy single anchor, migrated then unused
    NrOptional<float> DlssNrScanAnchorWhitePoint { 0.0f };  // legacy single anchor, migrated then unused
    NrOptional<std::string> DlssNrScanAnchors { std::string() };
    NrOptional<bool> DlssNrScanInverted { false };
    NrOptional<float> DlssNrWhitePointTrim { 1.0f };
    NrOptional<float> DlssNrScanTrim { 1.0f };
    NrOptional<uint32_t> DlssNrPasses { 1 };
    NrOptional<bool> DlssNrAutoCapture { true };
    NrOptional<float> DlssNrWhitePointScale { 1.0f };
    NrConfigState state;
    NrConfigState::RuntimeSnapshot GetDlssNrRuntimeSnapshot() const noexcept { return state.Snapshot(); }
};

NrConfigSnapshot<TestConfig> TestConfig::GetDlssNrConfigSnapshot() const
{
    return NrConfigSnapshot<TestConfig>(*this);
}

template <class T, HasDefaultValue D> void Differential(NrOptional<T, D>& synchronized,
                                                       CustomOptional<T, D>& original,
                                                       const T& a, const T& b)
{
    std::minstd_rand random(74094);
    for (int i = 0; i < 10000; ++i)
    {
        const T& v = (random() & 1) ? a : b;
        switch (random() % 8)
        {
        case 0: synchronized = v; original = v; break;
        case 1: synchronized.set_from_config(v); original.set_from_config(v); break;
        case 2: synchronized.set_from_config(std::nullopt); original.set_from_config(std::nullopt); break;
        case 3: synchronized.set_volatile_value(v); original.set_volatile_value(v); break;
        case 4: synchronized.reset(); original.reset(); break;
        case 5: synchronized = std::optional<T>{}; original = std::optional<T>{}; break;
        case 6: synchronized = T(v); original = T(v); break;
        case 7: synchronized = std::optional<T>{v}; original = std::optional<T>{v}; break;
        }
        CHECK(synchronized.has_value() == original.has_value());
        CHECK(synchronized.snapshot() == static_cast<std::optional<T>>(original));
        CHECK(synchronized.value_for_config() == original.value_for_config());
        CHECK(synchronized.value_for_config_or(b) == original.value_for_config_or(b));
        CHECK(synchronized.value_or(a) == original.value_or(a));
        if constexpr (D != NoDefault)
            CHECK(synchronized.value_or_default() == original.value_or_default());
        if (original.has_value())
            CHECK(synchronized.value() == original.value());
        NrOptional<T, D> copy(synchronized);
        CHECK(copy.snapshot() == synchronized.snapshot());
        CHECK(copy.value_for_config() == synchronized.value_for_config());
        copy = synchronized;
        CHECK(copy.value_for_config() == synchronized.value_for_config());
    }
}

void OptionalSemantics()
{
    NrOptional<float> f(1.0f);
    CustomOptional<float> raw(1.0f);
    Differential(f, raw, 1.0f, 0.375f);
    NrOptional<float, NoDefault> nd;
    CustomOptional<float, NoDefault> rawNd;
    Differential(nd, rawNd, 0.0f, 1.75f);
    NrOptional<int, SoftDefault> soft(5);
    CustomOptional<int, SoftDefault> rawSoft(5);
    Differential(soft, rawSoft, 5, 19);
    NrOptional<std::string> text("");
    CustomOptional<std::string> rawText("");
    Differential(text, rawText, std::string{}, std::string(2048, 'a'));

    // Explicit expectations, in addition to comparison with the original implementation.
    f.reset();
    f = std::optional<float>{};
    f.set_from_config(0.5f);
    f.set_volatile_value(0.75f);
    f.set_volatile_value(0.875f);
    CHECK(f.value() == 0.875f && f.value_for_config() == 0.5f);
    f = 1.0f;
    CHECK(!f.value_for_config().has_value());
    f.set_from_config(9.0f);
    CHECK(f.value() == 1.0f);
    soft = 5;
    CHECK(soft.value_for_config() == 5);
    nd.reset();
    CHECK(!nd.value_for_config().has_value());
    bool threw = false;
    try { (void)nd.value(); } catch (const std::bad_optional_access&) { threw = true; }
    CHECK(threw);
    nd = 0.0f;
    CHECK(nd.value_for_config() == 0.0f);
    text = "1:2;3:4";
    auto owned = text.value();
    text = std::string(4096, 'z');
    CHECK(owned == "1:2;3:4");

    NrOptional<Scaler> scaler(Scaler::Lanczos3);
    CHECK(!scaler.snapshot());
    scaler = Scaler::Lanczos3;
    CHECK(scaler.snapshot() == Scaler::Lanczos3); // legacy enum save persists engaged default
    CHECK(!scaler.value_for_config());           // NOT the old enum save path
    std::cout << "PASS optional/default/volatile/serialization differential tests\n";
}

void ConcurrentOptional()
{
    NrOptional<std::string> text("");
    NrOptional<float, NoDefault> amount;
    const std::string a(4096, 'a'), b(8192, 'b');
    std::barrier start(5);
    std::vector<std::thread> threads;
    for (int writer = 0; writer < 2; ++writer)
        threads.emplace_back([&, writer] {
            start.arrive_and_wait();
            for (int i = 0; i < 15000; ++i)
            {
                text = writer ? a : b;
                text.set_volatile_value(writer ? b : a);
                text.reset();
                text.set_from_config(a);
                amount = writer ? 0.5f : 1.5f;
                amount.reset();
                amount.set_from_config(0.5f);
            }
        });
    for (int reader = 0; reader < 3; ++reader)
        threads.emplace_back([&] {
            start.arrive_and_wait();
            for (int i = 0; i < 15000; ++i)
            {
                auto value = text.value_or_default();
                CHECK(value.empty() || value == a || value == b);
                auto saved = text.value_for_config();
                CHECK(!saved || *saved == a || *saved == b);
                auto numeric = amount.snapshot();
                CHECK(!numeric || *numeric == 0.5f || *numeric == 1.5f);
                NrOptional<std::string> copy(text);
                auto stableCopy = copy.value_or_default();
                CHECK(stableCopy.empty() || stableCopy == a || stableCopy == b);
            }
        });
    for (auto& t : threads) t.join();
    std::cout << "PASS concurrent strings, empty floats, volatile values, and copies\n";
}

void EnablePublication()
{
    NrConfigState state;
    NrOptional<bool> enabled(false);
    state.LoadEnabled(enabled, false);
    CHECK(!state.Snapshot().enabled && state.Snapshot().resumeGeneration == 0);
    state.SetEnabled(enabled, true);
    state.SetEnabled(enabled, true);
    CHECK(state.Snapshot().resumeGeneration == 1);
    state.SetEnabled(enabled, false);
    CHECK(state.Snapshot().resumeGeneration == 1);
    std::barrier start(3);
    std::thread toggler([&] {
        start.arrive_and_wait();
        for (int i = 0; i < 30000; ++i) { state.SetEnabled(enabled, true); state.SetEnabled(enabled, false); }
    });
    std::thread loader([&] {
        start.arrive_and_wait();
        for (int i = 0; i < 30000; ++i) state.LoadEnabled(enabled, (i & 1) != 0);
    });
    std::thread reader([&] {
        start.arrive_and_wait();
        uint64_t previous = 0;
        for (int i = 0; i < 30000; ++i)
        {
            auto runtime = state.Snapshot();
            CHECK(runtime.resumeGeneration >= previous);
            previous = runtime.resumeGeneration;
            NrConfigSynchronization::Transaction transaction;
            CHECK(state.Snapshot().enabled == enabled.CopyForSnapshot(transaction).value_or_default());
        }
    });
    toggler.join(); loader.join(); reader.join();
    CHECK(!state.Snapshot().enabled && state.Snapshot().resumeGeneration == 30001);
    std::cout << "PASS config loading versus packed enable publication and exact resume count\n";
}

void CoherentSnapshots()
{
    TestConfig config;
    config.DlssNrIntensity = 0.0f;
    NrConfigState::LoadRoutingMode(config.DlssNrRenderingMode, config.DlssNrRunBeforeSr, 0);
    config.state.LoadEnabled(config.DlssNrEnabled, false);
    config.DlssNrScanAnchors = "0";
    std::barrier start(4);
    std::thread writer([&] {
        start.arrive_and_wait();
        for (uint32_t i = 1; i <= 30000; ++i)
        {
            NrConfigSynchronization::Transaction transaction;
            config.DlssNrStyle = i;
            config.DlssNrIntensity = static_cast<float>(i);
            config.DlssNrScanAnchors = std::to_string(i);
            NrConfigState::SetRoutingMode(config.DlssNrRenderingMode, config.DlssNrRunBeforeSr, i & 1);
            config.state.SetEnabled(config.DlssNrEnabled, (i & 1) != 0);
        }
    });
    std::vector<std::thread> readers;
    for (int r = 0; r < 3; ++r)
        readers.emplace_back([&] {
            start.arrive_and_wait();
            for (int i = 0; i < 15000; ++i)
            {
                const auto snapshot = config.GetDlssNrConfigSnapshot();
                const auto createIntensity = snapshot.DlssNrIntensity.value_or_default();
                const auto createStyle = snapshot.DlssNrStyle.value_or_default();
                std::this_thread::yield(); // simulate work after releasing the config mutex
                const auto recordedIntensity = snapshot.DlssNrIntensity.value_or_default();
                CHECK(createIntensity == recordedIntensity && createIntensity == static_cast<float>(createStyle));
                CHECK(snapshot.DlssNrScanAnchors.value_or_default() == std::to_string(createStyle));
                CHECK(snapshot.DlssNrRunBeforeSr.value_or_default() == (snapshot.DlssNrRenderingMode.value_or_default() != 0));
                CHECK(snapshot.GetDlssNrRuntimeSnapshot().enabled == snapshot.DlssNrEnabled.value_or_default());
                CHECK(snapshot.DlssNrEnabled.value_or_default() == ((createStyle & 1) != 0));
            }
        });
    writer.join();
    for (auto& reader : readers) reader.join();

    const NrConfigSnapshot<TestConfig> held(config);
    auto writerWithSnapshotAlive = std::async(std::launch::async, [&] { config.DlssNrIntensity = -1.0f; });
    CHECK(writerWithSnapshotAlive.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    writerWithSnapshotAlive.get();
    CHECK(held.DlssNrIntensity.value_or_default() == 30000.0f);

    // Reload must preserve an existing canonical UI choice when filling an empty legacy option.
    config.DlssNrRunBeforeSr.reset();
    NrConfigState::LoadRoutingMode(config.DlssNrRenderingMode, config.DlssNrRunBeforeSr, 1);
    CHECK(config.DlssNrRunBeforeSr.value_or_default() == (config.DlssNrRenderingMode.value_or_default() != 0));
    NrConfigState::SetRoutingMode(config.DlssNrRenderingMode, config.DlssNrRunBeforeSr, -10);
    CHECK(config.DlssNrRenderingMode.value() == 0 && !config.DlssNrRunBeforeSr.value());
    NrConfigState::SetRoutingMode(config.DlssNrRenderingMode, config.DlssNrRunBeforeSr, 2);
    CHECK(config.DlssNrRenderingMode.value() == 1 && config.DlssNrRunBeforeSr.value());
    std::cout << "PASS coherent tuning, routing, owned snapshots, and unlocked post-capture work\n";
}

void SnapshotCost()
{
    TestConfig config;
    config.DlssNrScanAnchors = std::string(2048, 'x');
    constexpr int count = 100000;
    size_t checksum = 0;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < count; ++i)
    {
        const NrConfigSnapshot<TestConfig> snapshot(config);
        checksum += snapshot.DlssNrScanAnchors.value_or_default().size();
        checksum += snapshot.DlssNrWorkingScale.value_or_default() > 0;
    }
    const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start);
    std::cout << "INFO single-lock 60-field snapshot (2 KiB anchor, uncontended): "
              << elapsed.count() / count << " us/capture; checksum=" << checksum << '\n';
}

struct ThrowOnCopy
{
    inline static bool fail = false; // changed only before/after this single-threaded fault injection
    ThrowOnCopy() = default;
    ThrowOnCopy(const ThrowOnCopy&) { if (fail) throw std::bad_alloc(); }
    ThrowOnCopy(ThrowOnCopy&&) = default;
    ThrowOnCopy& operator=(const ThrowOnCopy&) = default;
    ThrowOnCopy& operator=(ThrowOnCopy&&) = default;
};

void SnapshotCopyFailure()
{
    struct FaultConfig : TestConfig
    {
        NrOptional<ThrowOnCopy> DlssNrScanAnchors { ThrowOnCopy{} };
    } config;
    ThrowOnCopy::fail = true;
    bool threw = false;
    try { const NrConfigSnapshot<FaultConfig> snapshot(config); (void)snapshot; }
    catch (const std::bad_alloc&) { threw = true; }
    CHECK(!TryNrConfigSnapshot(config));
    ThrowOnCopy::fail = false;
    CHECK(threw);
    auto writer = std::async(std::launch::async, [&] { config.DlssNrStyle = 3u; });
    CHECK(writer.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    writer.get();
    CHECK(config.DlssNrStyle.value() == 3u);
    std::cout << "PASS injected snapshot copy failure releases the transaction mutex\n";
}

void StageFirstContract()
{
    namespace U = DlssNr::StageUi;
    namespace R = DlssNr::PresentResolution;
    for (uint32_t route = 0; route < 3; ++route)
    for (int mode = 0; mode < 2; ++mode)
    for (float scale : {0.25f, 0.5f, 0.67f, 1.0f, 1.25f, 2.0f})
    for (bool hint : {false, true})
    {
        TestConfig c;
        c.DlssNrRoute = route;
        NrConfigState::SetRoutingMode(c.DlssNrRenderingMode, c.DlssNrRunBeforeSr, mode);
        c.DlssNrWorkingScale = scale;
        U::LoadHints(c, hint, 0.77f, 999u);
        CHECK(c.DlssNrRoute.value_or_default() == route);
        CHECK(c.DlssNrRenderingMode.value_or_default() == mode);
        CHECK(c.DlssNrWorkingScale.value_or_default() == scale);
        CHECK(U::Stage(c) == (route == 0 && mode == 1 ? 0 : 1));
        CHECK(U::Manual(c) == (scale != 1.0f || hint));
        if (U::Stage(c) == 1)
        {
            U::SelectStage(c, 0); CHECK(c.DlssNrRoute.value_or_default() == 0);
            U::SelectStage(c, 1); CHECK(c.DlssNrRoute.value_or_default() == route);
        }
        CHECK(c.DlssNrWorkingScale.value_or_default() == scale);
        U::SelectManual(c, false); CHECK(c.DlssNrWorkingScale.value_or_default() == 1.0f);
        U::SelectManual(c, true); CHECK(c.DlssNrWorkingScale.value_or_default() == scale);
    }
    for (uint32_t route : {1u, 2u})
    for (int preset = 0; preset < 7; ++preset)
    {
        TestConfig c;
        U::SelectMethod(c, route);
        U::SelectPreset(c, preset);
        const auto selected = R::Selected(c);
        CHECK(U::Preset(selected) == preset);
        const auto expected = R::Resolve(selected, 2560, 1440, 1280, 720);
        CHECK(expected.width && expected.height);
        U::SelectMethod(c, route == 1 ? 2 : 1); U::SelectPreset(c, (preset + 1) % 7);
        U::SelectMethod(c, 0); U::SelectScale(c, 1.25f);
        U::SelectMethod(c, route);
        CHECK(R::Selected(c).mode == selected.mode && R::Selected(c).scale == selected.scale);
        CHECK(R::Resolve(R::Selected(c), 2560, 1440, 1280, 720).width == expected.width);
        if (preset == 0)
            CHECK(R::Resolve(selected, 2560, 1440, 960, 540).width == 960);
    }
    for (auto hint : {std::optional<float>{}, std::optional<float>{-5.0f},
                     std::optional<float>{9.0f}, std::optional<float>{NAN}})
    {
        TestConfig c; U::LoadHints(c, {}, hint, 99u);
        CHECK(!U::Manual(c) && c.DlssNrWorkingScale.value_or_default() == 1.0f);
        CHECK(c.DlssNrRoute.value_or_default() == 2 && !c.GetDlssNrRuntimeSnapshot().enabled);
        U::SelectManual(c, true); CHECK(c.DlssNrWorkingScale.value_or_default() == 1.0f);
    }
    TestConfig c;
    U::SelectMethod(c, 2);
    std::atomic<bool> done = false;
    auto writer = std::async(std::launch::async, [&] {
        for (int n = 0; n < 10000; ++n) { U::SelectStage(c, 0); U::SelectStage(c, 1); }
        done = true;
    });
    do
    {
        const auto s = c.GetDlssNrConfigSnapshot();
        CHECK((s.DlssNrRoute.value_or_default() == 0 && s.DlssNrRenderingMode.value_or_default() == 1 && s.DlssNrRunBeforeSr.value_or_default()) ||
              (s.DlssNrRoute.value_or_default() == 2 && s.DlssNrRenderingMode.value_or_default() == 0 && !s.DlssNrRunBeforeSr.value_or_default()));
    } while (!done);
    writer.get();
    std::cout << "PASS stage/method/resolution truth table, old-scale authority, UI memory, malformed hints, dynamic resolution and atomic stage publication\n";
}

static void BasicMultipassContract()
{
    namespace B = DlssNr::BasicMultipass;
    TestConfig c;
    c.DlssNrWorkingScale = 0.67f;
    c.DlssNrIntensity = 1.4f;
    c.DlssNrTransferStrength = 1.7f;
    c.DlssNrSecondLayerWorkingScale = 1.75f;
    c.DlssNrSecondLayerIntensity = 0.45f;
    c.DlssNrExtraLayers.values[0].intensity = 0.65f;
    c.DlssNrPreset = 3u;
    c.DlssNrStyle = 2u;
    c.DlssNrSkinStructure = 0.3f;
    c.DlssNrColourStrength = 0.8f;
    c.DlssNrMultipassEnabled = true;
    for (float total : {0.0f, 1.0f, 1.1f, 2.3f, 3.3f, 10.0f})
    for (float detail : {0.0f, 1.0f, 1.5f, 10.0f})
    {
        B::Update(c, [&](auto& p) { p.maximum = 10; p.model = total; p.detail = detail; p.resolution = 1.25f; });
        auto s = TryNrConfigSnapshot(c); CHECK(s.has_value());
        CHECK(s->DlssNrPasses.value_or_default() == uint32_t(std::ceil((std::max)(total, detail))));
        CHECK(s->DlssNrWorkingScale.value_or_default() == 1.25f);
        CHECK(s->DlssNrIntensity.value_or_default() == B::Strength(total, 0));
        CHECK(s->DlssNrTransferStrength.value_or_default() == B::Strength(detail, 0));
        CHECK(s->DlssNrSecondLayerIntensity.value_or_default() == B::Strength(total, 1));
        CHECK(s->DlssNrSecondLayerTransferStrength.value_or_default() == B::Strength(detail, 1));
        for (unsigned i = 0; i < 8; ++i)
        {
            const auto& layer = s->DlssNrExtraLayers[i];
            CHECK(layer.intensity.value_or_default() == B::Strength(total, i + 2));
            CHECK(layer.transferStrength.value_or_default() == B::Strength(detail, i + 2));
            CHECK(layer.workingScale.value_or_default() == 1.25f);
            CHECK(layer.preset.value_or_default() == 3 && layer.style.value_or_default() == 2);
            CHECK(layer.skinStructure.value_or_default() == 0.3f && layer.colourStrength.value_or_default() == 0.8f);
        }
        // Derivation cannot modify any stored main or additional profile.
        CHECK(c.DlssNrWorkingScale.value_or_default() == 0.67f && c.DlssNrIntensity.value_or_default() == 1.4f);
        CHECK(c.DlssNrSecondLayerIntensity.value_or_default() == 0.45f);
        CHECK(c.DlssNrExtraLayers.values[0].intensity.value_or_default() == 0.65f);
    }
    B::Update(c, [](auto& p) { p.advanced = true; });
    auto advanced = TryNrConfigSnapshot(c); CHECK(advanced.has_value());
    CHECK(advanced->DlssNrIntensity.value_or_default() == 1.4f);
    CHECK(advanced->DlssNrTransferStrength.value_or_default() == 1.7f);
    CHECK(advanced->DlssNrSecondLayerWorkingScale.value_or_default() == 1.75f);
    CHECK(advanced->DlssNrExtraLayers[0].intensity.value_or_default() == 0.65f);
    for (bool isAdvanced : {true, false})
    {
        B::Update(c, [&](auto& p) { p.advanced = isAdvanced; });
        c.DlssNrApplyModel = false;
        auto hidden = TryNrConfigSnapshot(c); CHECK(hidden.has_value());
        CHECK(!hidden->DlssNrApplyModel.value_or_default() && !hidden->DlssNrSecondLayerApplyModel.value_or_default());
        for (const auto& layer : hidden->DlssNrExtraLayers) CHECK(!layer.applyModel.value_or_default());
        CHECK(c.DlssNrSecondLayerApplyModel.value_or_default());
        c.DlssNrApplyModel = true;
        CHECK(TryNrConfigSnapshot(c)->DlssNrSecondLayerApplyModel.value_or_default());
    }
    c.DlssNrMultipassEnabled = false;
    CHECK(TryNrConfigSnapshot(c)->DlssNrWorkingScale.value_or_default() == 0.67f);
    CHECK(TryNrConfigSnapshot(c)->DlssNrIntensity.value_or_default() == 1.4f);
    std::atomic<bool> done = false;
    auto writer = std::async(std::launch::async, [&] {
        for (int i = 0; i < 10000; ++i)
        {
            B::Update(c, [](auto& p) { p.maximum = 10; p.model = 10; p.detail = 9; });
            B::Update(c, [](auto& p) { p.maximum = 1; });
        }
        done = true;
    });
    do
    {
        const auto s = c.GetDlssNrConfigSnapshot();
        const auto p = s.DlssNrBasicMultipass.value_or_default();
        CHECK(p.model <= p.maximum && p.detail <= p.maximum);
    } while (!done);
    writer.get();
    CHECK(!B::Load([](const char*) -> std::optional<float> { return {}; }, false).advanced);
    CHECK(B::Load([](const char*) -> std::optional<float> { return {}; }, true).advanced);
    CHECK(B::Load([](const char*) -> std::optional<float> { return NAN; }, false) == B::Profile{});
    std::cout << "PASS Basic Multipass totals, independent distribution, profile restoration, global effect visibility and atomic maximum reduction\n";
}

int main()
{
    BasicMultipassContract();
    StageFirstContract();
    OptionalSemantics();
    ConcurrentOptional();
    EnablePublication();
    CoherentSnapshots();
    SnapshotCopyFailure();
    SnapshotCost();
    std::cout << "PASS all NR config tests\n";
}
