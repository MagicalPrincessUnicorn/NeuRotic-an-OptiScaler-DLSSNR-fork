#pragma once
#include "PresentFgScope.h"
#include "PresentFgRestoration.h"
// CPU ordering only. Tickets own scalar bookkeeping, never provider objects,
// tokens, resources, command lists or foreign descriptor storage. Every method
// releases gate_ before returning; the caller performs the actual API outside it.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace DlssNr::PreFg::FinalColor
{
enum class Disposition { Original, NullOptional };

class Controller
{
    struct Channel;
    // CPU adapter phases only, never GPU or provider-resource retirement.
    struct ForwardPhases { uint64_t publications = 0, awaitingForward = 0; };
    struct Operation
    {
        std::weak_ptr<Channel> channel;
        uint64_t key = 0, poison = 0, revision = 0;
        unsigned constantsInFlight = 0;
        bool stickyReset = false, reset = false, constantsBatchFailed = false, accepted = false;
        bool published = false, publicationInFlight = false, clearInFlight = false, unsafe = false;
        bool awaitingForward = false;
    };
    struct TagCall
    {
        std::shared_ptr<Channel> channel;
        uint64_t key = 0;
        bool legacy = false, filtered = false, opaque = false, finished = false;
    };
    struct ConstantsCall
    {
        std::shared_ptr<Channel> channel;
        std::shared_ptr<Operation> operation;
        bool finished = false;
    };
    struct ClearCall
    {
        std::shared_ptr<Channel> channel;
        std::shared_ptr<Operation> operation;
        uint64_t poison = 0, revision = 0;
        bool finished = false;
    };
    struct PublicationCall
    {
        std::shared_ptr<Channel> channel;
        std::shared_ptr<Operation> operation;
        Scope scope;
        uint64_t poison = 0;
        bool finished = false;
    };
    struct Channel
    {
        Scope scope;
        std::shared_ptr<ForwardPhases> forward;
        Restoration restoration;
        bool policyInstalled = false, unsafePublication = false;
        bool wanted = false, armed = false, filtering = false, frontierKnown = false, exiting = false;
        bool coherent = false, needReset = true, restorationPending = false, exitResetKnown = false;
        uint64_t frontier = 0, poison = 0;
        unsigned filteredInFlight = 0, opaqueInFlight = 0, constantsInFlight = 0;
        std::size_t next = 0;
        std::array<std::shared_ptr<Operation>, 16> operations {};
        std::unordered_map<uint64_t, std::weak_ptr<Operation>> keyed;
        std::vector<std::weak_ptr<TagCall>> originals;
    };
public:
    using OperationPtr = std::shared_ptr<Operation>;
    struct ConstantsTicket { bool forceReset = false; std::shared_ptr<ConstantsCall> call; };
    struct ClearTicket { bool allowed = false; std::shared_ptr<ClearCall> call; };
    struct TagTicket { Disposition disposition = Disposition::Original; std::shared_ptr<TagCall> call; };
    struct PublicationTicket { bool allowed = false; std::shared_ptr<PublicationCall> call; };
    class FallbackTicket
    {
        friend class Controller;
        Controller* owner_ = nullptr;
        explicit FallbackTicket(Controller* owner) : owner_(owner) {}
    public:
        FallbackTicket() = default;
        FallbackTicket(const FallbackTicket&) = delete;
        FallbackTicket& operator=(const FallbackTicket&) = delete;
        FallbackTicket(FallbackTicket&& other) noexcept : owner_(std::exchange(other.owner_, nullptr)) {}
        FallbackTicket& operator=(FallbackTicket&& other) = delete;
    };
    struct Observation { bool accepted = false, published = false, unsafe = false, restorationPending = false; };
    struct UiObservation
    {
        Scope scope {};
        bool wanted = false, installed = false, exiting = false, restorationPending = false, unsafe = false;
        FreshPolicy fresh = FreshPolicy::Pending;
    };

private:
    std::mutex gate_;
    std::unordered_map<uint64_t, std::shared_ptr<Channel>> channels_;
    // Weak registry of current and ticket-owned older scopes. Scope changes
    // invalidate receipts; they do not retire actual provider calls.
    std::vector<std::weak_ptr<Channel>> enrolled_;
    uint64_t fallbackInFlight_ = 0;

    bool AnyFiltered(uint64_t viewport) const
    {
        for (const auto& weak : enrolled_)
            if (const auto channel = weak.lock(); channel && channel->scope.viewport == viewport && channel->filteredInFlight) return true;
        return false;
    }
    bool AnyOpaque(uint64_t viewport) const
    {
        for (const auto& weak : enrolled_)
            if (const auto channel = weak.lock(); channel && channel->scope.viewport == viewport && channel->opaqueInFlight) return true;
        return false;
    }
    bool OlderConstants(const std::shared_ptr<Channel>& current) const
    {
        for (const auto& weak : enrolled_)
            if (const auto channel = weak.lock(); channel && channel != current &&
                channel->scope.viewport == current->scope.viewport && channel->constantsInFlight) return true;
        return false;
    }
    bool OriginalPending(const std::shared_ptr<Channel>& current) const
    {
        for (const auto& weak : enrolled_)
        {
            const auto channel = weak.lock();
            if (!channel || channel->scope.viewport != current->scope.viewport) continue;
            for (const auto& tagWeak : channel->originals)
                if (const auto call = tagWeak.lock(); call && !call->finished &&
                    (channel != current || call->legacy || !Before(call->key, current->frontier))) return true;
        }
        return false;
    }

    // Keys are the existing uint32 frame token plus one, not time stamps.
    static bool Before(uint64_t a, uint64_t b)
    {
        const uint32_t delta = static_cast<uint32_t>(a - b);
        return delta != 0 && delta >= 0x80000000u;
    }
    std::shared_ptr<Channel> Entry(const Scope& scope)
    {
        auto& channel = channels_[scope.viewport];
        if (!channel || !(channel->scope == scope))
        {
            const bool restorationPending = channel && (channel->restorationPending || channel->filtering);
            const bool filtering = (channel && channel->filtering) || AnyFiltered(scope.viewport);
            const bool wanted = channel && channel->wanted;
            auto replacement = std::make_shared<Channel>();
            replacement->scope = scope;
            replacement->forward = channel ? channel->forward : std::make_shared<ForwardPhases>();
            if (channel) replacement->restoration = channel->restoration;
            replacement->restoration.ChangeScope(scope);
            if (!wanted && (filtering || restorationPending)) replacement->restoration.ExitRequested(scope);
            replacement->restorationPending = restorationPending;
            replacement->filtering = filtering; replacement->wanted = wanted;
            replacement->exiting = !wanted && filtering;
            enrolled_.erase(std::remove_if(enrolled_.begin(), enrolled_.end(),
                [](const auto& weak) { return weak.expired(); }), enrolled_.end());
            enrolled_.push_back(replacement);
            channel = std::move(replacement);
        }
        return channel;
    }
    bool Current(const std::shared_ptr<Channel>& channel, const Scope& scope) const
    {
        const auto it = channels_.find(scope.viewport);
        return channel && channel->scope == scope && it != channels_.end() && it->second == channel;
    }
    static bool Tracked(const std::shared_ptr<Channel>& channel, const OperationPtr& op)
    {
        if (!channel || !op) return false;
        const auto it = channel->keyed.find(op->key);
        return it != channel->keyed.end() && it->second.lock() == op;
    }
    static OperationPtr Find(const std::shared_ptr<Channel>& channel, uint64_t key)
    {
        const auto found = channel->keyed.find(key);
        if (found != channel->keyed.end()) if (const auto op = found->second.lock()) return op;
        for (auto it = channel->keyed.begin(); it != channel->keyed.end(); )
            if (it->second.expired()) it = channel->keyed.erase(it); else ++it;
        auto op = std::make_shared<Operation>();
        op->channel = channel; op->key = key; op->poison = channel->poison;
        channel->keyed[key] = op;
        channel->operations[channel->next++ % channel->operations.size()] = op;
        return op;
    }
    void FinishExit(const std::shared_ptr<Channel>& channel)
    {
        if (channel->exiting && channel->exitResetKnown && !AnyFiltered(channel->scope.viewport) &&
            !channel->forward->publications && !channel->forward->awaitingForward)
        {
            channel->filtering = false;
            channel->exiting = false;
            // This opens the original API. It does not restore old registrations.
        }
    }
    void Intent(const std::shared_ptr<Channel>& channel, bool wanted)
    {
        if (channel->wanted == wanted) return;
        channel->wanted = wanted;
        channel->coherent = false; channel->needReset = true;
        channel->exitResetKnown = false;
        if (wanted) channel->exiting = false;
        if (!wanted && channel->filtering)
        {
            channel->restorationPending = true;
            channel->restoration.ExitRequested(channel->scope);
            channel->exiting = true;
            FinishExit(channel);
        }
    }
    bool Clean(const OperationPtr& op, const std::shared_ptr<Channel>& channel) const
    {
        return op && channel && !op->unsafe && op->poison == channel->poison &&
            !op->constantsInFlight && !op->clearInFlight && !op->publicationInFlight &&
            !fallbackInFlight_ && !AnyOpaque(channel->scope.viewport) && !OlderConstants(channel) && !OriginalPending(channel);
    }
    bool Publishable(const OperationPtr& op, const Scope& scope) const
    {
        const auto channel = op ? op->channel.lock() : nullptr;
        return Current(channel, scope) && Tracked(channel, op) && channel->wanted &&
            op->accepted && !op->published && !op->publicationInFlight && !op->clearInFlight && Clean(op, channel);
    }
public:
    UiObservation Inspect(uint64_t viewport)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto found = channels_.find(viewport);
        if (found == channels_.end() || !found->second) return {};
        const auto& channel = found->second;
        return {channel->scope, channel->wanted, channel->policyInstalled, channel->exiting,
            channel->restorationPending, channel->unsafePublication, channel->restoration.Read(channel->scope)};
    }
    bool Observing(uint64_t viewport)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto found = channels_.find(viewport);
        if (found == channels_.end() || !found->second) return AnyFiltered(viewport);
        const auto& channel = found->second;
        return channel->wanted || channel->filtering || channel->restorationPending || AnyFiltered(viewport);
    }
    OperationPtr Request(const Scope& scope, uint64_t key, bool wanted)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto channel = Entry(scope);
        Intent(channel, wanted);
        channel->armed = wanted;
        return Find(channel, key);
    }
    ConstantsTicket BeginConstants(const Scope& scope, uint64_t key, bool wanted)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto channel = Entry(scope);
        Intent(channel, wanted);
        const auto op = Find(channel, key);
        const bool force = op->stickyReset || (channel->exiting && !channel->exitResetKnown) ||
            (channel->armed && wanted && (channel->needReset || !channel->coherent));
        auto call = std::make_shared<ConstantsCall>(); call->operation = op; call->channel = channel;
        op->stickyReset |= force;
        if (!op->constantsInFlight) op->constantsBatchFailed = false;
        ++op->constantsInFlight; ++channel->constantsInFlight; ++op->revision;
        if (force) { channel->needReset = true; channel->coherent = false; }
        return {force, call};
    }
    void EndConstants(const ConstantsTicket& ticket, bool success, bool actualReset, const Scope& current)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto call = ticket.call;
        if (!call || call->finished) return;
        call->finished = true;
        const auto op = call->operation; const auto channel = call->channel;
        --op->constantsInFlight; --channel->constantsInFlight;
        if (!Current(channel, current) || !Tracked(channel, op))
        {
            op->reset = false; op->accepted = false; op->unsafe |= op->published;
            return;
        }
        if (!success || (op->stickyReset && !actualReset))
        {
            op->constantsBatchFailed = true;
            ++op->revision;
            op->reset = false; op->accepted = false; op->unsafe |= op->published;
            channel->needReset = true; channel->coherent = false;
        }
        else if (actualReset && !op->constantsInFlight && !op->constantsBatchFailed &&
            !fallbackInFlight_ && !AnyOpaque(channel->scope.viewport) && !OlderConstants(channel))
        {
            op->reset = true;
            op->poison = channel->poison;
            if (!channel->wanted && channel->exiting) channel->exitResetKnown = true;
            FinishExit(channel);
        }
    }
    ClearTicket BeginClear(const OperationPtr& op, const Scope& current)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto channel = op ? op->channel.lock() : nullptr;
        if (!Current(channel, current) || !Tracked(channel, op) || !channel->wanted ||
            op->published || op->publicationInFlight || op->clearInFlight || op->constantsInFlight ||
            fallbackInFlight_ || AnyOpaque(channel->scope.viewport) || OlderConstants(channel) ||
            (!(op->reset && op->poison == channel->poison) && (!channel->coherent || channel->needReset))) return {};
        if (!channel->filtering || !channel->frontierKnown)
        {
            channel->filtering = true; channel->frontier = op->key; channel->frontierKnown = true;
        }
        if (OriginalPending(channel)) return {};
        auto call = std::make_shared<ClearCall>(); call->operation = op; call->channel = channel;
        call->poison = channel->poison; call->revision = op->revision;
        op->clearInFlight = true;
        ++channel->filteredInFlight;
        return {true, call};
    }
    void EndClear(const ClearTicket& ticket, bool success, const Scope& current, bool legacy = true)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto call = ticket.call;
        if (!call || call->finished) return;
        call->finished = true;
        const auto op = call->operation; const auto channel = call->channel;
        op->clearInFlight = false;
        if (!channel) return;
        --channel->filteredInFlight;
        if (Current(channel, current) && Tracked(channel, op))
        {
            op->accepted = success && channel->wanted && call->poison == channel->poison &&
                call->revision == op->revision && !op->constantsInFlight &&
                !fallbackInFlight_ && !AnyOpaque(channel->scope.viewport) && !OlderConstants(channel) && !OriginalPending(channel);
            op->poison = call->poison;
            if (!op->accepted) { channel->needReset = true; channel->coherent = false; }
            else
            {
                channel->restoration.ClearAccepted(current, op->key, legacy);
                channel->restorationPending = false; channel->policyInstalled = true;
                channel->unsafePublication = false;
            }
        }
        FinishExit(channel);
    }
    TagTicket BeginTag(const Scope& scope, uint64_t key, bool legacy, bool interpretable)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto channel = Entry(scope);
        FinishExit(channel);
        auto call = std::make_shared<TagCall>();
        call->channel = channel; call->key = key; call->legacy = legacy;
        const bool relevant = legacy || !channel->filtering || !channel->frontierKnown || !Before(key, channel->frontier);
        call->filtered = relevant && channel->filtering && interpretable;
        call->opaque = relevant && !interpretable;
        if (!call->filtered)
        {
            channel->originals.erase(std::remove_if(channel->originals.begin(), channel->originals.end(),
                [](const auto& weak) { const auto old = weak.lock(); return !old || old->finished; }), channel->originals.end());
            channel->originals.push_back(call);
        }
        if (call->opaque)
        {
            ++channel->poison; ++channel->opaqueInFlight;
            channel->coherent = false; channel->needReset = true; channel->policyInstalled = false;
            channel->restoration.UnknownOriginal(channel->scope);
            for (const auto& op : channel->operations) if (op && op->published)
            { op->unsafe = true; channel->unsafePublication = true; }
        }
        if (call->filtered) ++channel->filteredInFlight;
        return {call->filtered ? Disposition::NullOptional : Disposition::Original, call};
    }
    void EndTag(const TagTicket& ticket, bool success, const OptionalDeclarations& declarations = {},
        bool declarationsKnown = false, const Scope* current = nullptr)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto call = ticket.call;
        if (!call || call->finished) return;
        call->finished = true;
        const auto channel = call->channel;
        if (call->filtered) --channel->filteredInFlight;
        if (call->opaque) --channel->opaqueInFlight;
        if (!call->filtered && current)
        {
            const auto live = channels_.find(current->viewport);
            const auto target = live == channels_.end() ? nullptr : live->second;
            if (target)
            {
                if (!Current(channel, *current) || !success || !declarationsKnown || call->opaque)
                    target->restoration.UnknownOriginal(target->scope);
                else if (!target->wanted && !target->filtering && !target->exiting)
                {
                    if (declarations.hudless != Declaration::Omitted || declarations.color != Declaration::Omitted ||
                        declarations.alpha != Declaration::Omitted) target->policyInstalled = false;
                    target->restoration.AcceptedOriginal(*current, declarations, call->key, call->legacy);
                    if (target->restoration.Read(*current) != FreshPolicy::Pending && !OriginalPending(target)) target->restorationPending = false;
                }
            }
        }
        FinishExit(channel);
    }
    // Allocation-free exceptional enrollment. The exact scalar viewport may
    // itself have failed to allocate, so this actual untracked provider call
    // temporarily invalidates local publication proof until its real return.
    // It is never a compatibility whitelist, synthetic return or new API error.
    FallbackTicket BeginFallback()
    {
        const std::lock_guard<std::mutex> lock(gate_);
        ++fallbackInFlight_;
        for (const auto& [viewport, channel] : channels_)
        {
            (void)viewport;
            if (!channel) continue;
            ++channel->poison; channel->coherent = false; channel->needReset = true;
            channel->restorationPending |= channel->filtering || channel->policyInstalled;
            channel->policyInstalled = false; channel->restoration.UnknownOriginal(channel->scope);
            if (channel->exiting) channel->exitResetKnown = false;
        }
        return FallbackTicket(this);
    }
    void EndFallback(FallbackTicket& ticket)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        if (ticket.owner_ != this) return;
        ticket.owner_ = nullptr;
        --fallbackInFlight_;
    }
    bool CanPublish(const OperationPtr& op, const Scope& current)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        return Publishable(op, current);
    }
    PublicationTicket BeginPublication(const OperationPtr& op, const Scope& current)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        if (!Publishable(op, current)) return {};
        auto call = std::make_shared<PublicationCall>();
        call->operation = op; call->channel = op->channel.lock(); call->scope = current; call->poison = op->poison;
        op->publicationInFlight = true;
        ++call->channel->forward->publications;
        return {true, call};
    }
    void EndPublication(const PublicationTicket& ticket, bool actualSubmitted, const Scope& current)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto call = ticket.call;
        if (!call || call->finished) return;
        call->finished = true;
        const auto op = call->operation; const auto channel = call->channel;
        op->publicationInFlight = false;
        --channel->forward->publications;
        op->published |= actualSubmitted;
        if (actualSubmitted && !op->awaitingForward) { op->awaitingForward = true; ++channel->forward->awaitingForward; }
        if (actualSubmitted && (!Current(channel, current) || !(call->scope == current) ||
            call->poison != channel->poison || !op->accepted || !Clean(op, channel))) op->unsafe = true;
        if (actualSubmitted && op->unsafe && channel) channel->unsafePublication = true;
        FinishExit(channel);
    }
    void Handoff(const OperationPtr& op, bool originalEntered, bool resultSucceeded, const Scope& current)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto channel = op ? op->channel.lock() : nullptr;
        if (channel && op->awaitingForward)
        {
            op->awaitingForward = false;
            --channel->forward->awaitingForward;
            FinishExit(channel);
        }
        if (Current(channel, current) && Tracked(channel, op) && channel->wanted && op->accepted &&
            originalEntered && resultSucceeded && Clean(op, channel))
        {
            channel->coherent = true; channel->needReset = false;
        }
    }
    Observation View(const OperationPtr& op)
    {
        const std::lock_guard<std::mutex> lock(gate_);
        const auto channel = op ? op->channel.lock() : nullptr;
        return {op && op->accepted, op && op->published,
            op && (op->unsafe || (channel && op->poison != channel->poison)),
            channel && channel->restorationPending};
    }
};
}
