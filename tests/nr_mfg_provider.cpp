#include "../OptiScaler/mfg/MfgProviderQualification.h"
#include "../OptiScaler/mfg/MfgAdaPlan.h"
#include "../OptiScaler/mfg/MfgModulePath.h"

#include <array>
#include <cassert>
#include <cstdint>

using namespace Neurotic::Mfg;

static constexpr std::array<uint8_t, 52> providerBytes = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x3D, 0xB0, 0x01, 0x00, 0x00, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x81, 0xFD, 0xB0, 0x01, 0x00, 0x00, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static constexpr std::array<uint8_t, 28> wrapperBytes = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0xBA, 0x05, 0, 0, 0, 0x3B, 0xCA, 0x0F, 0x42, 0xD1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static MfgProviderImages GoodImages()
{
    return {{providerBytes.data(), providerBytes.size(), 101, 12, true, 1,
                 MfgProviderVersion {310, 8, 0, 0}, 1, 5,
                 FingerprintMfgImage(providerBytes.data(), providerBytes.size())},
            {wrapperBytes.data(), wrapperBytes.size(), 202, 12, true, 1,
                 MfgProviderVersion {2, 13, 0, 0}, 0, 5,
                 FingerprintMfgImage(wrapperBytes.data(), wrapperBytes.size())}, 101, 202};
}

static void TestExactSelectedPair()
{
    const auto result = QualifyMfgProvider(GoodImages(), {true, true}, 12);
    assert(result.status == MfgQualificationStatus::ReadyForExperiment);
    assert(result.providerIdentity == 101 && result.wrapperIdentity == 202);
    assert(result.providerGeneration == 12);
    assert(result.architectureSites == 2 && result.wrapperSites == 1);
    assert(result.maxGenerated == 5);
    auto olderBytes = wrapperBytes;
    olderBytes[9] = 3;
    auto older = GoodImages();
    older.wrapper.bytes = olderBytes.data();
    older.wrapper.snapshotFingerprint = FingerprintMfgImage(olderBytes.data(), olderBytes.size());
    assert(QualifyMfgProvider(older, {true, true}, 12).maxGenerated == 3);
    olderBytes[9] = 4; // unknown clamp is not a supported capability
    older.wrapper.snapshotFingerprint = FingerprintMfgImage(olderBytes.data(), olderBytes.size());
    const auto refused = QualifyMfgProvider(older, {true, true}, 12);
    assert(refused.status == MfgQualificationStatus::Refused && !refused.maxGenerated);
    auto unsupported = GoodImages();
    unsupported.provider.version = {310, 9, 1, 0};
    unsupported.provider.temporalProfile = 2;
    unsupported.wrapper.version = {2, 14, 1, 0};
    assert(QualifyMfgProvider(unsupported, {true, true}, 12).status ==
        MfgQualificationStatus::ReadyForExperiment);
    unsupported.wrapper.version = {2, 14, 0, 0}; // selected NVIDIA OTA wrapper
    assert(QualifyMfgProvider(unsupported, {true, true}, 12).status ==
        MfgQualificationStatus::ReadyForExperiment);
    unsupported.provider.temporalProfile = 1;
    assert(QualifyMfgProvider(unsupported, {true, true}, 12).status ==
        MfgQualificationStatus::ReadyForExperiment); // Actual structure, not a version-pair label.
    unsupported.provider.version = {900, 41, 2, 9};
    unsupported.wrapper.version = {};
    assert(QualifyMfgProvider(unsupported, {true, true}, 12).status ==
        MfgQualificationStatus::ReadyForExperiment);
    unsupported.provider.temporalProfile = 99;
    assert(QualifyMfgProvider(unsupported, {true, true}, 12).status ==
        MfgQualificationStatus::Refused); // Unknown actual transformation stays unsupported.
}

static void TestMissingDuplicateAndForeign()
{
    auto images = GoodImages();
    images.provider.activeOwner = false;
    assert(QualifyMfgProvider(images, {true, true}, 12).status ==
           MfgQualificationStatus::Unavailable);
    images = GoodImages();
    images.provider.candidateCount = 2;
    assert(QualifyMfgProvider(images, {true, true}, 12).status ==
           MfgQualificationStatus::Indeterminate);
    images = GoodImages();
    images.provider.bytes = wrapperBytes.data();
    images.provider.size = wrapperBytes.size();
    assert(QualifyMfgProvider(images, {true, true}, 12).status ==
           MfgQualificationStatus::Refused);
}

static void TestAdapterAbiTemporalAndGeneration()
{
    auto images = GoodImages();
    assert(QualifyMfgProvider(images, {false, true}, 12).status == MfgQualificationStatus::Refused);
    assert(QualifyMfgProvider(images, {true, false}, 12).status == MfgQualificationStatus::Refused);
    images.provider.abiVersion = 77;
    assert(QualifyMfgProvider(images, {true, true}, 12).status == MfgQualificationStatus::Refused);
    images = GoodImages();
    images.provider.temporalProfile = 0;
    assert(QualifyMfgProvider(images, {true, true}, 12).status == MfgQualificationStatus::Refused);
    images = GoodImages();
    images.boundProviderIdentity = 999;
    assert(QualifyMfgProvider(images, {true, true}, 12).status == MfgQualificationStatus::Refused);
    images = GoodImages();
    images.wrapper.version.minor = 14;
    assert(QualifyMfgProvider(images, {true, true}, 12).status == MfgQualificationStatus::ReadyForExperiment);
    images = GoodImages();
    assert(QualifyMfgProvider(images, {true, true}, 13).status == MfgQualificationStatus::Stale);
    images = GoodImages();
    images.wrapper.generation = 13;
    assert(QualifyMfgProvider(images, {true, true}, 12).status == MfgQualificationStatus::Stale);
}

static void TestChangedImageCannotReuseVersion()
{
    auto changed = providerBytes;
    changed[40] = 0x02;
    auto images = GoodImages();
    images.provider.bytes = changed.data();
    assert(QualifyMfgProvider(images, {true, true}, 12).status == MfgQualificationStatus::Refused);
    images = GoodImages();
    images.wrapper.snapshotFingerprint = 0;
    assert(QualifyMfgProvider(images, {true, true}, 12).status == MfgQualificationStatus::Refused);
    images = GoodImages();
    images.provider.snapshotFingerprint = 1;
    assert(QualifyMfgProvider(images, {true, true}, 12).status == MfgQualificationStatus::Refused);

    auto duplicateFirst = providerBytes;
    const uint8_t firstAgain[] = {0x3D, 0xB0, 1, 0, 0, 0};
    for (size_t i = 0; i < sizeof firstAgain; ++i) duplicateFirst[30 + i] = firstAgain[i];
    images = GoodImages();
    images.provider.bytes = duplicateFirst.data();
    images.provider.snapshotFingerprint = FingerprintMfgImage(duplicateFirst.data(), duplicateFirst.size());
    assert(QualifyMfgProvider(images, {true, true}, 12).status == MfgQualificationStatus::Refused);

    auto duplicateSecond = providerBytes;
    const uint8_t secondAgain[] = {0x81, 0xFD, 0xB0, 1, 0, 0};
    for (size_t i = 0; i < sizeof secondAgain; ++i) duplicateSecond[10 + i] = secondAgain[i];
    images = GoodImages();
    images.provider.bytes = duplicateSecond.data();
    images.provider.snapshotFingerprint = FingerprintMfgImage(duplicateSecond.data(), duplicateSecond.size());
    assert(QualifyMfgProvider(images, {true, true}, 12).status == MfgQualificationStatus::Refused);
}

static void TestGatePlanIsBoundToQualifiedBytes()
{
    auto images = GoodImages();
    const auto qualification = QualifyMfgProvider(images, {true, true}, 12);
    MfgPatchPlan plan;
    assert(BuildAdaGatePlan(images, qualification, plan));
    assert(plan.generation == 12 && plan.count == 3);
    assert(plan.sites[0].address == reinterpret_cast<uintptr_t>(providerBytes.data() + 11));
    assert(plan.sites[1].address == reinterpret_cast<uintptr_t>(providerBytes.data() + 32));
    assert(plan.sites[2].address == reinterpret_cast<uintptr_t>(wrapperBytes.data() + 17));
    assert(plan.sites[2].expected[0] == 0xD1 && plan.sites[2].replacement[0] == 0xD2);
    auto changed = providerBytes;
    changed[40] = 0x04;
    images.provider.bytes = changed.data();
    assert(!BuildAdaGatePlan(images, qualification, plan));
    assert(plan.count == 0);
}

static void TestActiveWrapperPaths()
{
    assert(IsMfgWrapperPath(L"C:\\Game\\sl.dlss_g.dll"));
    assert(IsMfgWrapperPath(L"C:\\ProgramData\\NVIDIA\\NGX\\models\\sl_dlss_g_override_0\\versions\\134656\\files\\190_E658700.dll"));
    assert(IsMfgWrapperPath(L"C:\\ProgramData\\NVIDIA/NGX/models/sl_dlss_g_override_0\\versions\\134656\\files\\190_E658700.dll"));
    assert(!IsMfgWrapperPath(L"C:\\Other\\190_E658700.dll"));
    assert(!IsMfgWrapperPath(L"C:\\ProgramData\\NVIDIA\\NGX\\models\\sl_dlss_g_override_0\\versions\\134656\\files\\other.exe"));
    assert(!IsMfgWrapperPath(L"C:\\Game\\sl.dlss.dll"));
}

int main()
{
    TestExactSelectedPair();
    TestMissingDuplicateAndForeign();
    TestAdapterAbiTemporalAndGeneration();
    TestChangedImageCannotReuseVersion();
    TestGatePlanIsBoundToQualifiedBytes();
    TestActiveWrapperPaths();
}
