#pragma once
#include <nr/contracts/C03_Consumption.h>
#include <optional>

namespace Neurotic::Lifecycle
{
enum class Fact { Unknown, No, Yes };
// Resource-owner runtime specialization, not a wire record or identity issuer.
// Ordinals identify ordering milestones inside one reserved recording; they are
// not frame/resource IDs. The allocation owner establishes the reservation only
// after its existing retirement checks and retains it through replay retirement.
struct SameRecordingUse
{
    Contracts::RecordKey recording,reservation;
    std::uint64_t producerOrdinal=0,consumerOrdinal=0;
    std::optional<Contracts::RecordKey> providerRegistration;
    bool operator==(const SameRecordingUse&)const=default;
};
// Authenticated owner input only, held under the allocation owner's lock through
// the operation. activeRecording requires the existing GpuSafety ticket's exact
// current list association, not a decoded receipt. orderEstablished follows the
// actual preparation/barrier milestone; conflictingUsesExcluded includes older
// recordings and external consumers. No fact here grants global storage reuse.
struct SameRecordingFacts
{
    SameRecordingUse use;
    Fact activeRecording=Fact::Unknown,orderEstablished=Fact::Unknown;
    Fact reservationRetained=Fact::Unknown,conflictingUsesExcluded=Fact::Unknown;
    // Fresh, exact-use permissions from the owner for these active holds. A
    // compatible current read/provider consume is not release or future reuse.
    // Proof must cover allocation, consumer, evaluation, recording and operation.
    Contracts::BoundedList<Contracts::RecordKey,16> compatibleHolds;
};
}
