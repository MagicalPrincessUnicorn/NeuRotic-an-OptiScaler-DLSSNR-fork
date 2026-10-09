#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Neurotic::Mfg
{
enum class MfgPublicationStatus : uint8_t
{
    NoMatch, ForeignPrepatched, Stale, RollbackVerified, Indeterminate, OwnedPublication
};

struct MfgPatchSite
{
    uintptr_t address = 0;
    uint8_t size = 0;
    std::array<uint8_t, 8> expected {};
    std::array<uint8_t, 8> replacement {};
};

struct MfgPatchPlan
{
    uint64_t generation = 0;
    size_t count = 0;
    std::array<MfgPatchSite, 16> sites {};
};

struct MfgPublicationReceipt { MfgPatchPlan plan; bool owned = false; };
struct MfgPublicationResult
{
    MfgPublicationStatus status = MfgPublicationStatus::NoMatch;
    MfgPublicationReceipt receipt;
};
struct MfgPublicationLatch
{
    bool indeterminate = false;
    // Call only after the owning module has been observed unloaded.
    void ClearAfterUnload() noexcept { indeterminate = false; }
};

// Production implementation uses VirtualProtect and FlushInstructionCache.
// Tests inject failures at each step, including writes that mutate then fail.
class MfgMutationOps
{
public:
    virtual ~MfgMutationOps() = default;
    virtual bool Read(uintptr_t address, uint8_t* out, size_t size) noexcept = 0;
    virtual bool MakeWritable(uintptr_t address, size_t size, uintptr_t& prior) noexcept = 0;
    virtual bool Write(uintptr_t address, const uint8_t* bytes, size_t size) noexcept = 0;
    virtual bool Flush(uintptr_t address, size_t size) noexcept = 0;
    virtual bool RestoreProtection(uintptr_t address, size_t size, uintptr_t prior) noexcept = 0;
};

inline bool MfgBytesEqual(const uint8_t* lhs, const uint8_t* rhs, size_t n) noexcept
{
    for (size_t i = 0; i < n; ++i) if (lhs[i] != rhs[i]) return false;
    return true;
}

inline MfgPublicationStatus RestoreMfgPatches(MfgMutationOps& ops,
    const MfgPublicationReceipt& receipt, uint64_t generation) noexcept
{
    if (!receipt.owned || !generation || receipt.plan.generation != generation)
        return MfgPublicationStatus::Stale;
    const auto& plan = receipt.plan;
    for (size_t i = 0; i < plan.count; ++i)
    {
        uint8_t current[8] {};
        const auto& site = plan.sites[i];
        if (!ops.Read(site.address, current, site.size) ||
            !MfgBytesEqual(current, site.replacement.data(), site.size))
            return MfgPublicationStatus::Indeterminate;
    }
    bool allRestored = true;
    for (size_t n = plan.count; n > 0; --n)
    {
        const auto& site = plan.sites[n - 1];
        uintptr_t prior = 0;
        if (!ops.MakeWritable(site.address, site.size, prior)) { allRestored = false; continue; }
        const bool wrote = ops.Write(site.address, site.expected.data(), site.size);
        const bool flushed = ops.Flush(site.address, site.size);
        const bool protectedAgain = ops.RestoreProtection(site.address, site.size, prior);
        uint8_t current[8] {};
        allRestored = wrote && flushed && protectedAgain &&
            ops.Read(site.address, current, site.size) &&
            MfgBytesEqual(current, site.expected.data(), site.size) && allRestored;
    }
    return allRestored ? MfgPublicationStatus::RollbackVerified : MfgPublicationStatus::Indeterminate;
}

inline MfgPublicationResult PublishMfgPatches(MfgMutationOps& ops,
    const MfgPatchPlan& plan, uint64_t generation, MfgPublicationLatch& latch) noexcept
{
    MfgPublicationResult result;
    if (latch.indeterminate)
    {
        result.status = MfgPublicationStatus::Indeterminate;
        return result;
    }
    if (!generation || !plan.generation || generation != plan.generation)
    {
        result.status = MfgPublicationStatus::Stale;
        return result;
    }
    if (!plan.count || plan.count > plan.sites.size()) return result;
    for (size_t i = 0; i < plan.count; ++i)
    {
        const auto& site = plan.sites[i];
        if (!site.size || site.size > 8 || site.address > UINTPTR_MAX - site.size) return result;
        for (size_t j = 0; j < i; ++j)
        {
            const auto& other = plan.sites[j];
            if (site.address < other.address + other.size && other.address < site.address + site.size)
                return result;
        }
        uint8_t current[8] {};
        if (!ops.Read(site.address, current, site.size)) return result;
        if (!MfgBytesEqual(current, site.expected.data(), site.size))
        {
            result.status = MfgBytesEqual(current, site.replacement.data(), site.size) ?
                MfgPublicationStatus::ForeignPrepatched : MfgPublicationStatus::NoMatch;
            return result;
        }
    }

    size_t touched = 0;
    bool protectionFailure = false;
    bool completed = true;
    for (size_t i = 0; i < plan.count; ++i)
    {
        const auto& site = plan.sites[i];
        uintptr_t prior = 0;
        if (!ops.MakeWritable(site.address, site.size, prior)) { completed = false; break; }
        touched = i + 1; // A failed Write may already have modified this site.
        const bool wrote = ops.Write(site.address, site.replacement.data(), site.size);
        const bool flushed = ops.Flush(site.address, site.size);
        const bool protectedAgain = ops.RestoreProtection(site.address, site.size, prior);
        if (!protectedAgain) protectionFailure = true;
        uint8_t current[8] {};
        if (!wrote || !flushed || !protectedAgain ||
            !ops.Read(site.address, current, site.size) ||
            !MfgBytesEqual(current, site.replacement.data(), site.size))
        { completed = false; break; }
    }
    if (completed && touched == plan.count && !protectionFailure)
    {
        result.status = MfgPublicationStatus::OwnedPublication;
        result.receipt = {plan, true};
        return result;
    }
    MfgPublicationReceipt touchedReceipt;
    touchedReceipt.plan = plan;
    touchedReceipt.plan.count = touched;
    touchedReceipt.owned = touched != 0;
    result.status = touched ? RestoreMfgPatches(ops, touchedReceipt, generation) :
        MfgPublicationStatus::NoMatch;
    if (protectionFailure) result.status = MfgPublicationStatus::Indeterminate;
    if (result.status == MfgPublicationStatus::Indeterminate) latch.indeterminate = true;
    return result;
}
}
