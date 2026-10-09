#pragma once
#include <nr/contracts/Identity.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace Neurotic::Lifecycle
{
// Session-local Native attempt authority. The source enrollment and this
// ledger share one immutable session/stream/view/feature/ingress scope. A
// consumed producer ordinal cannot be re-admitted after a slot retires.
class NativeExecutionLedger
{
  public:
    struct Ticket { std::size_t slot=0;std::uint64_t revision=0; };
  private:
    struct Slot
    {
        bool used=false,claimStarted=false,authorized=false,modelStarted=false,modelCalled=false,modelReturned=false;
        std::uint64_t revision=0;
        C::NativeSampleIdentityV1 sample;
    };
    std::array<Slot,64> slots_{};
    std::uint64_t highestOrdinal_=0;
    Slot* Find(Ticket t)noexcept
    {return t.slot<slots_.size()&&slots_[t.slot].used&&slots_[t.slot].revision==t.revision?&slots_[t.slot]:nullptr;}
    const Slot* Find(Ticket t)const noexcept
    {return t.slot<slots_.size()&&slots_[t.slot].used&&slots_[t.slot].revision==t.revision?&slots_[t.slot]:nullptr;}
  public:
    // Caller holds the coordinator mutation lock. All required storage is
    // already in the session before any owner or model effect is possible.
    std::optional<Ticket> Reserve(const C::NativeSampleIdentityV1& sample)noexcept
    {
        if(sample.Check()!=C::Error::None||sample.producerOrdinal<=highestOrdinal_)return {};
        for(std::size_t i=0;i<slots_.size();++i)
        {
            auto& slot=slots_[i];
            if(slot.used||slot.revision==(std::numeric_limits<std::uint64_t>::max)())continue;
            const auto revision=slot.revision+1;
            slot={};slot.used=true;slot.revision=revision;slot.sample=sample;
            highestOrdinal_=sample.producerOrdinal;
            return Ticket{i,revision};
        }
        return {};
    }
    bool Contains(Ticket ticket)const noexcept{return Find(ticket)!=nullptr;}
    bool BeginClaim(Ticket ticket)noexcept
    {auto* slot=Find(ticket);if(!slot||slot->claimStarted)return false;slot->claimStarted=true;return true;}
    bool Authorize(Ticket ticket)noexcept
    {auto* slot=Find(ticket);if(!slot||!slot->claimStarted||slot->authorized)return false;slot->authorized=true;return true;}
    bool Authorized(Ticket ticket)const noexcept
    {const auto* slot=Find(ticket);return slot&&slot->authorized;}
    bool StartModel(Ticket ticket)noexcept
    {auto* slot=Find(ticket);if(!slot||!slot->authorized||slot->modelStarted)return false;slot->modelStarted=true;return true;}
    bool ModelStarted(Ticket ticket)const noexcept
    {const auto* slot=Find(ticket);return slot&&slot->modelStarted;}
    bool ModelCalled(Ticket ticket)const noexcept
    {const auto* slot=Find(ticket);return slot&&slot->modelCalled;}
    bool MarkModelCalled(Ticket ticket)noexcept
    {auto* slot=Find(ticket);if(!slot||!slot->modelStarted||slot->modelCalled)return false;slot->modelCalled=true;return true;}
    void MarkModelReturned(Ticket ticket)noexcept
    {auto* slot=Find(ticket);if(slot&&slot->modelCalled)slot->modelReturned=true;}
    bool Retire(Ticket ticket)noexcept
    {
        auto* slot=Find(ticket);if(!slot)return false;
        const auto revision=slot->revision;*slot={};slot->revision=revision;
        return true;
    }
};
}
