#pragma once

#include "MfgPublication.h"
#include "MfgProviderQualification.h"

namespace Neurotic::Mfg
{
// Adapted architecture and wrapper ceiling sites from MFGAdaUnlock-RenoDx
// 1.1.5 (MIT). The lower immediate 0x1b0 -> 0x190 admits Ada; the wrapper
// cmov source D1 -> D2 leaves the caller's requested count intact.
inline bool BuildAdaGatePlan(const MfgProviderImages& images,
    const MfgQualification& qualification, MfgPatchPlan& plan) noexcept
{
    plan = {};
    if (qualification.status != MfgQualificationStatus::ReadyForExperiment ||
        qualification.providerIdentity != images.provider.imageIdentity ||
        qualification.wrapperIdentity != images.wrapper.imageIdentity ||
        qualification.providerGeneration != images.provider.generation ||
        qualification.providerGeneration != images.wrapper.generation ||
        FingerprintMfgImage(images.provider.bytes, images.provider.size) != qualification.providerFingerprint ||
        FingerprintMfgImage(images.wrapper.bytes, images.wrapper.size) != qualification.wrapperFingerprint)
        return false;

    const auto add = [&plan](uintptr_t address, uint8_t before, uint8_t after) noexcept
    {
        if (plan.count >= plan.sites.size()) return false;
        auto& site = plan.sites[plan.count++];
        site.address = address;
        site.size = 1;
        site.expected[0] = before;
        site.replacement[0] = after;
        return true;
    };
    const auto* provider = images.provider.bytes;
    const auto providerSize = images.provider.size;
    size_t firstArchitectureSites = 0, secondArchitectureSites = 0;
    for (size_t i = 0; i < providerSize; ++i)
    {
        if (i + 5 <= providerSize && provider[i] == 0x3D && provider[i + 1] == 0xB0 &&
            provider[i + 2] == 1 && provider[i + 3] == 0 && provider[i + 4] == 0)
        {
            ++firstArchitectureSites;
            if (!add(reinterpret_cast<uintptr_t>(provider + i + 1), 0xB0, 0x90)) return false;
        }
        if (i + 6 <= providerSize && provider[i] == 0x81 && provider[i + 1] == 0xFD &&
            provider[i + 2] == 0xB0 && provider[i + 3] == 1 && provider[i + 4] == 0 &&
            provider[i + 5] == 0)
        {
            ++secondArchitectureSites;
            if (!add(reinterpret_cast<uintptr_t>(provider + i + 2), 0xB0, 0x90)) return false;
        }
    }
    const size_t expectedSecond = images.provider.temporalProfile == 3 ? 0 : 1;
    if (firstArchitectureSites != 1 || secondArchitectureSites != expectedSecond ||
        plan.count != qualification.architectureSites) { plan = {}; return false; }
    const auto* wrapper = images.wrapper.bytes;
    const auto wrapperSize = images.wrapper.size;
    size_t wrapperCount = 0;
    for (size_t i = 0; i + 10 <= wrapperSize; ++i)
    {
        if (wrapper[i] != 0xBA || (wrapper[i + 1] != 3 && wrapper[i + 1] != 5) ||
            wrapper[i + 2] || wrapper[i + 3] || wrapper[i + 4] ||
            wrapper[i + 5] != 0x3B || wrapper[i + 6] != 0xCA ||
            wrapper[i + 7] != 0x0F || wrapper[i + 8] != 0x42 || wrapper[i + 9] != 0xD1)
            continue;
        ++wrapperCount;
        if (!add(reinterpret_cast<uintptr_t>(wrapper + i + 9), 0xD1, 0xD2)) return false;
    }
    if (wrapperCount != qualification.wrapperSites ||
        plan.count != qualification.architectureSites + qualification.wrapperSites)
    { plan = {}; return false; }
    plan.generation = qualification.providerGeneration;
    return true;
}
}
