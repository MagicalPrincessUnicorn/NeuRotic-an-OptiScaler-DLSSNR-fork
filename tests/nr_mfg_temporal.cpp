#include "../OptiScaler/mfg/MfgTemporal.h"
#include "../OptiScaler/mfg/MfgTemporalDescriptor.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Neurotic::Mfg::Temporal::internal;

template<class T> static void Put(std::vector<uint8_t>& bytes, size_t off, T value)
{
    std::memcpy(bytes.data() + off, &value, sizeof value);
}

static std::vector<uint8_t> MakeFatbin(bool duplicateLabel = false, bool missingMidpoint = false)
{
    std::string ptx = ".entry main_kernel(\n.param .align 8 .b8 main_kernel_param_0[144]\n"
        ".reg .f32 %f<1362>;\n$L__BB0_3:\n";
    if (duplicateLabel) ptx += "$L__BB0_3:\n";
    for (int i = 0; i < (missingMidpoint ? 103 : 104); ++i)
        ptx += "mul.ftz.f32 %f1, %f2, 0f3F000000;\n";
    assert(ptx.size() < 99362);
    ptx.resize(99362, ' ');

    std::vector<uint8_t> packed;
    packed.push_back(0xF0); // One all-literal LZ4 block.
    size_t extension = ptx.size() - 15;
    while (extension >= 255) { packed.push_back(255); extension -= 255; }
    packed.push_back(static_cast<uint8_t>(extension));
    packed.insert(packed.end(), ptx.begin(), ptx.end());

    std::vector<uint8_t> fat(16 + 64 + packed.size());
    Put<uint32_t>(fat, 0, 0xBA55ED50);
    Put<uint16_t>(fat, 6, 16);
    Put<uint64_t>(fat, 8, static_cast<uint64_t>(fat.size() - 16));
    Put<uint16_t>(fat, 16, 1);
    Put<uint32_t>(fat, 20, 64);
    Put<uint64_t>(fat, 24, static_cast<uint64_t>(packed.size()));
    Put<uint32_t>(fat, 32, static_cast<uint32_t>(packed.size()));
    Put<uint32_t>(fat, 44, 89);
    Put<uint64_t>(fat, 72, 99362);
    std::memcpy(fat.data() + 80, packed.data(), packed.size());
    return fat;
}

static size_t Count(const std::string& text, const std::string& needle)
{
    size_t count = 0, pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) { ++count; pos += needle.size(); }
    return count;
}

// Catch the old scanner's AV in the test process without a Windows crash dialog.
static int DiscoverWithoutFault(const uint8_t* image, Neurotic::Mfg::MfgTemporalDescriptors& found)
{
    __try { return Neurotic::Mfg::FindTemporalDescriptors(image, found) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void Require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void TestMappedTemporalDescriptor()
{
    auto fat = MakeFatbin();
    std::vector<uint8_t> image(0x30000);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data() + 0x80);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 2;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(image.size());
    auto* section = IMAGE_FIRST_SECTION(nt);
    std::memcpy(section[0].Name, ".text", 5);
    section[0].VirtualAddress = 0x1000;
    section[0].Misc.VirtualSize = 0x1000;
    section[0].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
    std::memcpy(section[1].Name, ".rdata", 6);
    section[1].VirtualAddress = 0x2000;
    section[1].Misc.VirtualSize = 0x20000;
    section[1].Characteristics = IMAGE_SCN_MEM_READ;
    std::memcpy(image.data() + 0x4000, fat.data(), fat.size());
    std::memcpy(image.data() + 0x3000, "main_kernel", sizeof("main_kernel"));
    std::memcpy(image.data() + 0x3020, "dlfg_kernel", sizeof("dlfg_kernel"));
    const auto base = reinterpret_cast<uintptr_t>(image.data());
    for (size_t i = 0; i < 8; ++i)
    {
        const size_t descriptor = 0x2100 + i * 0x40;
        Put<uint64_t>(image, descriptor, base + 0x4000);
        Put<uint64_t>(image, descriptor + 0x10, base + 0x3000);
        Put<uint64_t>(image, descriptor + 0x28, base + 0x3020);
    }
    Neurotic::Mfg::MfgTemporalDescriptors found;
    assert(Neurotic::Mfg::FindTemporalDescriptors(image.data(), found));
    assert(found.profile == 1 && found.count == 8);
    assert(found.slots[0] == base + 0x2100);
    assert(!found.rebuilt.empty());
    Put<uint64_t>(image, 0x2100 + 7 * 0x40, 0);
    assert(!Neurotic::Mfg::FindTemporalDescriptors(image.data(), found));
    Put<uint64_t>(image, 0x2100 + 7 * 0x40, base + 0x4000);

    // A loader-discarded readable PE section must not crash discovery, nor
    // hide eight valid descriptors in another section. Test actual protections.
    nt->FileHeader.NumberOfSections = 3;
    section[2].VirtualAddress = 0x22000;
    section[2].Misc.VirtualSize = 0x1000;
    section[2].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_DISCARDABLE;
    auto* mapped = static_cast<uint8_t*>(VirtualAlloc(nullptr, image.size(),
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(mapped != nullptr, "allocate protected provider fixture");
    std::memcpy(mapped, image.data(), image.size());
    const auto mappedBase = reinterpret_cast<uintptr_t>(mapped);
    for (size_t i = 0; i < 8; ++i)
        for (size_t offset : {size_t(0), size_t(0x10), size_t(0x28)})
        {
            uint64_t pointer = 0;
            auto* slot = mapped + 0x2100 + i * 0x40 + offset;
            std::memcpy(&pointer, slot, 8);
            pointer = pointer - base + mappedBase;
            std::memcpy(slot, &pointer, 8);
        }
    DWORD prior = 0;
    Require(VirtualProtect(mapped + 0x22000, 0x1000, PAGE_NOACCESS, &prior) != 0,
        "protect discarded section");
    Require(DiscoverWithoutFault(mapped, found) == 1,
        "discovery must survive inaccessible PE section and find valid descriptors");
    Require(found.count == 8 && found.slots[0] == mappedBase + 0x2100,
        "discovery must retain original mapped addresses");
    const auto mappedPe = Neurotic::Mfg::ReadMfgMappedPe(mapped);
    Neurotic::Mfg::MfgSectionSnapshot text;
    Require(text.Capture(mappedPe.ExecutableText()), "capture executable section for qualification");
    const auto copiedText = reinterpret_cast<uintptr_t>(text.bytes.data());
    Require(text.OriginalAddress(copiedText + 17) == mappedBase + 0x1011 &&
        text.OriginalAddress(copiedText + text.bytes.size()) == 0,
        "patch destinations must map to original code, rejecting outside addresses");
    Require(VirtualProtect(mapped + 0x1000, 0x1000, PAGE_NOACCESS, &prior) != 0,
        "make captured executable section unavailable");
    Require(text.bytes[17] == 0, "owned code remains readable after original becomes inaccessible");
    Require(!text.Capture(mappedPe.ExecutableText()) && text.bytes.empty() && text.address == 0,
        "inaccessible executable section must refuse qualification and clear stale snapshot");
    Require(VirtualProtect(mapped + 0x22000, 0x1000, PAGE_READWRITE | PAGE_GUARD, &prior) != 0,
        "guard unused section");
    Require(DiscoverWithoutFault(mapped, found) == 1, "guarded section must not fault");
    MEMORY_BASIC_INFORMATION region {};
    VirtualQuery(mapped + 0x22000, &region, sizeof region);
    Require((region.Protect & PAGE_GUARD) != 0, "discovery must not consume guard protection");
    Require(VirtualProtect(mapped + 0x4000, 0x1000, PAGE_NOACCESS, &prior) != 0,
        "protect required fatbin");
    Require(DiscoverWithoutFault(mapped, found) == 0,
        "unreadable required program must refuse without an access violation");
    Require(found.count == 0 && found.rebuilt.empty(), "refusal must clear descriptors");
    Require(VirtualProtect(mapped, 0x1000, PAGE_NOACCESS, &prior) != 0, "protect PE header");
    Require(DiscoverWithoutFault(mapped, found) == 0, "unreadable header must refuse safely");
    VirtualFree(mapped, 0, MEM_RELEASE);
    section[1].Misc.VirtualSize = 0x40000;
    assert(!Neurotic::Mfg::FindTemporalDescriptors(image.data(), found));
}

int main()
{
    const auto fat = MakeFatbin();
    std::vector<uint8_t> rebuilt;
    std::string why;
    assert(BuildTemporalFatbin(fat.data(), fat.size(), kTemporalProfiles[0], rebuilt, why));
    assert(!rebuilt.empty());
    const std::string output(reinterpret_cast<const char*>(rebuilt.data()), rebuilt.size());
    assert(Count(output, "ld.param.f32 %f134") == 1);
    assert(Count(output, "mul.ftz.f32 %f1, %f2, %f136;") == 52);
    assert(Count(output, "mul.ftz.f32 %f1, %f2, %f134;") == 52);
    assert(Count(output, "0f3F000000") == 0);
    assert(!BuildTemporalFatbin(fat.data(), fat.size(), kTemporalProfiles[1], rebuilt, why));
    const auto duplicate = MakeFatbin(true);
    assert(!BuildTemporalFatbin(duplicate.data(), duplicate.size(), kTemporalProfiles[0], rebuilt, why));
    const auto missing = MakeFatbin(false, true);
    assert(!BuildTemporalFatbin(missing.data(), missing.size(), kTemporalProfiles[0], rebuilt, why));
    auto malformed = fat;
    Put<uint32_t>(malformed, 32, 0xffffffffu);
    assert(!BuildTemporalFatbin(malformed.data(), malformed.size(), kTemporalProfiles[0], rebuilt, why));
    malformed = fat;
    Put<uint64_t>(malformed, 24, UINT64_MAX);
    assert(!BuildTemporalFatbin(malformed.data(), malformed.size(), kTemporalProfiles[0], rebuilt, why));
    malformed = fat;
    Put<uint64_t>(malformed, 8, UINT64_MAX);
    assert(!BuildTemporalFatbin(malformed.data(), malformed.size(), kTemporalProfiles[0], rebuilt, why));
    TestMappedTemporalDescriptor();
    std::puts("PASS: protected PE discovery, owned text snapshots, original addresses and temporal qualification");
}
