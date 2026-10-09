#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Neurotic::Mfg
{
// Section flags describe the file, not current page accessibility. Never touch
// guarded pages; RPM also contains protection/unmap races after VirtualQuery.
inline bool ReadMfgMemory(const void* source, void* destination, size_t size) noexcept
{
    auto cursor = reinterpret_cast<uintptr_t>(source);
    if (!cursor || !destination || !size || cursor > UINTPTR_MAX - size) return false;
    const auto end = cursor + size;
    while (cursor < end)
    {
        MEMORY_BASIC_INFORMATION page {};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &page, sizeof page) ||
            page.State != MEM_COMMIT || (page.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto protection = page.Protect & 0xff;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
        const auto begin = reinterpret_cast<uintptr_t>(page.BaseAddress);
        if (begin > cursor || page.RegionSize > UINTPTR_MAX - begin ||
            begin + page.RegionSize <= cursor) return false;
        cursor = (end < begin + page.RegionSize) ? end : begin + page.RegionSize;
    }
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), source, destination, size, &copied) && copied == size;
}

struct MfgMappedSection
{
    const uint8_t* bytes = nullptr;
    size_t size = 0;
    bool executable = false;
    bool readable = false;
    bool text = false;
};

struct MfgMappedPe
{
    const uint8_t* base = nullptr;
    size_t imageSize = 0;
    std::array<MfgMappedSection, 32> sections {};
    size_t sectionCount = 0;
    bool valid = false;

    bool ReadableRange(uintptr_t address, size_t length) const noexcept
    {
        if (!valid || !length || address > UINTPTR_MAX - length) return false;
        for (size_t i = 0; i < sectionCount; ++i)
        {
            const auto& section = sections[i];
            const auto begin = reinterpret_cast<uintptr_t>(section.bytes);
            if (section.readable && address >= begin && address - begin <= section.size &&
                length <= section.size - (address - begin)) return true;
        }
        return false;
    }

    MfgMappedSection ExecutableText() const noexcept
    {
        MfgMappedSection chosen;
        if (!valid) return chosen;
        for (size_t i = 0; i < sectionCount; ++i)
        {
            const auto& section = sections[i];
            if (!section.text || !section.executable || !section.readable) continue;
            if (chosen.bytes) return {}; // Ambiguous .text sections.
            chosen = section;
        }
        return chosen;
    }
};

// The caller supplies an OS-mapped and lifetime-held module base. This parser
// bounds all subsequent section walks; it does not authenticate the module.
inline MfgMappedPe ReadMfgMappedPe(const uint8_t* base) noexcept
{
    MfgMappedPe out;
    if (!base) return out;
    alignas(IMAGE_NT_HEADERS64) std::array<uint8_t, 0x1000> header {};
    if (!ReadMfgMemory(base, header.data(), header.size())) return out;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(header.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) ||
        dos->e_lfanew > 0x400) return out;
    const auto ntOffset = static_cast<size_t>(dos->e_lfanew);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(header.data() + ntOffset);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        !nt->FileHeader.NumberOfSections || nt->FileHeader.NumberOfSections > out.sections.size())
        return out;
    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    const size_t sectionTableEnd = ntOffset + sizeof(IMAGE_NT_HEADERS64) +
        nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
    if (imageSize < 0x1000 || imageSize > (128u << 20) || sectionTableEnd > 0x1000)
        return out;
    out.base = base;
    out.imageSize = imageSize;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const size_t address = section[i].VirtualAddress;
        const size_t size = section[i].Misc.VirtualSize;
        if (!size || address >= imageSize || size > imageSize - address) return {};
        MfgMappedSection view;
        view.bytes = base + address;
        view.size = size;
        view.executable = (section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        view.readable = (section[i].Characteristics & IMAGE_SCN_MEM_READ) != 0;
        view.text = std::memcmp(section[i].Name, ".text", 5) == 0 &&
            (section[i].Name[5] == 0 || section[i].Name[5] == '$');
        out.sections[out.sectionCount++] = view;
    }
    out.valid = true;
    return out;
}

// Only owned bytes are exposed to parsers. Original addresses remain separate
// for publication, whose expected-byte check still runs against live memory.
struct MfgSectionSnapshot
{
    uintptr_t address = 0;
    std::vector<uint8_t> bytes;

    bool Capture(const MfgMappedSection& section)
    {
        address = 0;
        bytes.clear();
        if (!section.readable || !section.bytes || !section.size || section.size > (128u << 20)) return false;
        bytes.resize(section.size);
        if (!ReadMfgMemory(section.bytes, bytes.data(), bytes.size())) { bytes.clear(); return false; }
        address = reinterpret_cast<uintptr_t>(section.bytes);
        return true;
    }

    const uint8_t* At(uintptr_t source, size_t length) const noexcept
    {
        if (!address || source < address || source - address > bytes.size() ||
            length > bytes.size() - (source - address)) return nullptr;
        return bytes.data() + (source - address);
    }

    uintptr_t OriginalAddress(uintptr_t copied) const noexcept
    {
        const auto begin = reinterpret_cast<uintptr_t>(bytes.data());
        return address && copied >= begin && copied - begin < bytes.size() ? address + copied - begin : 0;
    }
};
}
