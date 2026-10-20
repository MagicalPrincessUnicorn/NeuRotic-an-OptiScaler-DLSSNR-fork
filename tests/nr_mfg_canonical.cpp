#include "../OptiScaler/mfg/MfgTemporalDescriptor.h"
#include "../OptiScaler/mfg/MfgProviderQualification.h"
#include "../OptiScaler/mfg/MfgAdaPlan.h"

#include <windows.h>
#include <winver.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

using namespace Neurotic::Mfg;

struct ImageView
{
    HMODULE module = nullptr;
    const uint8_t* bytes = nullptr;
    ~ImageView() { if (module) FreeLibrary(module); }
    bool Open(const wchar_t* path)
    {
        // Maps relocations without resolving imports or calling the DLL entry point.
        module = LoadLibraryExW(path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
        bytes = reinterpret_cast<const uint8_t*>(module);
        return bytes != nullptr;
    }
};

static bool Version(const wchar_t* path, MfgProviderVersion& out)
{
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    if (!size || size > (1u << 20)) return false;
    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return false;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) ||
        !info || length < sizeof *info) return false;
    out = {HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
        HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS)};
    return true;
}

static bool MappedVersion(HMODULE module, MfgProviderVersion& out)
{
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));
    if (!resource) return false;
    const DWORD size = SizeofResource(module, resource);
    const auto loaded = LoadResource(module, resource);
    const auto* bytes = static_cast<const uint8_t*>(LockResource(loaded));
    if (!bytes || size < sizeof(VS_FIXEDFILEINFO) || size > (1u << 20)) return false;
    for (size_t offset = 0; offset + sizeof(VS_FIXEDFILEINFO) <= size; offset += 4)
    {
        VS_FIXEDFILEINFO info {};
        std::memcpy(&info, bytes + offset, sizeof info);
        if (info.dwSignature != 0xFEEF04BD || info.dwStrucVersion != 0x00010000) continue;
        out = {HIWORD(info.dwFileVersionMS), LOWORD(info.dwFileVersionMS),
            HIWORD(info.dwFileVersionLS), LOWORD(info.dwFileVersionLS)};
        return true;
    }
    return false;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) return 2;
    ImageView provider, wrapper;
    if (!provider.Open(argv[1]) || !wrapper.Open(argv[2])) return 3;
    const auto providerPe = ReadMfgMappedPe(provider.bytes);
    const auto wrapperPe = ReadMfgMappedPe(wrapper.bytes);
    const auto providerText = providerPe.ExecutableText();
    const auto wrapperText = wrapperPe.ExecutableText();
    if (!providerText.bytes || !wrapperText.bytes) return 4;
    MfgTemporalDescriptors temporal;
    if (!FindTemporalDescriptors(provider.bytes, temporal)) return 5;
    MfgProviderVersion pv, wv;
    if (!Version(argv[1], pv) || !Version(argv[2], wv)) return 6;
    MfgProviderVersion pm, wm;
    if (!MappedVersion(provider.module, pm) || !MappedVersion(wrapper.module, wm) ||
        std::memcmp(&pm, &pv, sizeof pm) || std::memcmp(&wm, &wv, sizeof wm)) return 8;
    MfgProviderImages images;
    images.provider = {providerText.bytes, providerText.size,
        reinterpret_cast<uintptr_t>(provider.bytes), 1, true, 1, pv, temporal.profile, 5,
        FingerprintMfgImage(providerText.bytes, providerText.size)};
    images.wrapper = {wrapperText.bytes, wrapperText.size,
        reinterpret_cast<uintptr_t>(wrapper.bytes), 1, true, 1, wv, 0, 5,
        FingerprintMfgImage(wrapperText.bytes, wrapperText.size)};
    images.boundProviderIdentity = reinterpret_cast<uintptr_t>(provider.bytes);
    images.boundWrapperIdentity = reinterpret_cast<uintptr_t>(wrapper.bytes);
    const auto qualification = QualifyMfgProvider(images, {true, true}, 1);
    MfgPatchPlan plan;
    const bool gate = BuildAdaGatePlan(images, qualification, plan);
    std::printf("provider=%u.%u.%u.%u wrapper=%u.%u.%u.%u profile=%u descriptors=%zu rebuilt=%zu qualification=%u gate=%d sites=%zu\n",
        pv.major, pv.minor, pv.patch, pv.build, wv.major, wv.minor, wv.patch, wv.build,
        temporal.profile, temporal.count, temporal.rebuilt.size(),
        static_cast<unsigned>(qualification.status), gate, plan.count);
    return qualification.status == MfgQualificationStatus::ReadyForExperiment && gate &&
        temporal.profile == 1 && temporal.count == 8 && plan.count == 3 ? 0 : 7;
}
