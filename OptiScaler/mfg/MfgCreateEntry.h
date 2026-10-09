#pragma once

#include <windows.h>
#include <intrin.h>
#include <nvsdk_ngx.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

extern "C" void NrMfgCreateForward();

// Atomic padded-entry publication adapted from dashdogy/RTX40MFG-Unlock,
// source/native/entry_detour.cpp. See licenses/RTX40MFG-Unlock-MIT.txt.
// This narrow observer does not select a provider on export lookup and does
// not implement runtime dispatch, broad provider patching or option control.
namespace Neurotic::Mfg
{
using MfgCreateFunction = decltype(&NVSDK_NGX_D3D12_CreateFeature);
using MfgBeforeCreate = void(*)(HMODULE, ID3D12GraphicsCommandList*) noexcept;
using MfgAllowCreate = bool(*)(HMODULE, ID3D12GraphicsCommandList*) noexcept;

class RetainedCreateEntries
{
    struct Entry
    {
        HMODULE owner = nullptr;
        uint8_t* target = nullptr;
        uint8_t* patchBase = nullptr;
        uint8_t shortBranch = 0;
        void* relay = nullptr;
        MfgBeforeCreate before = nullptr;
        MfgAllowCreate allow = nullptr;
        std::array<uint8_t, 5> branch {};
        std::atomic<MfgCreateFunction> original {nullptr};
        std::atomic<bool> ready {false};
    };
    // Never recycle a slot: cached entry pointers and return paths remain valid
    // for the process lifetime, even after the game releases its module handle.
    inline static std::array<Entry, 16> entries;
    inline static SRWLOCK installLock = SRWLOCK_INIT;

    static bool Read(const void* source, void* target, size_t count) noexcept
    {
        SIZE_T read = 0;
        return ReadProcessMemory(GetCurrentProcess(), source, target, count, &read) && read == count;
    }
    static bool ExecutableImage(const void* address, size_t count, HMODULE owner) noexcept
    {
        MEMORY_BASIC_INFORMATION m {};
        const uintptr_t p = reinterpret_cast<uintptr_t>(address);
        if (!VirtualQuery(address, &m, sizeof m) || m.State != MEM_COMMIT ||
            m.Type != MEM_IMAGE || m.AllocationBase != owner ||
            (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD protection = m.Protect & 0xff;
        return (protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY) &&
            p >= reinterpret_cast<uintptr_t>(m.BaseAddress) && p <= UINTPTR_MAX - count &&
            p + count <= reinterpret_cast<uintptr_t>(m.BaseAddress) + m.RegionSize;
    }
    static bool Current(const Entry& entry) noexcept
    {
        std::array<uint8_t, 5> bytes {};
        std::array<uint8_t, 2> entryBytes {};
        return entry.ready.load(std::memory_order_acquire) &&
            Read(entry.patchBase + 4, bytes.data(), bytes.size()) &&
            Read(entry.target, entryBytes.data(), entryBytes.size()) &&
            std::memcmp(bytes.data(), entry.branch.data(), 5) == 0 &&
            entryBytes[0] == 0xeb && entryBytes[1] == entry.shortBranch;
    }
    static void AbsoluteJump(uint8_t* bytes, const void* destination) noexcept
    {
        const uint8_t prefix[6] {0xff, 0x25, 0, 0, 0, 0};
        std::memcpy(bytes, prefix, 6);
        std::memcpy(bytes + 6, &destination, 8);
    }
    static bool MakeExecutable(void* memory, size_t size) noexcept
    {
        DWORD old = 0;
        return FlushInstructionCache(GetCurrentProcess(), memory, size) &&
            VirtualProtect(memory, size, PAGE_EXECUTE_READ, &old);
    }
    static void* NearRelay(uint8_t* entry, Entry* context) noexcept
    {
        SYSTEM_INFO info {};
        GetSystemInfo(&info);
        const uintptr_t origin = reinterpret_cast<uintptr_t>(entry);
        const uintptr_t minimum = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
        const uintptr_t maximum = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
        const uintptr_t reach = INT32_MAX;
        const uintptr_t low = origin > reach ? (std::max)(minimum, origin - reach) : minimum;
        const uintptr_t high = origin <= maximum - reach ? origin + reach : maximum;
        const uintptr_t granularity = info.dwAllocationGranularity;
        uintptr_t cursor = ((low + granularity - 1) / granularity) * granularity;
        while (cursor <= high)
        {
            MEMORY_BASIC_INFORMATION m {};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &m, sizeof m)) break;
            const uintptr_t base = reinterpret_cast<uintptr_t>(m.BaseAddress);
            const uintptr_t end = base <= UINTPTR_MAX - m.RegionSize ? base + m.RegionSize : UINTPTR_MAX;
            if (m.State == MEM_FREE && end - cursor >= 34)
            {
                void* relay = VirtualAlloc(reinterpret_cast<void*>(cursor), 34,
                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (relay)
                {
                    // Carry the retained entry and observer into the assembly
                    // forwarder without touching the caller's argument registers
                    // or return address. Both scratch registers are volatile.
                    auto* code = static_cast<uint8_t*>(relay);
                    code[0] = 0x49; code[1] = 0xba; // mov r10, context
                    std::memcpy(code + 2, &context, 8);
                    const auto observer = &PrepareForward;
                    code[10] = 0x49; code[11] = 0xbb; // mov r11, observer
                    std::memcpy(code + 12, &observer, 8);
                    AbsoluteJump(code + 20, reinterpret_cast<void*>(&NrMfgCreateForward));
                    if (MakeExecutable(relay, 34)) return relay;
                    VirtualFree(relay, 0, MEM_RELEASE);
                }
            }
            if (end <= cursor || end > UINTPTR_MAX - granularity) break;
            cursor = ((end + granularity - 1) / granularity) * granularity;
        }
        return nullptr;
    }
    static const std::atomic<MfgCreateFunction>* PrepareForward(Entry* context, ID3D12GraphicsCommandList* command,
        NVSDK_NGX_Feature feature) noexcept
    {
        auto& entry = *context;
        const auto original = entry.original.load(std::memory_order_acquire);
        // The trampoline is published before the atomic entry branch, so every
        // thread reaching this thunk has a valid original with the exact ABI.
        if (feature == NVSDK_NGX_Feature_FrameGeneration)
        {
            const bool current = Current(entry);
            if (current) entry.before(entry.owner, command);
            // Optional admission is used only by the experimental backend.
            // The existing RTX 40 observer retains its original behavior.
            if (entry.allow && (!current || !entry.allow(entry.owner, command)))
                return nullptr;
        }
        // The assembly owner tail-jumps to this target with the original stack.
        // A C++ call here changes _ReturnAddress(), which NVIDIA validates.
        static_assert(std::atomic<MfgCreateFunction>::is_always_lock_free);
        return original ? &entry.original : nullptr;
    }
public:
    bool IsCurrent(HMODULE owner, const void* address) const noexcept
    {
        for (const auto& entry : entries)
            if (entry.ready.load(std::memory_order_acquire) && entry.owner == owner && entry.target == address)
                return Current(entry);
        return false;
    }
#ifdef NR_MFG_ENTRY_TEST
    inline static void(*publicationProbe)(void*) noexcept = nullptr;
    inline static void(*stagedProbe)(void*) noexcept = nullptr;
#endif
    bool Install(HMODULE owner, void* address, MfgBeforeCreate before, MfgAllowCreate allow = nullptr) noexcept
    {
        if (!owner || !address || !before || (reinterpret_cast<uintptr_t>(address) & 15) ||
            !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)) return false;
        // No waiting in loader callbacks or nested export lookup. A missed
        // attempt can be retried at the subsequent GetProcAddress boundary.
        if (!TryAcquireSRWLockExclusive(&installLock)) return false;
        struct Guard { ~Guard() { ReleaseSRWLockExclusive(&installLock); } } guard;
        for (const auto& entry : entries)
            if (entry.target == address) return entry.owner == owner && entry.before == before && entry.allow == allow && Current(entry);
        PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg {};
        PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY cet {};
        if (!GetProcessMitigationPolicy(GetCurrentProcess(), ProcessControlFlowGuardPolicy, &cfg, sizeof cfg) ||
            !GetProcessMitigationPolicy(GetCurrentProcess(), ProcessUserShadowStackPolicy, &cet, sizeof cet) ||
            cfg.StrictMode || cfg.EnableXfg || cet.EnableUserShadowStackStrictMode ||
            cet.BlockNonCetBinaries || cet.BlockNonCetBinariesNonEhcont) return false;
        auto* target = static_cast<uint8_t*>(address);
        std::array<uint8_t, 6> prior {};
        constexpr std::array<uint8_t, 6> supported {0x40,0x53,0x55,0x56,0x41,0x56};
        if (!ExecutableImage(target, prior.size(), owner) ||
            !Read(target, prior.data(), prior.size()) || prior != supported) return false;
        alignas(16) std::array<uint8_t, 16> prefix {};
        size_t paddingDistance = 0;
        // 310.2.1 has only two padding bytes immediately before Create. Its
        // verified preceding 12-byte padding run starts at -92, leaving that
        // neighboring function's conventional -5 hotpatch slot untouched.
        // Both layouts retain the same atomic two-byte entry instruction.
        for (size_t distance : {size_t{16}, size_t{96}}) {
            if ((reinterpret_cast<uintptr_t>(target - distance) & 15) ||
                !ExecutableImage(target - distance, distance + prior.size(), owner) ||
                !Read(target - distance, prefix.data(), prefix.size())) continue;
            bool padding = true;
            for (size_t i = 4; i < prefix.size(); ++i) padding &= prefix[i] == 0xcc;
            if (padding) { paddingDistance = distance; break; }
        }
        if (!paddingDistance) return false;
        auto* patchBase = target - paddingDistance;
        size_t index = 0;
        while (index < entries.size() && entries[index].target) ++index;
        if (index == entries.size()) return false;
        HMODULE held = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(address), &held)) return false;
        if (held != owner) { FreeLibrary(held); return false; }
#ifdef NR_MFG_ENTRY_TEST
        if (publicationProbe) publicationProbe(address);
#endif
        void* relay = NearRelay(patchBase + 9, &entries[index]);
        void* trampoline = VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (trampoline)
        {
            // Replay the validated preimage, never bytes a competing installer
            // may have changed while allocation/protection calls were running.
            const uint8_t push[2] {0x40, 0x53};
            std::memcpy(trampoline, push, 2);
            AbsoluteJump(static_cast<uint8_t*>(trampoline) + 2, target + 2);
            if (!MakeExecutable(trampoline, 16)) { VirtualFree(trampoline, 0, MEM_RELEASE); trampoline = nullptr; }
        }
        DWORD protection = 0;
        if (!relay || !trampoline || !VirtualProtect(patchBase, paddingDistance + 2, PAGE_EXECUTE_WRITECOPY, &protection))
        {
            if (relay) VirtualFree(relay, 0, MEM_RELEASE);
            if (trampoline) VirtualFree(trampoline, 0, MEM_RELEASE);
            FreeLibrary(held);
            return false;
        }
        auto& entry = entries[index];
        entry.owner = held; entry.target = target; entry.relay = relay; entry.before = before; entry.allow = allow;
        entry.patchBase = patchBase;
        entry.shortBranch = static_cast<uint8_t>(2 - static_cast<int>(paddingDistance));
        entry.branch[0] = 0xe9;
        const auto displacement = static_cast<int32_t>(reinterpret_cast<intptr_t>(relay) - reinterpret_cast<intptr_t>(patchBase + 9));
        std::memcpy(entry.branch.data() + 1, &displacement, 4);
        // Materialize the image's copy-on-write page before atomic publication.
        auto* materialize = reinterpret_cast<volatile uint8_t*>(patchBase);
        materialize[0] = materialize[0];
        entry.original.store(reinterpret_cast<MfgCreateFunction>(trampoline), std::memory_order_release);
        entry.ready.store(true, std::memory_order_release);
        // Stage our five-byte jump within the verified padding, disjoint from the conventional -5
        // hotpatch slot. Atomically compare the entire aligned prefix: a newly
        // foreign branch refuses, without overwriting it. If another installer
        // subsequently uses -5, its destination remains independent of ours.
        alignas(16) LONG64 comparand[2] {};
        std::memcpy(comparand, prefix.data(), 16);
        auto replacement = prefix;
        std::memcpy(replacement.data() + 4, entry.branch.data(), 5);
        LONG64 exchange[2] {};
        std::memcpy(exchange, replacement.data(), 16);
        const bool staged = _InterlockedCompareExchange128(reinterpret_cast<volatile LONG64*>(patchBase),
            exchange[1], exchange[0], comparand) != 0;
        if (staged) FlushInstructionCache(GetCurrentProcess(), patchBase, 16);
#ifdef NR_MFG_ENTRY_TEST
        if (staged && stagedProbe) stagedProbe(address);
#endif
        const auto observed = staged ? _InterlockedCompareExchange16(reinterpret_cast<volatile SHORT*>(target),
            static_cast<SHORT>((static_cast<unsigned>(entry.shortBranch) << 8) | 0xeb),
            static_cast<SHORT>(0x5340)) : static_cast<SHORT>(0);
        FlushInstructionCache(GetCurrentProcess(), target, 2);
        DWORD ignored = 0;
        const bool restored = VirtualProtect(patchBase, paddingDistance + 2, protection, &ignored) != 0;
        if (observed != static_cast<SHORT>(0x5340) || !restored)
        {
            // Do not undo foreign entry bytes, free a possibly reachable relay
            // or enable mutation after uncertain publication/protection state.
            entry.ready.store(false, std::memory_order_release);
            return false;
        }
        return Current(entry);
    }
};
}
