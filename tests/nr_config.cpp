#include "../OptiScaler/NrConfigSnapshot.h"

#include <barrier>
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
    NrConfigSnapshot<TestConfig> GetDlssNrConfigSnapshot() const;
    NrOptional<bool> DlssNrEnabled { false };
    NrOptional<bool> DlssNrSecondLayer { false };
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
    std::cout << "INFO single-lock 45-field snapshot (2 KiB anchor, uncontended): "
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

int main()
{
    OptionalSemantics();
    ConcurrentOptional();
    EnablePublication();
    CoherentSnapshots();
    SnapshotCopyFailure();
    SnapshotCost();
    std::cout << "PASS all NR config tests\n";
}
