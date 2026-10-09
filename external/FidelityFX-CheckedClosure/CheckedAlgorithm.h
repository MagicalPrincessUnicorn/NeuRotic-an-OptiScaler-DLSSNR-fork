// Experimental creating-module interface; this is not a public AMD ABI.
#pragma once
#include "CheckedOwnedOutput.h"
namespace ffx::nr {
struct AlgorithmSnapshot {
    FfxNrContextTicketV1 swapchain{};
    uint64_t algorithmGeneration=0,allocationGeneration=0;
    ID3D12Resource* input=nullptr;
    bool registrationCreated=false,registrationComplete=false;
    bool active=false,failed=false,admissionClosed=false,destroyAttempted=false,released=false;
};
struct AlgorithmReleaseReceipt {
    uint64_t algorithmGeneration=0;
    FfxNrDrainHandleV1 drain{};
    uint64_t evidenceRevision=0,cleanupOperations=0;
    int32_t actualResult=-1;
    bool wholeContextDestroyed=false;
};
class AlgorithmLease {
public:
    virtual ~AlgorithmLease()=default;
    virtual AlgorithmSnapshot inspect() const noexcept=0; // atomic reads; no owner/Resource lock
    virtual bool Owns(const std::shared_ptr<OwnedOutputLease>&) const noexcept=0;
    virtual void closeAdmission() noexcept=0;
    virtual FfxNrStatusV1 releaseAfterDrain(const FfxNrDrainHandleV1&,uint64_t revision,AlgorithmReleaseReceipt&) noexcept=0;
};
struct AlgorithmConsumerObserver {
    void* user=nullptr;
    FfxNrStatusV1 (*begin_consumer)(void*,const std::shared_ptr<OwnedOutputLease>&,const std::shared_ptr<AlgorithmLease>&,const FfxNrDispatchTicketV1*,void**)=nullptr;
    void (*end_consumer)(void*,void*,int32_t actualResult)=nullptr;
};
}
