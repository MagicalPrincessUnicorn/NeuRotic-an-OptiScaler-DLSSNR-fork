#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace Neurotic::D3D12::NativeQueryInsertion
{
// Identity of the terminal native recording, never an address-only lifetime claim.
struct Boundary
{
    std::uintptr_t list = 0;
    std::uint64_t incarnation = 0, hookGeneration = 0, workOrdinal = 0;
    // Nonzero only after the intercepted outer wrapper EndQuery has returned.
    std::uintptr_t executionCaller = 0;
};
enum class Outcome { Skipped, Recorded, Failed };
enum class Reason
{
    None, DataWork, Reset, Closed, Aliased, UnknownEffect, CoverageLost,
    ForwardingFailed, Superseded, Capacity, QueryReopened
};
struct Operation
{
    virtual ~Operation() = default;
    virtual Outcome Record(const Boundary&) noexcept = 0;
    virtual void Cancel(Reason) noexcept = 0;
    virtual void CancelAt(Reason reason, std::uint32_t) noexcept { Cancel(reason); }
    // Optional, default-refused policy. The concrete request must authenticate
    // descriptor ownership; the hook supplies observation, never invented state.
    virtual bool AllowsRtvClear(const Boundary&, std::uintptr_t) noexcept { return false; }
};

// Only the hook adapter may publish EndReturned, after the actual native call.
// Readiness is not execution: Drain belongs after the outer forwarding stack,
// temporary COM owners, and observer locks have all unwound.
class Owner
{
    struct Entry { Boundary armed; std::shared_ptr<Operation> operation; bool ready = false; };
    mutable std::mutex mutex_;
    std::map<std::uintptr_t, Entry> entries_;
    bool closing_ = false;
    static bool SameEpoch(const Boundary& a, const Boundary& b) noexcept
    { return a.list == b.list && a.incarnation == b.incarnation && a.hookGeneration == b.hookGeneration; }
public:
    Owner() = default;
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    ~Owner()
    {
        { std::lock_guard lock(mutex_); closing_ = true; }
        CancelAll(Reason::CoverageLost);
    }
    bool Arm(Boundary boundary, std::shared_ptr<Operation> operation)
    {
        if (!operation || !boundary.list || !boundary.incarnation || !boundary.hookGeneration) return false;
        std::shared_ptr<Operation> retired;
        {
            std::lock_guard lock(mutex_);
            if (closing_) return false;
            auto found = entries_.find(boundary.list);
            if (found == entries_.end())
            {
                // Resource pressure refuses this insertion, not the game's recording.
                if (entries_.size() >= 64) return false;
                entries_.emplace(boundary.list, Entry{boundary, operation, false});
            }
            else
            {
                retired = std::move(found->second.operation);
                found->second = Entry{boundary, operation, false};
            }
        }
        if (retired) retired->Cancel(Reason::Superseded);
        return true;
    }
    void EndReturned(const Boundary& current)
    {
        std::lock_guard lock(mutex_);
        auto found = entries_.find(current.list);
        if (found != entries_.end() && SameEpoch(found->second.armed, current) &&
            current.workOrdinal >= found->second.armed.workOrdinal)
            found->second.ready = true;
    }
    Outcome Drain(const Boundary& current)
    {
        std::shared_ptr<Operation> selected;
        Reason invalid = Reason::None;
        {
            std::lock_guard lock(mutex_);
            auto found = entries_.find(current.list);
            if (found == entries_.end()) return Outcome::Skipped;
            if (!SameEpoch(found->second.armed, current) || current.workOrdinal < found->second.armed.workOrdinal)
                invalid = current.incarnation != found->second.armed.incarnation ? Reason::Reset : Reason::CoverageLost;
            else if (!found->second.ready) return Outcome::Skipped;
            selected = std::move(found->second.operation);
            entries_.erase(found);
        }
        // Both callbacks and the final owner release may reenter command hooks.
        if (invalid != Reason::None) { selected->Cancel(invalid); return Outcome::Skipped; }
        return selected->Record(current);
    }
    void Cancel(std::uintptr_t list, Reason reason, std::uint32_t slot = UINT32_MAX)
    {
        std::shared_ptr<Operation> retired;
        {
            std::lock_guard lock(mutex_);
            auto found = entries_.find(list);
            if (found == entries_.end()) return;
            retired = std::move(found->second.operation);
            entries_.erase(found);
        }
        retired->CancelAt(reason, slot);
    }
    void RtvClear(const Boundary& current, std::uintptr_t descriptor)
    {
        std::shared_ptr<Operation> selected;
        Boundary armed;
        {
            std::lock_guard lock(mutex_);
            const auto f = entries_.find(current.list);
            if (f == entries_.end()) return;
            selected = f->second.operation; armed = f->second.armed;
        }
        // Policy and final foreign owner release are outside the mutex. A
        // reentrant replacement cannot inherit this earlier decision.
        const bool allowed = SameEpoch(armed, current) && current.workOrdinal >= armed.workOrdinal &&
            selected->AllowsRtvClear(current, descriptor);
        std::shared_ptr<Operation> retired;
        {
            std::lock_guard lock(mutex_);
            const auto f = entries_.find(current.list);
            if (f == entries_.end() || f->second.operation != selected ||
                !SameEpoch(f->second.armed, armed) || f->second.armed.workOrdinal != armed.workOrdinal) return;
            if (allowed) return;
            retired = std::move(f->second.operation); entries_.erase(f);
        }
        retired->CancelAt(Reason::DataWork, 48);
    }
    void CancelAll(Reason reason)
    {
        std::map<std::uintptr_t, Entry> retired;
        { std::lock_guard lock(mutex_); retired.swap(entries_); }
        for (auto& [list, entry] : retired) entry.operation->Cancel(reason);
    }
    std::size_t Pending() const { std::lock_guard lock(mutex_); return entries_.size(); }
};
}
