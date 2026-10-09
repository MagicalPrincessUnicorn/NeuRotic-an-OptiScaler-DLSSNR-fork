#pragma once
#include "CapabilityStore.h"
namespace DlssNr::Capability {
struct WriterCell {
    std::weak_ptr<StoreState> store;
    uint64_t id=0; ProducerId producer{}; std::string session,subjectGroup;
    std::atomic<uint64_t> offered{0},committed{0}; std::atomic<bool> closed{false};
    std::unique_ptr<Batch> active,pending;
    std::mutex pendingMutex; uint64_t pendingSequence=0;
};
struct StoreState {
    explicit StoreState(std::string s):session(std::move(s)){}
    std::string session; std::mutex mutex;
    std::array<std::shared_ptr<WriterCell>,128> slots{};
    uint64_t nextWriter=0,revision=0; bool closed=false;
    std::atomic<unsigned> snapshots{0};
};
}
