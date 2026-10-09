#pragma once

#include "MfgMappedPe.h"
#include "MfgTemporal.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Neurotic::Mfg
{
struct MfgTemporalDescriptors
{
    uint32_t profile = 0;
    std::array<uintptr_t, 12> slots {};
    size_t count = 0;
    std::vector<uint8_t> rebuilt;
};

// Read-only adaptation of the descriptor discovery in MFGAdaUnlock-RenoDx
// 1.1.5 midpoint.hpp (MIT). Every slot must name one unique temporal fatbin.
inline bool FindTemporalDescriptors(const uint8_t* module, MfgTemporalDescriptors& out)
{
    out = {};
    const auto pe = ReadMfgMappedPe(module);
    if (!pe.valid) return false;
    const auto start = reinterpret_cast<uintptr_t>(module);
    std::array<MfgSectionSnapshot, 32> snapshots;
    for (size_t i = 0; i < pe.sectionCount; ++i) snapshots[i].Capture(pe.sections[i]);
    const auto at = [&snapshots](uintptr_t address, size_t size) -> const uint8_t*
    {
        for (const auto& snapshot : snapshots)
            if (const auto* bytes = snapshot.At(address, size)) return bytes;
        return nullptr;
    };
    const auto namedString = [&at](uintptr_t address, const char* name)
    {
        const size_t size = std::strlen(name) + 1;
        const auto* bytes = at(address, size);
        return bytes && std::memcmp(bytes, name, size) == 0;
    };
    const uint8_t* selectedFat = nullptr;
    size_t selectedSize = 0;
    const Temporal::internal::TemporalProfile* selectedProfile = nullptr;
    for (size_t sectionIndex = 0; sectionIndex < pe.sectionCount; ++sectionIndex)
    {
        const auto& section = pe.sections[sectionIndex];
        const auto& snapshot = snapshots[sectionIndex];
        if (!section.readable || section.executable || snapshot.bytes.empty()) continue;
        for (size_t off = 0; off + sizeof(uint64_t) <= section.size; off += sizeof(uint64_t))
        {
            const uint8_t* slot = section.bytes + off;
            uint64_t value = 0;
            std::memcpy(&value, snapshot.bytes.data() + off, sizeof value);
            if (pe.imageSize < Temporal::internal::kOuterHeader || value < start ||
                value - start > pe.imageSize - Temporal::internal::kOuterHeader ||
                !pe.ReadableRange(static_cast<uintptr_t>(value), Temporal::internal::kOuterHeader)) continue;
            const auto* candidate = at(static_cast<uintptr_t>(value), Temporal::internal::kOuterHeader);
            if (!candidate || Temporal::internal::ReadU32(candidate) != Temporal::internal::kFatbinMagic) continue;
            const uint64_t declared = Temporal::internal::ReadU64(candidate + 8);
            if (declared > (16u << 20) || declared > pe.imageSize - (value - start) -
                Temporal::internal::kOuterHeader) continue;
            const size_t total = static_cast<size_t>(declared) + Temporal::internal::kOuterHeader;
            if (total < 1024 || !pe.ReadableRange(static_cast<uintptr_t>(value), total)) continue;
            candidate = at(static_cast<uintptr_t>(value), total);
            if (!candidate) continue;
            const auto* program = Temporal::internal::FindTemporalProfile(candidate, total);
            const Temporal::internal::TemporalProfile* named = nullptr;
            for (const auto& profile : Temporal::internal::kTemporalProfiles)
            {
                if (&profile != program) continue;
                uint64_t entryName = 0, descriptorName = 0;
                const auto slotAddress = reinterpret_cast<uintptr_t>(slot);
                if (!pe.ReadableRange(slotAddress + profile.entry_name_offset, 8) ||
                    !pe.ReadableRange(slotAddress + profile.descriptor_name_offset, 8)) continue;
                const auto* entry = at(slotAddress + profile.entry_name_offset, 8);
                const auto* descriptor = at(slotAddress + profile.descriptor_name_offset, 8);
                if (!entry || !descriptor) continue;
                std::memcpy(&entryName, entry, 8);
                std::memcpy(&descriptorName, descriptor, 8);
                if (namedString(static_cast<uintptr_t>(entryName), profile.entry_name) &&
                    namedString(static_cast<uintptr_t>(descriptorName), profile.descriptor_name))
                    { named = &profile; break; }
            }
            if (!named) continue;
            if (selectedFat && selectedFat != candidate) { out = {}; return false; }
            if (!selectedFat)
            {
                selectedFat = candidate;
                selectedSize = total;
                selectedProfile = named;
            }
            if (out.count == out.slots.size()) { out = {}; return false; }
            out.slots[out.count++] = reinterpret_cast<uintptr_t>(slot);
        }
    }
    // 310.2.1 has seven descriptor tables; 310.8/9 have eight.
    // A partial table match must never be published as a complete correction.
    if (!selectedFat || !selectedProfile || out.count != selectedProfile->descriptor_count)
        { out = {}; return false; }
    std::string why;
    if (!Temporal::internal::BuildTemporalFatbin(selectedFat, selectedSize,
        *selectedProfile, out.rebuilt, why)) { out = {}; return false; }
    out.profile = static_cast<uint32_t>(selectedProfile - Temporal::internal::kTemporalProfiles) + 1;
    return true;
}
}
