#include "CapabilityInternal.h"
#include "CapabilityStaging.h"
namespace DlssNr::Capability {
StageResult TryStageEnvelope(const WriterPort& port,uint64_t sequence,const PendingEnvelope& image) noexcept {
    auto& c=*port.cell; if(c.closed.load()) return StageResult::Closed;
    if(image.overflow||image.used>Batch::Capacity) return StageResult::Capacity;
    std::unique_lock lock(c.pendingMutex,std::try_to_lock); if(!lock.owns_lock()) return StageResult::Contended;
    if(c.closed.load()) return StageResult::Closed;
    if(sequence!=c.offered.load()||sequence<=c.committed.load()) return StageResult::StaleSequence;
    *c.pending=image; c.pendingSequence=sequence; return StageResult::Staged;
}
std::vector<PublishResult> AdapterFactory::DrainStagedObservations(const Selection& selection) noexcept {
    try {
        std::array<std::shared_ptr<WriterCell>,128> cells;
        { std::lock_guard lock(state->mutex); cells=state->slots; }
        std::vector<PublishResult> results; results.reserve(128);
        auto image=std::make_unique<Batch>();
        for(auto& c:cells) if(c) {
            uint64_t sequence=0;
            { std::unique_lock lock(c->pendingMutex,std::try_to_lock); if(!lock.owns_lock()) { results.push_back(PublishResult::Contended); continue; }
                if(!c->pendingSequence) continue;
                bool selected=selection.capabilities.empty(); for(size_t i=0;!selected&&i<c->pending->factCount;++i) for(auto id:selection.capabilities) if(c->pending->At<Fact>(c->pending->facts[i]).capability==id) selected=true;
                if(!selected) continue; sequence=c->pendingSequence; *image=*c->pending; c->pendingSequence=0;
            }
            results.push_back(TryPublish(WriterPort(c),sequence,*image));
        }
        return results;
    } catch(...) { return {}; }
}
}
